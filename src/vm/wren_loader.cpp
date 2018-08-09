/*
 * Copyright (c) 2018- μNEST Foundation, and contributors.
 *
 * The MIT License
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include "wren_loader.h"

#include <stdio.h>
#include "wren_value.h"
#include "wren_debug.h"
#include "../cjson/cJSON.h"

#include <algorithm>
#include <memory>

#define WREN_FILE_MAGIC       'NERW'
#define WREN_BYTECODDE_MAJOR  1
#define WREN_BYTECODDE_MINOR  0

#ifndef _countof
#define _countof(x)    (sizeof(x) / sizeof((x)[0]))
#endif

#if defined(_DEBUG) || defined(DEBUG)
//#define dbgprint printf
#define dbgprint
#else
#define dbgprint
#endif

#ifndef _WIN32
    #define _stricmp strcasecmp
#endif

#pragma pack(push, 1)

typedef struct TLV
{
  uint8_t  type;
  uint32_t length;
  char     value[0];
} TLV;

typedef struct TLV_NUM
{
  uint8_t  type;
  uint32_t length;
  double   value;    //number value.
} TLV_NUM;

typedef struct TLV_FN
{
  uint8_t  type;
  uint32_t length;
  uint32_t value;    //index number of ObjFn in the current module.
} TLV_FN;

typedef struct TLV TLV_STR;

typedef struct LV
{
  uint32_t length;
  char     value[0];
} LV;

typedef struct DataDirectory
{
  uint32_t offset;
  uint32_t size;
} DataDirectory;

enum MODULE_DATA_DIRECTORY
{
  MODULE_DATA_DIR_VARIABLE    = 0,
  MODULE_DATA_DIR_FN_INDEX    = 1,
  MODULE_DATA_DIR_FN          = 2,
  MODULE_DATA_DIR_METHOD      = 3,
  MODULE_DATA_DIR_RELOCATION  = 4,
  MODULE_DATA_DIR_SECURITY    = 5,
};

typedef struct ModuleFileHeader
{
  uint32_t      magic;
  uint32_t      majorVersion;
  uint32_t      minorVersion;
  char          name[32];
  uint32_t      entryFnIndex;
  DataDirectory dataDir[16];
} ModuleFileHeader;

enum FN_DATA_DIRECTORY
{
  FN_DATA_DIR_CODE       = 0,
  FN_DATA_DIR_CONSTANTS  = 1,
  FN_DATA_DIR_DEBUG      = 2,
};

typedef struct FnHeader
{
  uint32_t maxSlots;
  uint32_t numUpvalues;
  uint32_t arity;
  uint32_t id;
  DataDirectory dataDir[3];
} FnHeader;

enum FN_CONST_TYPE
{
  FN_CONST_TYPE_NUM,
  FN_CONST_TYPE_CLASS,
  FN_CONST_TYPE_CLOSURE,
  FN_CONST_TYPE_FIBER,
  FN_CONST_TYPE_FN,
  FN_CONST_TYPE_FOREIGN,
  FN_CONST_TYPE_INSTANCE,
  FN_CONST_TYPE_LIST,
  FN_CONST_TYPE_MAP,
  FN_CONST_TYPE_MODULE,
  FN_CONST_TYPE_RANGE,
  FN_CONST_TYPE_STRING,
  FN_CONST_TYPE_UPVALUE,

  FN_CONST_TYPE_NULL,
  FN_CONST_TYPE_FALSE,
  FN_CONST_TYPE_TRUE,
  FN_CONST_TYPE_UNDEFINED,
  //only append allowed, because the above value will be saved to byte code.
};

typedef struct MethodNameRebind
{
  uint32_t fnIndex;  
  uint32_t offset;
  uint32_t nameIndex;
} MethodNameRebind;

typedef enum METHOD_DATA_DIR
{
  METHOD_DATA_DIR_NAME,        //strings of method name 
  METHOD_DATA_DIR_REBIND,      //rebind info of method name
} METHOD_DATA_DIR;

#pragma pack(pop)

typedef struct Buffer
{
  char    *pointer;
  uint32_t length;
  uint32_t capacity;
  uint32_t consumed;
  uint32_t consumeLimit;
} Buffer;

typedef struct MethodNameInfo
{
  ObjFn                 *objFn;
  uint32_t               offset;    //offset relative to the start of objFn opcode.
} MethodNameInfo;

extern char *rootDirectory;

bool SaveValueToBuffer(WrenVM *vm, ObjModule *module, Value value, Buffer *buffer);
uint32_t GetFnIndex(WrenVM *vm, ObjModule *module, ObjFn *fnTarget);
ObjFn *GetObjFn(WrenVM *vm, ObjModule *module, uint32_t fnIndex);

void cJsonDeleter(cJSON *node)
{
    if (node)
        cJSON_Delete(node);
}

class gcHelper
{
public:
    gcHelper(WrenVM *vm, Obj *obj)
    {
        _vm = vm;
        wrenPushRoot(vm, obj);
    }

    gcHelper()
        :_vm(nullptr)
    {
    }

    ~gcHelper()
    {
        if (_vm)
            wrenPopRoot(_vm);
    }

    void attach(WrenVM *vm, Obj *obj)
    {
        _vm = vm;
        wrenPushRoot(vm, obj);
    }

    void detach()
    {
        if (_vm)
        {
            wrenPopRoot(_vm);
            _vm = nullptr;
        }
    }

private:
    WrenVM *_vm;    
};

void BufferInit(Buffer *buffer)
{
  buffer->pointer      = NULL;
  buffer->length       = 0;
  buffer->capacity     = 0;
  buffer->consumed     = 0;
  buffer->consumeLimit = 0;
}

bool BufferInitSize(Buffer *buffer, uint32_t size)
{
  if (size)
  {
    buffer->pointer = (char *)malloc(size);
    if (!buffer->pointer)
      return false;

    buffer->capacity = size;
  }
  else
  {
    buffer->pointer  = NULL;
    buffer->capacity = 0;
  }

  buffer->length       = 0;
  buffer->consumed     = 0;
  buffer->consumeLimit = 0;

  return true;
}

bool BufferAppend(Buffer *buffer, const char *data, uint32_t dataLength)
{
  if (!dataLength)
    return true;

  uint32_t lenReq = buffer->length + dataLength;
  if (lenReq > buffer->capacity)
  {
    uint32_t newCapacity = lenReq << 1;
    char *newPointer = (char *)realloc(buffer->pointer, newCapacity);
    if (!newPointer)
      return false;

    buffer->pointer  = newPointer;
    buffer->capacity = newCapacity;
  }

  memcpy(buffer->pointer + buffer->length, data, dataLength);
  buffer->length += dataLength;
  buffer->consumeLimit = buffer->length;

  return true;
}

bool BufferConsume(Buffer *buffer, uint32_t lenDesired, char **data)
{
  if (!lenDesired)
  {
    *data = buffer->pointer + buffer->consumed;
    return true;
  }

  if (buffer->consumeLimit - buffer->consumed < lenDesired)
  {
    ASSERT(false, "consume limit reached");
    return false;
  }

  if (buffer->length - buffer->consumed < lenDesired)
  {
    ASSERT(false, "no enough data to consume");
    return false;
  }

  *data = buffer->pointer + buffer->consumed;
  buffer->consumed += lenDesired;
  return true;
}

bool BufferSeek(Buffer *buffer, uint32_t offset)
{
  if (offset < buffer->length)
  {
    buffer->consumed = offset;
    return true;
  }

  ASSERT(false, "unexpected buffer offset");
  return false;
}

void BufferRewind(Buffer *buffer)
{
  buffer->consumed = 0;
}

void BufferSetLimit(Buffer *buffer, uint32_t limit)
{
  ASSERT(buffer->consumed <= limit
    && limit <= buffer->length, "illegal consume limit");

  buffer->consumeLimit = limit;
}

bool BufferSet(Buffer *buffer, char *data, uint32_t length)
{
  buffer->pointer      = data;
  buffer->length       = length;
  buffer->capacity     = length;
  buffer->consumed     = 0;
  buffer->consumeLimit = length;

  return true;
}

bool IsBufferExhausted(const Buffer *buffer)
{
  return (buffer->consumed == buffer->consumeLimit
    || buffer->consumed == buffer->length);
}

void BufferFree(Buffer *buffer)
{
  if (buffer->pointer)
    free(buffer->pointer);

  buffer->pointer      = NULL;
  buffer->length       = 0;
  buffer->capacity     = 0;
  buffer->consumed     = 0;
  buffer->consumeLimit = 0;
}

static bool GetFnMethodNameInfo(ObjFn* fn, Buffer *buffer)
{
  //foreign function body is empty
  if (!fn->code.count || !fn->code.data)
    return true;

  int ip = 0;

  for (;;)
  {
    Code instruction = (Code)fn->code.data[ip];
    switch (instruction)
    {
    case CODE_SUPER_0:
    case CODE_SUPER_1:
    case CODE_SUPER_2:
    case CODE_SUPER_3:
    case CODE_SUPER_4:
    case CODE_SUPER_5:
    case CODE_SUPER_6:
    case CODE_SUPER_7:
    case CODE_SUPER_8:
    case CODE_SUPER_9:
    case CODE_SUPER_10:
    case CODE_SUPER_11:
    case CODE_SUPER_12:
    case CODE_SUPER_13:
    case CODE_SUPER_14:
    case CODE_SUPER_15:
    case CODE_SUPER_16:

    case CODE_CALL_0:
    case CODE_CALL_1:
    case CODE_CALL_2:
    case CODE_CALL_3:
    case CODE_CALL_4:
    case CODE_CALL_5:
    case CODE_CALL_6:
    case CODE_CALL_7:
    case CODE_CALL_8:
    case CODE_CALL_9:
    case CODE_CALL_10:
    case CODE_CALL_11:
    case CODE_CALL_12:
    case CODE_CALL_13:
    case CODE_CALL_14:
    case CODE_CALL_15:
    case CODE_CALL_16:

    case CODE_METHOD_STATIC:
    case CODE_METHOD_INSTANCE:
      {
        MethodNameInfo nameInfo;
        nameInfo.objFn  = fn;
        nameInfo.offset = ip + 1;
        if (!BufferAppend(buffer, (const char *)&nameInfo, sizeof(nameInfo)))
        {
          ASSERT(false, "out of memory");
          return false;
        }
      }
      break;

    case CODE_END:
      return true;

    default:
      break;
    }

    ip += 1 + getNumArguments(fn->code.data, fn->constants.data, ip);
  }

  return true;
}

static bool GetModuleMethodNameInfo(WrenVM *vm, ObjModule *module, Buffer *buffer)
{
  bool ret = true;

  uint32_t index = 0;

  for (Obj *obj = vm->first; obj; obj = obj->next)
  {
    if (obj->type != OBJ_FN)
      continue;

    ObjFn *fn = (ObjFn *)obj;
    if (fn->module != module)
      continue;

    if (!GetFnMethodNameInfo(fn, buffer))
    {
      ret = false;
      break;
    }

    index++;
  }

  return ret;
}

static bool SaveMethodNamesToBuffer(WrenVM *vm, ObjModule *module,
  SymbolTable *methodNames, Buffer *buffer)
{
  for (int k = 0; k < methodNames->count; k++)
  {
    if (!BufferAppend(buffer, (const char *)&methodNames->data[k]->length, sizeof(uint32_t))
      || !BufferAppend(buffer, (const char *)methodNames->data[k]->value, methodNames->data[k]->length))
    {
      return false;
    }
  }

  return true;
}

//only save method names of a single module.
static bool SaveModuleMethodToBuffer(WrenVM *vm, ObjModule *module, Buffer *buffer)
{
  bool ret = true;

  dbgprint("module %s\n", MODULE_NAME(module));

  Buffer infoBuffer;
  BufferInit(&infoBuffer);

  if (!GetModuleMethodNameInfo(vm, module, &infoBuffer))
  {
    BufferFree(&infoBuffer);
    return false;
  }

  Buffer nameBuffer;
  Buffer rebindBuffer;

  BufferInit(&nameBuffer);
  BufferInit(&rebindBuffer);

  SymbolTable methodNames;
  wrenSymbolTableInit(&methodNames);

  MethodNameInfo *info;
  while(!IsBufferExhausted(&infoBuffer)
    && BufferConsume(&infoBuffer, sizeof(MethodNameInfo), (char **)&info))
  {
    ObjFn *objFn = info->objFn;

    if (info->offset < 1)
    {
      ASSERT(false, "bad offset of operand");
      ret = false;
      break;
    }

    uint8_t instruction = objFn->code.data[info->offset - 1];
    ASSERT(instruction == CODE_METHOD_INSTANCE
      || instruction == CODE_METHOD_STATIC
      || (CODE_CALL_0 <= instruction && instruction <= CODE_CALL_16)
      || (CODE_SUPER_0 <= instruction && instruction <= CODE_SUPER_16),
      "unexpected instruction");

    if (info->offset + sizeof(uint16_t) > (uint32_t)objFn->code.count)
    {
      ASSERT(false, "instruction out of bounds");
      ret = false;
      break;
    }

    uint8_t *operand = (uint8_t *)&objFn->code.data[info->offset];
    uint16_t symbol = (operand[0] << 8) | operand[1];
    ASSERT(symbol < vm->methodNames.count, "unexpected method name index");

    const char *data       = vm->methodNames.data[symbol]->value;
    uint32_t    dataLength = vm->methodNames.data[symbol]->length;

    int index = wrenSymbolTableEnsure(vm, &methodNames, data, dataLength);

    MethodNameRebind rebindInfo;
    rebindInfo.fnIndex   = GetFnIndex(vm, module, objFn);
    if (rebindInfo.fnIndex == (uint32_t)-1)
    {
      ASSERT(false, "fn not found");
      ret = false;
      break;
    }

    rebindInfo.offset    = info->offset;
    rebindInfo.nameIndex = index;
    
    dbgprint("    method %u ('%s'), fn = %u (0x%p), offset = 0x%x(%u), code count = %d\n",
      index, data, rebindInfo.fnIndex, objFn, info->offset, info->offset,
      objFn->code.count);

    if (!BufferAppend(&rebindBuffer, (const char *)&rebindInfo, sizeof(rebindInfo)))
    {
      ret = false;
      break;
    }
  }

  BufferFree(&infoBuffer);

  if (ret && SaveMethodNamesToBuffer(vm, module, &methodNames, &nameBuffer))
  {
    DataDirectory dataDir[2];

    uint32_t offset = sizeof(dataDir);

    dataDir[METHOD_DATA_DIR_NAME].offset = offset;
    dataDir[METHOD_DATA_DIR_NAME].size   = nameBuffer.length;
    offset += dataDir[METHOD_DATA_DIR_NAME].size;

    dataDir[METHOD_DATA_DIR_REBIND].offset = offset;
    dataDir[METHOD_DATA_DIR_REBIND].size   = rebindBuffer.length;

    if (!BufferAppend(buffer, (const char *)dataDir, sizeof(dataDir))
      || !BufferAppend(buffer, (const char *)nameBuffer.pointer, nameBuffer.length)
      || !BufferAppend(buffer, (const char *)rebindBuffer.pointer, rebindBuffer.length)
      )
    {
      ret = false;
    }
  }

  wrenSymbolTableClear(vm, &methodNames);
  BufferFree(&rebindBuffer);
  BufferFree(&nameBuffer);

  return ret;
}

// save all variable names of a single module.
static bool SaveMoudleVariableToBuffer(WrenVM *vm, ObjModule *module, Buffer *buffer)
{
  int numToIgnore;

  bool isCoreModule = module->name == NULL;
  if (isCoreModule)
  {
    //the following 3 classes are defined with code, no need to save.
    //ObjClass *objectClass;
    //ObjClass *classClass;
    //ObjClass* objectMetaclass;
    numToIgnore = 3;
  }
  else
  {
    ObjModule *coreModule = getModule(vm, NULL_VAL);

    ASSERT(module->variableNames.count >= coreModule->variableNames.count,
      "all variables of core module must be implicitly imported");

    numToIgnore = coreModule->variableNames.count;
  }

  dbgprint("%s\n", MODULE_NAME(module));
    
  for (int k = numToIgnore; k < module->variableNames.count; k++)
  {
    Value value = module->variables.data[k];

    dbgprint("    var %d: %s, ", k, module->variableNames.data[k]->value);

    const char *data       = module->variableNames.data[k]->value;
    uint32_t    dataLength = module->variableNames.data[k]->length;
    if (!BufferAppend(buffer, (const char *)&dataLength, sizeof(uint32_t))
      || !BufferAppend(buffer, data, dataLength)
      || !SaveValueToBuffer(vm, module, value, buffer))
    {
      return false;
    }
  }

  return true;
}

uint32_t GetFnIndex(WrenVM *vm, ObjModule *module, ObjFn *fnTarget)
{
  uint32_t index = 0;

  for (Obj *obj = vm->first; obj; obj = obj->next)
  {
    if (obj->type != OBJ_FN)
      continue;

    ObjFn *fn = (ObjFn *)obj;
    if (fn->module != module)
      continue;

    if (fn == fnTarget)
      return index;

    index++;
  }

  ASSERT(false, "ObjFn not found!");
  return (uint32_t)-1;
}

//TODO: 此处增加对新的类型的处理。
bool SaveValueToBuffer(WrenVM *vm, ObjModule *module,
  Value value, Buffer *buffer)
{
#if WREN_NAN_TAGGING
  
  //wrenDumpValue(value);
  dbgprint("\n");

  if (IS_NUM(value))
  {
    TLV_NUM tlvNum;
    tlvNum.type = FN_CONST_TYPE_NUM;
    tlvNum.length = sizeof(tlvNum.value);
    tlvNum.value = AS_NUM(value);
    if (!BufferAppend(buffer, (const char *)&tlvNum, sizeof(tlvNum)))
      return false;
  }
  else if (IS_OBJ(value))
  {
    Obj *obj = AS_OBJ(value);
    switch (obj->type)
    {
      case OBJ_STRING:
        {
          TLV_STR tlvStr;
          ObjString *objStr = (ObjString *)obj;
          tlvStr.type = FN_CONST_TYPE_STRING;
          tlvStr.length = objStr->length;
          if (!BufferAppend(buffer, (const char *)&tlvStr, sizeof(tlvStr))
            || !BufferAppend(buffer, objStr->value, objStr->length))
          {
            return false;
          }
        }
        break;
      case OBJ_FN:
        {
          TLV_FN  tlvFn;
          tlvFn.type = FN_CONST_TYPE_FN;
          ObjFn *objFn = (ObjFn *)obj;
          tlvFn.length = sizeof(tlvFn.value);
          tlvFn.value = GetFnIndex(vm, module, objFn);
          if (tlvFn.value == (uint32_t)-1
            || !BufferAppend(buffer, (const char *)&tlvFn, sizeof(tlvFn)))
            return false;
        }
        break;
      case OBJ_CLOSURE:
        {
          ObjClosure *objClosure = (ObjClosure *)obj;
          break;
        }
        break;
      case OBJ_CLASS:
        {
          ObjClass *objClass = (ObjClass *)obj;
          break;
        }
        break;
      case OBJ_INSTANCE:
        {
          ObjInstance *objInstance = (ObjInstance *)obj;
          break;
        }
        break;
      case OBJ_FOREIGN:
        {
          ObjForeign *objForeign = (ObjForeign *)obj;
          break;
        }
        break;
      case OBJ_LIST:
        {
          ObjList *objList = (ObjList *)obj;
          break;
        }
        break;
      case OBJ_MAP:
        {
          ObjMap *objMap = (ObjMap *)obj;
          break;
        }
        break;
      case OBJ_RANGE:
        {
          ObjRange *objRange = (ObjRange *)obj;
          break;
        }
        break;
      case OBJ_FIBER:
        {
          ASSERT(false, "unexpected value type");
        }
        break;
      case OBJ_MODULE:
        {
          ASSERT(false, "unexpected value type");
        }
        break;
      case OBJ_UPVALUE:
        {
          ASSERT(false, "unexpected value type");
        }
        break;

      default:
        ASSERT(false, "add code here to process more types!");
        return false;
    }
  }
  else
  {
    switch (value)
    {
    case NULL_VAL:
      {
        TLV_NUM tlvNum;
        tlvNum.type   = FN_CONST_TYPE_NULL;
        tlvNum.length = sizeof(tlvNum.value);
        *((uint64_t *)&tlvNum.value) = NULL_VAL;
        if (!BufferAppend(buffer, (const char *)&tlvNum, sizeof(tlvNum)))
          return false;
      }
      break;
    case FALSE_VAL:
      ASSERT(false, "add code here to process more types!");
      break;
    case TRUE_VAL:
      ASSERT(false, "add code here to process more types!");
      break;
    case UNDEFINED_VAL:
      ASSERT(false, "add code here to process more types!");
      break;
    default:
      ASSERT(false, "add code here to process more types!");
      return false;
    }
  }

  return true;
#else
  ASSERT(false, "add code here to process more types!");
  return false;
#endif
}

// save each constant as a TLV struct(Type + Length + Value).
static bool SaveFnConstantsToBuffer(WrenVM *vm, ObjModule *module,
  ObjFn *fn, Buffer *buffer)
{
  dbgprint("%s.%s\n", MODULE_NAME(module),
          fn->debug->name ? fn->debug->name : "(script)");

  for (int k = 0; k < fn->constants.count; k++)
  {
    dbgprint("    const %d: ", k);

    Value value = fn->constants.data[k];
    if (!SaveValueToBuffer(vm, module, value, buffer))
      return false;
  }

  return true;
}

static bool SaveModuleFnToBuffer(WrenVM *vm, ObjModule *module,
  Buffer *indexBuffer, Buffer *buffer, uint32_t *entryFnIndex)
{
  bool ret = true;

  *entryFnIndex = (uint32_t)-1;

  uint32_t fnIndex = 0;
  for (Obj *obj = vm->first; obj; obj = obj->next)
  {
    if (obj->type != OBJ_FN)
      continue;

    ObjFn *fn = (ObjFn *)obj;
    if (fn->module != module)
      continue;

    Buffer constantBuffer;
    BufferInit(&constantBuffer);
    if (!SaveFnConstantsToBuffer(vm, module, fn, &constantBuffer))
    {
      ret = false;
      BufferFree(&constantBuffer);
      break;
    }

    FnHeader header;
    header.maxSlots    = fn->maxSlots;
    header.numUpvalues = fn->numUpvalues;
    header.arity       = fn->arity;
    header.id          = fn->abi.id;

    uint32_t offset = sizeof(header);

    header.dataDir[FN_DATA_DIR_CODE].size   = fn->code.count;
    header.dataDir[FN_DATA_DIR_CODE].offset = offset;
    offset += header.dataDir[FN_DATA_DIR_CODE].size;

    header.dataDir[FN_DATA_DIR_CONSTANTS].size   = constantBuffer.length;
    header.dataDir[FN_DATA_DIR_CONSTANTS].offset = offset;
    offset += header.dataDir[FN_DATA_DIR_CONSTANTS].size;

    uint32_t nameLength = fn->debug->name ? strlen(fn->debug->name) : 0;

    uint32_t numDebugBytes = nameLength;
    numDebugBytes += fn->debug->sourceLines.count
                      * sizeof(fn->debug->sourceLines.data[0]);

    header.dataDir[FN_DATA_DIR_DEBUG].size   = numDebugBytes;
    header.dataDir[FN_DATA_DIR_DEBUG].offset = offset;

    uint32_t oldOffset = buffer->length;

    ret = BufferAppend(buffer, (const char *)&header, sizeof(header))
      && BufferAppend(buffer, (const char *)fn->code.data, fn->code.count)
      && BufferAppend(buffer, constantBuffer.pointer, constantBuffer.length)
      && BufferAppend(buffer, (const char *)&nameLength, sizeof(uint32_t))
      && BufferAppend(buffer, fn->debug->name, nameLength)
      && BufferAppend(buffer, (const char *)fn->debug->sourceLines.data, numDebugBytes);
    BufferFree(&constantBuffer);
    if (!ret)
    {
      break;
    }

    DataDirectory dataDir;
    dataDir.offset = oldOffset;     //offset relative to beginning of dataDir[MODULE_DATA_DIR_FN];
    dataDir.size   = buffer->length - oldOffset;
    if (!BufferAppend(indexBuffer, (const char *)&dataDir, sizeof(dataDir)))
    {
      ret = false;
      break;
    }

    if (fn->debug->name && !strcmp(fn->debug->name, "(script)"))
    {
      ASSERT(*entryFnIndex == (uint32_t)-1, "too many block entry fn");
      *entryFnIndex = fnIndex;
    }

    fnIndex++;
  }

  return ret;
}

char *GetModuleFilePath(const char *moduleName)
{
  static const char suffix[] = ".wrc";

  bool hasSuffix = false;
  size_t len = strlen(moduleName);
  if (len > sizeof(suffix) - 1)
  {
    hasSuffix = _stricmp(moduleName + len - (sizeof(suffix) - 1), suffix) == 0;
  }

  char *name = NULL;

  if (rootDirectory)
  {
    name = (char *)malloc(strlen(rootDirectory) + strlen(moduleName) + sizeof(suffix));
    if (name)
    {
      strcpy(name, rootDirectory);
      strcat(name, moduleName);
      if (!hasSuffix)
        strcat(name, suffix);
    }
  }
  else
  {
    name = (char *)malloc(strlen(moduleName) + sizeof(suffix));
    if (name)
    {
      strcpy(name, moduleName);
      if (!hasSuffix)
        strcat(name, suffix);
    }
  }

  return name;
}

char *GetModuleFileName(ObjModule *module)
{
  const char *moduleName = MODULE_NAME(module);
  return GetModuleFilePath(moduleName);
}

// save all info of a single module to a file.
#ifdef __cplusplus
extern "C"
#endif
bool wrenSaveCompiledModule(WrenVM *vm, ObjModule *module)
{
  bool ret = false;
  
  uint32_t entryFnIndex = (uint32_t)-1;

  Buffer methodNameBuffer;
  Buffer variableBuffer;
  Buffer fnBuffer;
  Buffer fnIndexBuffer;

  BufferInit(&methodNameBuffer);
  BufferInit(&variableBuffer);
  BufferInit(&fnBuffer);
  BufferInit(&fnIndexBuffer);

  if (SaveMoudleVariableToBuffer(vm, module, &variableBuffer)
    && SaveModuleFnToBuffer(vm, module, &fnIndexBuffer, &fnBuffer, &entryFnIndex)
    && entryFnIndex != (uint32_t)-1
    && SaveModuleMethodToBuffer(vm, module, &methodNameBuffer))
  {
    const char *moduleName = MODULE_NAME(module);

    ModuleFileHeader header;
    memset(&header, 0, sizeof(header));

    header.magic        = WREN_FILE_MAGIC;
    header.majorVersion = WREN_BYTECODDE_MAJOR;
    header.minorVersion = WREN_BYTECODDE_MINOR;
    header.entryFnIndex = entryFnIndex;
    strncpy(header.name, moduleName, _countof(header.name) - 1);

    uint32_t offset = sizeof(header);

    header.dataDir[MODULE_DATA_DIR_VARIABLE].offset = offset;
    header.dataDir[MODULE_DATA_DIR_VARIABLE].size   = variableBuffer.length;
    offset += header.dataDir[MODULE_DATA_DIR_VARIABLE].size;

    header.dataDir[MODULE_DATA_DIR_FN_INDEX].offset = offset;
    header.dataDir[MODULE_DATA_DIR_FN_INDEX].size   = fnIndexBuffer.length;
    offset += header.dataDir[MODULE_DATA_DIR_FN_INDEX].size;

    header.dataDir[MODULE_DATA_DIR_FN].offset = offset;
    header.dataDir[MODULE_DATA_DIR_FN].size   = fnBuffer.length;
    offset += header.dataDir[MODULE_DATA_DIR_FN].size;

    header.dataDir[MODULE_DATA_DIR_METHOD].offset = offset;
    header.dataDir[MODULE_DATA_DIR_METHOD].size   = methodNameBuffer.length;
    offset += header.dataDir[MODULE_DATA_DIR_METHOD].size;

    //TODO:
    header.dataDir[MODULE_DATA_DIR_RELOCATION].offset = 0;
    header.dataDir[MODULE_DATA_DIR_RELOCATION].size   = 0;
    offset += header.dataDir[MODULE_DATA_DIR_RELOCATION].size;

    //TODO:
    header.dataDir[MODULE_DATA_DIR_SECURITY].offset = 0;
    header.dataDir[MODULE_DATA_DIR_SECURITY].size   = 0;
    offset += header.dataDir[MODULE_DATA_DIR_SECURITY].size;

    char *fileName = NULL;
    FILE *f        = NULL;

    if ((fileName = GetModuleFileName(module)) != NULL
      && (f = fopen(fileName, "wb")) != NULL
      && fwrite(&header, 1, sizeof(header), f) == sizeof(header)
      && fwrite(variableBuffer.pointer, 1, variableBuffer.length, f) == variableBuffer.length
      && fwrite(fnIndexBuffer.pointer, 1, fnIndexBuffer.length, f) == fnIndexBuffer.length
      && fwrite(fnBuffer.pointer, 1, fnBuffer.length, f) == fnBuffer.length
      && fwrite(methodNameBuffer.pointer, 1, methodNameBuffer.length, f) == methodNameBuffer.length)
    {
      ret = true;
    }

    if (f) fclose(f);
    if (!ret && fileName) remove(fileName);
    if (fileName) free(fileName);
  }

  BufferFree(&methodNameBuffer);
  BufferFree(&variableBuffer);
  BufferFree(&fnBuffer);
  BufferFree(&fnIndexBuffer);

  return ret;
}

bool IsValidModuleHeader(const ModuleFileHeader *header)
{
  if (header->magic != WREN_FILE_MAGIC)
    return false;

  if (!(header->majorVersion > WREN_BYTECODDE_MAJOR
    || (header->majorVersion == WREN_BYTECODDE_MAJOR && header->minorVersion >= WREN_BYTECODDE_MINOR)))
  {
    return false;
  }

  if (!header->dataDir[MODULE_DATA_DIR_FN_INDEX].offset
    || !header->dataDir[MODULE_DATA_DIR_FN_INDEX].size
    || !header->dataDir[MODULE_DATA_DIR_FN].offset
    || !header->dataDir[MODULE_DATA_DIR_FN].size)
  {
    return false;
  }

  return true;
}

//TODO: 此处增加对新的类型的处理。
bool ParseValue(WrenVM *vm, ObjModule *module, uint8_t type,
  const char *data, uint32_t length, Value *value)
{
  bool ret = false;

#if WREN_NAN_TAGGING
  switch (type)
  {
    case FN_CONST_TYPE_NUM:
      if (length == sizeof(double))
      {
        *value = NUM_VAL(*(double *)data);
        ret = true;
      }
      break;

    case FN_CONST_TYPE_STRING:
      *value = wrenNewStringLength(vm, data, length);
      ret = true;
      break;

    case FN_CONST_TYPE_FN:
      {        
        if (length == sizeof(uint32_t))
        {
          uint32_t fnIndex = *((int32_t *)data);
          ObjFn *objFn = GetObjFn(vm, module, fnIndex);
          ASSERT(objFn != NULL, "desired fn not created yet");
          if (objFn)
          {
            *value = OBJ_VAL(objFn);
            ret = true;
          }
        }
      }
      break;

    case FN_CONST_TYPE_NULL:
      if (length == sizeof(Value))
      {
        *value = *(Value *)data;
        ASSERT(*value == NULL_VAL, "unintialized variable must be NULL_VAL");
        ret = true;
      }
      break;

    default:
      ASSERT(false, "add code here to process more types!");
  }
#else
  ASSERT(false, "add code here to process more types!");
#endif

  return ret;
}


/*
  TODO: 
  1、由于每个模块都优先引入了core模块的全部变量，所以要对core模块的变量的下标进行重定位，
  防止core模块的变量有增删改时，下标所指向的变量不是所预期的那个。
*/
bool LoadVariables(Buffer *buffer, WrenVM *vm, ObjModule *module)
{
  if (!buffer->pointer)
    return true;

  bool ret = true;

  do
  {
    uint32_t *nameLength;
    char     *varName;
    
    uint8_t  *type;
    uint32_t *valueLength;
    char     *varValue;

    Value value;
    int   symbol;

    if (BufferConsume(buffer, sizeof(uint32_t), (char **)&nameLength)
      && BufferConsume(buffer, *nameLength, &varName)
      && BufferConsume(buffer, sizeof(uint8_t), (char **)&type)
      && BufferConsume(buffer, sizeof(uint32_t), (char **)&valueLength)
      && BufferConsume(buffer, *valueLength, &varValue)
      && ParseValue(vm, module, *type, varValue, *valueLength, &value)
      )
    {
      symbol = wrenDefineVariable(vm, module, varName, *nameLength, value);
    }

    if (symbol == -2)
    {
      ASSERT(false, "too many variables");
      ret = false;
    }
  } while(ret && !IsBufferExhausted(buffer));

  return ret;
}

ObjFn *GetObjFn(WrenVM *vm, ObjModule *module, uint32_t fnIndex)
{
  uint32_t index = 0;

  for (Obj *obj = vm->first; obj; obj = obj->next)
  {
    if (obj->type != OBJ_FN)
      continue;

    ObjFn *fn = (ObjFn *)obj;
    if (fn->module != module)
      continue;

    if (fnIndex == index)
      return fn;

    index++;
  }

  ASSERT(false, "ObjFn not found!");
  return NULL;
}

static bool RebindMethodNames(WrenVM *vm, ObjModule *module,
  MethodNameRebind *rebindInfo, SymbolTable *methodNames)
{
  if (rebindInfo->nameIndex >= (uint32_t)methodNames->count)
  {
    ASSERT(false, "method name index out of bounds");
    return false;
  }

  ObjFn *objFn = GetObjFn(vm, module, rebindInfo->fnIndex);
  if (!objFn)
  {
    ASSERT(false, "fn not found");
    return false;
  }

  if (rebindInfo->offset + sizeof(uint16_t) > (uint32_t)objFn->code.count)
  {
    ASSERT(false, "opcode + operand out of bounds");
    return false;
  }

  int newIndex = wrenSymbolTableEnsure(vm, &vm->methodNames,
    methodNames->data[rebindInfo->nameIndex]->value,
    methodNames->data[rebindInfo->nameIndex]->length);
  if (newIndex >= 0x10000)
  {
    ASSERT(false, "too many method names, max 65535");
    return false;
  }

  uint8_t instruction = objFn->code.data[rebindInfo->offset - 1];
  ASSERT(instruction == CODE_METHOD_INSTANCE
    || instruction == CODE_METHOD_STATIC
    || (CODE_CALL_0 <= instruction && instruction <= CODE_CALL_16)
    || (CODE_SUPER_0 <= instruction && instruction <= CODE_SUPER_16),
    "unexpected instruction");

  uint8_t *operand = (uint8_t *)&objFn->code.data[rebindInfo->offset];
  *operand++ = (uint8_t)(newIndex >> 8);
  *operand   = (uint8_t)newIndex;

  dbgprint("rebind old index = %u, new index = %u, '%s'\n",
    rebindInfo->nameIndex, newIndex, methodNames->data[rebindInfo->nameIndex]->value);

  return true;
}

/*
  对引用到vm->methodNames表的下标的指令进行重定位。
  
  由于method表是整个vm中的所有module共享的，这个表的增删改都影响到下标相关的指令。

  通过vm->methodNames的x-ref，发现共计有如下指令引用了vm->methodNames表的下标：
    CODE_CALL_0系列
    CODE_SUPER_0系列
    CODE_METHOD_INSTANCE
    CODE_METHOD_STATIC
*/
bool LoadMethods(Buffer *buffer, WrenVM *vm, ObjModule *module)
{
  if (!buffer->pointer)
    return false;

  bool ret = true;

  SymbolTable methodNames;
  wrenSymbolTableInit(&methodNames);

  DataDirectory *dataDir;
  if (BufferConsume(buffer, sizeof(DataDirectory) * 2, (char **)&dataDir)
    && BufferSeek(buffer, dataDir[METHOD_DATA_DIR_NAME].offset))
  {
    BufferSetLimit(buffer, 
      dataDir[METHOD_DATA_DIR_NAME].offset + dataDir[METHOD_DATA_DIR_NAME].size);

    uint32_t *length;
    char     *name;
    while (!IsBufferExhausted(buffer)
      && BufferConsume(buffer, sizeof(uint32_t), (char **)&length)
      && BufferConsume(buffer, *length, (char **)&name))
    {
      int symbol = wrenSymbolTableEnsure(vm, &methodNames, name, *length);
      dbgprint("    method: %s\n", methodNames.data[symbol]->value);
    }

    if (BufferSeek(buffer, dataDir[METHOD_DATA_DIR_REBIND].offset))
    {
      BufferSetLimit(buffer,
        dataDir[METHOD_DATA_DIR_REBIND].offset + dataDir[METHOD_DATA_DIR_REBIND].size);

      MethodNameRebind *rebindInfo;
      while (!IsBufferExhausted(buffer)
        && BufferConsume(buffer, sizeof(MethodNameRebind), (char **)&rebindInfo))
      {
        if (!RebindMethodNames(vm, module, rebindInfo, &methodNames))
        {
          ret = false;
          break;
        }
      }
    }
  }

  wrenSymbolTableClear(vm, &methodNames);

  return ret;
}

bool LoadConstants(Buffer *buffer, WrenVM *vm, ObjModule *module, ObjFn *fn)
{
  if (!buffer->pointer || !buffer->length)
    return true;

  bool ret = true;

  do
  {
    uint8_t  *type;
    uint32_t *length;
    char     *data;
    Value     value;

    if (BufferConsume(buffer, sizeof(uint8_t), (char **)&type)
      && BufferConsume(buffer, sizeof(uint32_t), (char **)&length)
      && BufferConsume(buffer, *length, (char **)&data)
      && ParseValue(vm, module, *type, data, *length, &value)
      )
    {
      if (IS_OBJ(value)) wrenPushRoot(vm, AS_OBJ(value));

      wrenValueBufferWrite(vm, &fn->constants, value);
      
      if (IS_OBJ(value)) wrenPopRoot(vm);
    }
    else
    {
      ASSERT(false, "bad constant data");
      ret = false;
    }
  } while (ret && !IsBufferExhausted(buffer));

  return ret;  
}

bool LoadFNs(Buffer *indexBuffer, Buffer *buffer,
  WrenVM *vm, ObjModule *module)
{
  if (!indexBuffer->pointer || !buffer->pointer
    || !indexBuffer->length || !buffer->length)
  {
    return false;
  }

  bool ret = true;

  uint32_t fnIndex = 0;

  //create all ObjFn objects first.
  do
  {
    DataDirectory *dataDir;
    FnHeader      *header;

    if (BufferConsume(indexBuffer, sizeof(DataDirectory), (char **)&dataDir)
      && BufferSeek(buffer, dataDir->offset)
      && BufferConsume(buffer, sizeof(FnHeader), (char **)&header)
      )
    {
      ObjFn *fn = wrenNewFunction(vm, module, header->maxSlots);
      if (!fn)
      {
        ASSERT(false, "unable to create fn");
        ret = false;
        break;
      }

      dbgprint("    created fn %p, index = %u, code count = %u\n",
        fn, fnIndex, header->dataDir[FN_DATA_DIR_CODE].size);

      fnIndex++;      
    }
  } while (ret && !IsBufferExhausted(indexBuffer));

  if (!ret)
    return false;

  BufferRewind(indexBuffer);
  BufferRewind(buffer);

  fnIndex = 0;

  do
  {
    DataDirectory *dataDir;
    FnHeader      *header;
    uint8_t       *code;
    char          *constants;
    char          *debug;
    ObjFn         *fn;
    uint32_t      *fnNameLen;
    char          *fnName;

    if (BufferConsume(indexBuffer, sizeof(DataDirectory), (char **)&dataDir)

      && BufferSeek(buffer, dataDir->offset)
      && BufferConsume(buffer, sizeof(FnHeader), (char **)&header)

      && BufferSeek(buffer, dataDir->offset + header->dataDir[FN_DATA_DIR_CODE].offset)
      && BufferConsume(buffer, header->dataDir[FN_DATA_DIR_CODE].size, (char **)&code)

      && BufferSeek(buffer, dataDir->offset + header->dataDir[FN_DATA_DIR_CONSTANTS].offset)
      && BufferConsume(buffer, header->dataDir[FN_DATA_DIR_CONSTANTS].size, (char **)&constants)

      && BufferSeek(buffer, dataDir->offset + header->dataDir[FN_DATA_DIR_DEBUG].offset)
      && BufferConsume(buffer, sizeof(uint32_t), (char **)&fnNameLen)
      && BufferConsume(buffer, *fnNameLen, (char **)&fnName)
      && BufferConsume(buffer, header->dataDir[FN_DATA_DIR_DEBUG].size, (char **)&debug)

      && (fn = GetObjFn(vm, module, fnIndex)) != NULL
      )
    {
      //wrenPushRoot(vm, (Obj *)fn);
      fn->arity       = header->arity;
      fn->numUpvalues = header->numUpvalues;
      fn->abi.id      = header->id;

      if (header->dataDir[FN_DATA_DIR_CONSTANTS].size)
      {
        Buffer constantBuffer;
        BufferSet(&constantBuffer, constants, header->dataDir[FN_DATA_DIR_CONSTANTS].size);
        ret = LoadConstants(&constantBuffer, vm, module, fn);
      }

      if (header->dataDir[FN_DATA_DIR_CODE].size)
      {
        wrenByteBufferAppend(vm, &fn->code, code, header->dataDir[FN_DATA_DIR_CODE].size);
      }
      
      if (*fnNameLen)
        wrenFunctionBindName(vm, fn, fnName, (int)(*fnNameLen));

      if (header->dataDir[FN_DATA_DIR_DEBUG].size)
      {
        wrenIntBufferAppend(vm, &fn->debug->sourceLines, (int *)debug,
          header->dataDir[FN_DATA_DIR_DEBUG].size / sizeof(int));
      }

      dbgprint("fn = %u (%s), code count = %u\n", fnIndex,
        fn->debug->name ? fn->debug->name : "noname",
        fn->code.count);

      //wrenPopRoot(vm);

      fnIndex++;
    }
    else
    {
      ASSERT(false, "unexpected fn data");
      ret = false;
    }
  } while (ret && !IsBufferExhausted(indexBuffer));

  return ret;
}

#ifdef __cplusplus
extern "C"
#endif
bool wrenLoadCompiledModule(WrenVM      *vm,
                            const char  *moduleName,
                            bool         runClosure,
                            ObjClosure **objClosure)
{
  char *fileName = NULL;
  FILE *f        = NULL;
  char *bytecode = NULL;
  int   fileLen;
  
  bool ret = false;

  if ((fileName = GetModuleFilePath(moduleName)) != NULL
    && (f = fopen(fileName, "rb")) != NULL
    && fseek(f, 0, SEEK_END) == 0
    && (fileLen = ftell(f)) > 0
    && (bytecode = (char *)malloc(fileLen)) != NULL
    && fseek(f, 0, SEEK_SET) == 0
    && fread(bytecode, 1, fileLen, f) == fileLen)
  {
    ret = wrenLoadCompiledModuleFromBuffer(vm, moduleName, (const uint8_t *)bytecode, fileLen, true, objClosure);
  }
  
  if (bytecode) free(bytecode);
  if (f)        fclose(f);
  if (fileName) free(fileName);  

  return ret;
}

//TODO: deal with GC.
bool wrenLoadCompiledModuleFromBuffer(WrenVM        *vm,
                                      const char    *moduleName,
                                      const uint8_t *bytecode,
                                      const uint32_t bytecodeLen,
                                      bool           runClosure,
                                      ObjClosure   **objClosure)
{
  Value name;

  if (_stricmp(moduleName, CORE_MODULE_NAME))
    name = wrenNewString(vm, moduleName);
  else
    name = NULL_VAL;

  ObjModule* module = getModule(vm, name);
  if (module && name != NULL_VAL)
    return true;

  if (name != NULL_VAL)
  {
    wrenPushRoot(vm, AS_OBJ(name));
    module = wrenNewModule(vm, AS_STRING(name));
    wrenPushRoot(vm, (Obj *)module);

    // Implicitly import the core module.
    ObjModule* coreModule = getModule(vm, NULL_VAL);
    for (int i = 0; i < coreModule->variables.count; i++)
    {
      wrenDefineVariable(vm, module,
        coreModule->variableNames.data[i]->value,
        coreModule->variableNames.data[i]->length,
        coreModule->variables.data[i]);
    }
  }

  bool ret = false;

  Buffer bytecodeBuffer;
  Buffer methodNameBuffer;
  Buffer variableBuffer;
  Buffer fnBuffer;
  Buffer fnIndexBuffer;
  
  BufferSet(&bytecodeBuffer, (char *)bytecode, bytecodeLen);
  
  ModuleFileHeader *header;
  char             *data;

  if (BufferConsume(&bytecodeBuffer, sizeof(header), (char **)&header)
    && IsValidModuleHeader(header)

    && BufferSeek(&bytecodeBuffer, header->dataDir[MODULE_DATA_DIR_VARIABLE].offset)
    && BufferConsume(&bytecodeBuffer, header->dataDir[MODULE_DATA_DIR_VARIABLE].size, (char **)&data)
    && BufferSet(&variableBuffer, data, header->dataDir[MODULE_DATA_DIR_VARIABLE].size)

    && BufferSeek(&bytecodeBuffer, header->dataDir[MODULE_DATA_DIR_FN_INDEX].offset)
    && BufferConsume(&bytecodeBuffer, header->dataDir[MODULE_DATA_DIR_FN_INDEX].size, (char **)&data)
    && BufferSet(&fnIndexBuffer, data, header->dataDir[MODULE_DATA_DIR_FN_INDEX].size)

    && BufferSeek(&bytecodeBuffer, header->dataDir[MODULE_DATA_DIR_FN].offset)
    && BufferConsume(&bytecodeBuffer, header->dataDir[MODULE_DATA_DIR_FN].size, (char **)&data)
    && BufferSet(&fnBuffer, data, header->dataDir[MODULE_DATA_DIR_FN].size)

    && BufferSeek(&bytecodeBuffer, header->dataDir[MODULE_DATA_DIR_METHOD].offset)
    && BufferConsume(&bytecodeBuffer, header->dataDir[MODULE_DATA_DIR_METHOD].size, (char **)&data)
    && BufferSet(&methodNameBuffer, data, header->dataDir[MODULE_DATA_DIR_METHOD].size)
    )
  {
    ret = LoadVariables(&variableBuffer, vm, module)
      && LoadFNs(&fnIndexBuffer, &fnBuffer, vm, module)
      && LoadMethods(&methodNameBuffer, vm, module);
  }

  if (ret && name != NULL_VAL)
  {
    // Store it in the VM's module registry so we don't load the same module
    // multiple times.
    wrenMapSet(vm, vm->modules, name, OBJ_VAL(module));
  }

  if (name != NULL_VAL)
  {
    wrenPopRoot(vm); //module
    wrenPopRoot(vm); //name
  }

  if (ret)
  {
    ObjClosure *closure;
    ObjFn      *entryFn;
    if (!(entryFn = GetObjFn(vm, module, header->entryFnIndex))
      || !(closure = wrenNewClosure(vm, entryFn))
      )
    {
      return false;
    }

    if (runClosure)
    {
      wrenPushRoot(vm, (Obj *)closure);
      ObjFiber* fiber = wrenNewFiber(vm, closure);
      wrenPopRoot(vm);

      if (!fiber)
        return false;

      return runInterpreter(vm, fiber) == WREN_RESULT_SUCCESS;
    }
    else
    {
      *objClosure = closure;
      return true;
    }
  }

  return ret;

}

#ifdef __cplusplus
extern "C"
#endif
bool wrenGenerateABI(WrenVM *vm, ObjModule *module)
{
  cJSON *jsonRoot = cJSON_CreateArray();
  if (!jsonRoot)
    return false;

  bool ret = true;

  for (Obj *obj = vm->first; obj; obj = obj->next)
  {
    if (obj->type != OBJ_FN)
      continue;

    ObjFn *fn = (ObjFn *)obj;
    MethodAbiInfo *abi = &fn->abi;
    if (!fn->module->name
      || fn->module->isBuiltIn
      || strcmp(fn->module->name->value, module->name->value)
      || abi->isForeign                 //TODO: foreign method of class
      || !abi->isMethod)
    {
      continue;
    }

    cJSON *jsonMethod = cJSON_CreateObject();
    if (!jsonMethod)
    {
      ret = false;
      break;
    }

    cJSON_AddItemToArray(jsonRoot, jsonMethod);

    if (!cJSON_AddNumberToObject(jsonMethod, "selector", abi->id))
    {
      ret = false;
      break;
    }

    if (!cJSON_AddStringToObject(jsonMethod, "sig", abi->signature))
    {
      ret = false;
      break;
    }

    if (!cJSON_AddStringToObject(jsonMethod, "class", abi->className))
    {
      ret = false;
      break;
    }

    if (!cJSON_AddBoolToObject(jsonMethod, "ctor", abi->isConstructor))
    {
      ret = false;
      break;
    }

    if (!cJSON_AddBoolToObject(jsonMethod, "static", abi->isStatic))
    {
      ret = false;
      break;
    }

    if (!cJSON_AddNumberToObject(jsonMethod, "input", abi->methodArity))
    {
      ret = false;
      break;
    }

    if (!cJSON_AddNumberToObject(jsonMethod, "output", 1))
    {
      ret = false;
      break;
    }
  }

  if (ret)
  {
    char *jsonString = cJSON_PrintUnformatted(jsonRoot);
    if (jsonString)
    {
      printf("%s\n", jsonString);
      free(jsonString);
    }
  }

  cJSON_Delete(jsonRoot);

  return ret;
}

struct AbiInfo
{
    vector<MethodAbiInfo *> abis;

    ~AbiInfo()
    {
        for (auto & abi : abis)
        {
            delete abi;
        }
    }
};

bool wrenParseAbiString(const char *abiJson, unique_ptr<AbiInfo> &abiInfo)
{
    bool ret = true;

    cJSON *jsonRoot = cJSON_Parse(abiJson);
    if (!jsonRoot)
        return false;

    std::unique_ptr<cJSON, decltype(&cJsonDeleter)> jsonNode(jsonRoot, &cJsonDeleter);

    int count = cJSON_GetArraySize(jsonRoot);
    if (count <= 0)
        return false;

    for (int i = 0; i < count; i++)
    {
        MethodAbiInfo *abi = new MethodAbiInfo;
        if (!abi)
        {
            ret = false;
            break;
        }

        abiInfo->abis.push_back(abi);

        cJSON * subitem = cJSON_GetArrayItem(jsonRoot, i);

        cJSON *jsonObj;
        jsonObj = cJSON_GetObjectItem(subitem, "selector");
        if (!jsonObj || jsonObj->type != cJSON_Number)
        {
            ret = false;
            break;
        }
        abi->id = (uint32_t)jsonObj->valuedouble;

        jsonObj = cJSON_GetObjectItem(subitem, "sig");
        if (!jsonObj || jsonObj->type != cJSON_String)
        {
            ret = false;
            break;
        }
        memset(abi->signature, 0, sizeof(abi->signature));
        memcpy(abi->signature, jsonObj->valuestring, std::min(strlen(jsonObj->valuestring), sizeof(abi->signature) - 1));

        jsonObj = cJSON_GetObjectItem(subitem, "class");
        if (!jsonObj || jsonObj->type != cJSON_String)
        {
            ret = false;
            break;
        }
        memset(abi->className, 0, sizeof(abi->className));
        memcpy(abi->className, jsonObj->valuestring, std::min(strlen(jsonObj->valuestring), sizeof(abi->className) - 1));


        jsonObj = cJSON_GetObjectItem(subitem, "ctor");
        if (!jsonObj || (jsonObj->type != cJSON_True && jsonObj->type != cJSON_False))
        {
            ret = false;
            break;
        }
        abi->isConstructor = (jsonObj->type == cJSON_True);

        jsonObj = cJSON_GetObjectItem(subitem, "static");
        if (!jsonObj || (jsonObj->type != cJSON_True && jsonObj->type != cJSON_False))
        {
            ret = false;
            break;
        }
        abi->isStatic = (jsonObj->type == cJSON_True);

        jsonObj = cJSON_GetObjectItem(subitem, "input");
        if (!jsonObj || jsonObj->type != cJSON_Number)
        {
            ret = false;
            break;
        }
        abi->methodArity = jsonObj->valueint;

        jsonObj = cJSON_GetObjectItem(subitem, "output");
        if (!jsonObj || jsonObj->type != cJSON_Number)
        {
            ret = false;
            break;
        }
    }
  
  return ret;
}

typedef enum ArgType
{
  ArgType_Integer,
  ArgType_String,
  ArgType_Bool,
} ArgType;

typedef struct ArgInfo
{
    ArgType type;

    int64_t     valueInt64;
    string      valueString;
    uint8_t     valueBool;
} ArgInfo;

typedef struct CallInfo
{
    uint32_t            methodId;
    vector<ArgInfo *>   args;

    ~CallInfo()
    {
        for (auto &arg : args)
        {
            delete arg;
        }
    }
} CallInfo;

inline bool hexCharToBin(char ch, uint8_t *bin)
{
  if (ch >= '0' && ch <= '9')
  {
    *bin = ch - '0';
    return true;
  }
  else if (ch >= 'a' && ch <= 'f')
  {
    *bin = ch - 'a' + 0xa;
    return true;
  }
  else if (ch >= 'A' && ch <= 'F')
  {
    *bin = ch - 'A' + 0xA;
    return true;
  }

  return false;
}

bool hexStringToBin(const char *callData, vector<uint8_t> &binData)
{
    size_t len = strlen(callData);
    if (len == 0 || (len & 1))
    return false;

    bool ret = true;

    for (size_t k = 0; k < len; k += 2)
    {
        uint8_t high;

        if (!hexCharToBin(callData[k], &high))
        {
            ret = false;
            break;
        }    

        uint8_t low;
        if (!hexCharToBin(callData[k + 1], &low))
        {
            ret = false;
            break;
        }

        binData.push_back((high << 4) | low);
    }

    return ret;
}

bool wrenParseCallData(const char *callData, std::unique_ptr<CallInfo> &callInfo)
{
    bool ret = true;

    vector<uint8_t> binData;
    if (!hexStringToBin(callData, binData))
        return false;

    callInfo = std::make_unique<CallInfo>();
    if (!callInfo)
        return false;

    Buffer buffer;
    BufferSet(&buffer, (char *)&binData[0], binData.size());

    uint32_t *methodId;
    if (!BufferConsume(&buffer, sizeof(uint32_t), (char **)&methodId))
        return false;

    callInfo->methodId = *methodId;

    while (ret)
    {
        if (IsBufferExhausted(&buffer))
            break;

        uint8_t  *type;
        uint16_t *length;
        uint8_t  *value;

        if (!BufferConsume(&buffer, sizeof(uint8_t), (char **)&type))
        {
            ret = false;
            break;
        }

        if (!BufferConsume(&buffer, sizeof(uint16_t), (char **)&length))
        {
            ret = false;
            break;
        }

        if (!BufferConsume(&buffer, *length, (char **)&value))
        {
            ret = false;
            break;
        }

        ArgInfo *newArg = new ArgInfo;
        if (!newArg)
        {
            ret = false;
            break;
        }

        callInfo->args.push_back(newArg);
        ArgInfo *currentArg = callInfo->args[callInfo->args.size() - 1];

        switch (*type)
        {
        case FN_CONST_TYPE_FALSE:
            ASSERT(*length == 1, "bad false length");
            ASSERT(*value == 0, "bad false value");
            currentArg->type      = ArgType_Bool;
            currentArg->valueBool = 0;
            break;

        case FN_CONST_TYPE_TRUE:
            ASSERT(*length == 1, "bad true length");
            ASSERT(*value == 1, "bad true value");
            currentArg->type      = ArgType_Bool;
            currentArg->valueBool = 1;
            break;

        case FN_CONST_TYPE_NUM:
            if (*length != sizeof(int64_t))
            {
            ret = false;
            break;
            }
            currentArg->type = ArgType_Integer;
            currentArg->valueInt64 = *((int64_t *)value);
            break;

        case FN_CONST_TYPE_STRING:
            currentArg->type = ArgType_String;
            currentArg->valueString = string((char *)value, *length);
            break;

        default:
            ASSERT(false, "not supported type yet");
            ret = false;
            break;
        }
    }

    return ret;
}

ObjClass * wrenGetClassObj(WrenVM *vm, const char *className)
{
  for (Obj *obj = vm->first; obj; obj = obj->next)
  {
    if (obj->type != OBJ_CLASS)
      continue;

    ObjClass *classObj = (ObjClass *)obj;
    if (!_stricmp(className, classObj->name->value))
      return classObj;
  }

  ASSERT(false, "ObjClass not found!");
  return NULL;
}

ObjFn * wrenGetMethodFn(WrenVM *vm, uint32_t methodId)
{
  for (Obj *obj = vm->first; obj; obj = obj->next)
  {
    if (obj->type != OBJ_FN)
      continue;

    ObjFn *fn = (ObjFn *)obj;
    if (fn->abi.id == methodId)
      return fn;
  }

  ASSERT(false, "ObjFn not found!");
  return NULL;
}

const MethodAbiInfo *wrenGetMethodAbi(uint32_t methodId, const std::unique_ptr<AbiInfo>  &abiInfo)
{
    for (size_t k = 0; k < abiInfo->abis.size(); k++)
    {
        if (methodId == abiInfo->abis[k]->id)
            return abiInfo->abis[k];
    }

    ASSERT(false, "method abi not found!");
    return NULL;
}

int wrenGetMethodSymbol(WrenVM *vm, const char *methodName)
{
  int existing = wrenSymbolTableFind(&vm->methodNames, methodName, strlen(methodName));
  ASSERT(existing != -1, "method name not loaded");
  return existing;
}

void emitByte(WrenVM *vm, ObjFn *fn, uint8_t byteValue)
{
  wrenByteBufferAppend(vm, &fn->code, &byteValue, 1);
}

void emitShort(WrenVM *vm, ObjFn *fn, uint16_t shortValue)
{
  uint8_t *byteValue = (uint8_t *)&shortValue;
  wrenByteBufferAppend(vm, &fn->code, &byteValue[1], 1);
  wrenByteBufferAppend(vm, &fn->code, &byteValue[0], 1);
}

bool LoadFieldsFromBuffer(WrenVM *vm, ObjModule *module,
                          ObjInstance *instance, const vector<uint8_t> &decodedOldState)
{
    if (instance->obj.classObj->numFields == 0)
        return true;

    if (decodedOldState.empty())
        return instance->obj.classObj->numFields == 0;

    Buffer buffer;
    BufferSet(&buffer, (char *)&decodedOldState[0], decodedOldState.size());

    bool ret = true;

    int numFields = 0;

    do
    {
        uint8_t  *type;
        uint32_t *length;
        char     *data;
        Value     value;

        if (BufferConsume(&buffer, sizeof(uint8_t), (char **)&type)
            && BufferConsume(&buffer, sizeof(uint32_t), (char **)&length)
            && BufferConsume(&buffer, *length, (char **)&data)
            && ParseValue(vm, module, *type, data, *length, &value)
            && numFields < instance->obj.classObj->numFields)
        {            
            if (IS_OBJ(value)) wrenPushRoot(vm, AS_OBJ(value));
            instance->fields[numFields] = value;
            if (IS_OBJ(value)) wrenPopRoot(vm);

            numFields++;
        }
        else
        {
            ASSERT(false, "bad fields data");
            ret = false;
        }
    } while (ret && !IsBufferExhausted(&buffer));

    return ret && (numFields == instance->obj.classObj->numFields);
}

bool SaveFieldsToBuffer(WrenVM *vm, ObjModule *module,
                        const ObjInstance *instance, vector<uint8_t> &newState)
{
    Buffer buffer;
    BufferInit(&buffer);

    int numFields = instance->obj.classObj->numFields;
    for (int k = 0; k < numFields; k++)
    {
        if (!SaveValueToBuffer(vm, module, instance->fields[k], &buffer))
            return false;
    }

    if (buffer.length)
    {
        newState.resize(buffer.length);
        if (newState.size() != buffer.length)
            return false;

        memcpy(&newState[0], buffer.pointer, buffer.length);
    }

    return true;
}

bool wrenCallMethod(WrenVM                *vm,
                    const vector<uint8_t> &decodedBytecode,
                    bool                   doConstruct,
                    const string          &callData,
                    const string          &abiJson,
                    const string          &contractHash,
                    const vector<uint8_t> &decodedOldState,
                    vector<uint8_t>       &newState)
{
    bool ret = true;

    std::unique_ptr<AbiInfo> abiInfo(std::make_unique<AbiInfo>());
    if (!wrenParseAbiString(abiJson.c_str(), abiInfo))
        return false;

    std::unique_ptr<CallInfo> callInfo(std::make_unique<CallInfo>());
    if (!wrenParseCallData(callData.c_str(), callInfo))
        return false;

    //use smart contract hash string as Wren module name.
    const char *moduleName = contractHash.c_str();

    ObjClosure *moduleClosure;
    if (!wrenLoadCompiledModuleFromBuffer(vm, moduleName, (const uint8_t *)&decodedBytecode[0],
                                            decodedBytecode.size(), true, &moduleClosure))
    {
        return false;
    }

    ObjFn *calleeFn = wrenGetMethodFn(vm, callInfo->methodId);
    if (!calleeFn)
        return false;

    const MethodAbiInfo *methodAbi = wrenGetMethodAbi(callInfo->methodId, abiInfo);
    if (!methodAbi)
        return false;

    //TODO: deal with variadic args
    if (methodAbi->methodArity != (uint8_t)callInfo->args.size())
        return false;

    if ((doConstruct && !methodAbi->isConstructor)
        || (!doConstruct && methodAbi->isConstructor))
    {
        return false;
    }

    ObjClass *classObj = wrenGetClassObj(vm, methodAbi->className);
    if (!classObj)
        return false;

    Value name = wrenNewString(vm, moduleName);
    ObjModule* module = getModule(vm, name);
    if (!module)
        return false;

    ObjFn *callerFn  = wrenNewFunction(vm, module, 8);

    static const char fnName[] = "fn_smart_contract_call_thunk";

    {
        gcHelper callerFnHelper(vm, (Obj *)callerFn);

        callerFn->debug = ALLOCATE(vm, FnDebug);
        callerFn->debug->name = ALLOCATE_ARRAY(vm, char, sizeof(fnName));
        memcpy(callerFn->debug->name, fnName, sizeof(fnName));
        wrenIntBufferInit(&callerFn->debug->sourceLines);
    }

    ObjClosure *callerClosure = wrenNewClosure(vm, callerFn);

    ObjFiber* fiber;
    {
        gcHelper callerClosureHelper(vm, (Obj *)callerClosure);
        fiber = wrenNewFiber(vm, callerClosure);
    }
    
    gcHelper fiberHelper(vm, (Obj *)fiber);

    int methodSymbol = wrenGetMethodSymbol(vm, methodAbi->signature);
    if (methodSymbol == -1)
        return false;

    #define STACK_COUNT(f) ((f)->stackTop - (f)->stack)

    #define PUSH(value) if (STACK_COUNT(fiber) >= fiber->stackCapacity) \
      { \
        wrenEnsureStack(vm, fiber, fiber->stackCapacity + 1); \
      } \
      *fiber->stackTop++ = value

    Value    instance;
    gcHelper instanceHelper;

    /*
      1. for constructor call, save member variables to chain database after construction.
      2. for non-constructor call, load member variables from chain database first,
         save member variables to chain database after call.
    */
    if (methodAbi->isStatic || methodAbi->isConstructor)
    {
        PUSH(wrenObjectToValue((Obj *)classObj));
    }
    else
    {
        //assume there is only one instance of smart contract class, i.e singleton.
        instance = wrenNewInstance(vm, classObj);
        instanceHelper.attach(vm, (Obj *)AS_INSTANCE(instance));

        if (!methodAbi->isConstructor)
        {
            ret = LoadFieldsFromBuffer(vm, module, AS_INSTANCE(instance), decodedOldState);
            if (!ret)
                return false;
        }

        PUSH(instance);
    }

    if (!ret)
        return false;

    //generate code to call specified member function.
    emitByte(vm, callerFn, CODE_CALL_0 + callInfo->args.size());
    emitShort(vm, callerFn, methodSymbol);
    emitByte(vm, callerFn, CODE_RETURN);

    wrenIntBufferFill(vm, &callerFn->debug->sourceLines, 0, 4);
    
    for (uint32_t k = 0; k < callInfo->args.size() && ret; k++)
    {
        ArgInfo *arg = callInfo->args[k];
        switch (arg->type)
        {
        case ArgType_Bool:
            if (arg->valueBool)
            {
                PUSH(TRUE_VAL);
            }
            else
            {
                PUSH(FALSE_VAL);
            }
            break;

        case ArgType_Integer:
            PUSH(wrenNumToValue((double)arg->valueInt64));
            break;

        case ArgType_String:
            PUSH(wrenNewString(vm, arg->valueString.c_str()));
            break;

        default:
            ret = false;
            break;
        }
    }

    fiber->frames[fiber->numFrames - 1].ip = callerClosure->fn->code.data;
    WrenInterpretResult result = runInterpreter(vm, fiber);    
    if (result != WREN_RESULT_SUCCESS)
        return false;

    if (methodAbi->isConstructor)
    {
        ObjInstance *constructedInstance = AS_INSTANCE(*fiber->stackTop);
        gcHelper helper(vm, (Obj *)constructedInstance);
        ret = SaveFieldsToBuffer(vm, module, constructedInstance, newState);
    }
    else if (!methodAbi->isStatic)
    {
        ret = SaveFieldsToBuffer(vm, module, AS_INSTANCE(instance), newState);
    }

    return ret;
}
