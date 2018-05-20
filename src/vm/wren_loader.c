#include "wren_loader.h"

#include <stdio.h>
#include "wren_value.h"
#include "wren_debug.h"

#define WREN_FILE_MAGIC       'NERW'
#define WREN_BYTECODDE_MAJOR  1
#define WREN_BYTECODDE_MINOR  0

#ifndef _countof
#define _countof(x)    (sizeof(x) / sizeof((x)[0]))
#endif

#if defined(_DEBUG) || defined(DEBUG)
#define dbgprint printf
#else
#define dbgprint
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
  uint32_t nameIndex;
  uint32_t maxSlots;
  uint32_t numUpvalues;
  uint32_t arity;

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
  FN_CONST_TYPE_UPVALUE

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
} Buffer;

extern const char *rootDirectory;

bool SaveValueToBuffer(WrenVM *vm, ObjModule *module,
  Value value, Buffer *buffer);
uint32_t GetFnIndex(WrenVM *vm, ObjModule *module, ObjFn *fnTarget);

void BufferInit(Buffer *buffer)
{
  buffer->pointer  = NULL;
  buffer->length   = 0;
  buffer->capacity = 0;
  buffer->consumed = 0;
}

bool BufferInitSize(Buffer *buffer, uint32_t size)
{
  if (size)
  {
    buffer->pointer = malloc(size);
    if (!buffer->pointer)
      return false;

    buffer->capacity = size;
  }
  else
  {
    buffer->pointer  = NULL;
    buffer->capacity = 0;
  }

  buffer->length   = 0;
  buffer->consumed = 0;

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

  return true;
}

bool BufferConsume(Buffer *buffer, uint32_t lenDesired, char **data)
{
  if (!lenDesired)
  {
    *data = buffer->pointer + buffer->consumed;
    return true;
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

  return false;
}

void BufferSet(Buffer *buffer, char *data, uint32_t length)
{
  buffer->pointer  = data;
  buffer->length   = length;
  buffer->capacity = length;
  buffer->consumed = 0;
}

bool IsBufferExhausted(const Buffer *buffer)
{
  return buffer->consumed == buffer->length;
}

void BufferFree(Buffer *buffer)
{
  if (buffer->pointer)
  {
    free(buffer->pointer);

    buffer->pointer  = NULL;
    buffer->length   = 0;
    buffer->capacity = 0;
  }
}

void wrenSaverInit(void)
{

}

static bool DumpMethodNames(WrenVM *vm)
{
  for (int i = 0; i < vm->methodNames.count; i++)
  {
    printf("    methodNames %d: %s\n", i, vm->methodNames.data[i]->value);
  }

  printf("\n");

  return true;
}

static bool DumpModules(WrenVM *vm)
{
  uint32_t actualCount = 0;

  for (uint32_t i = 0; i < vm->modules->capacity; i++)
  {
    if (IS_UNDEFINED(vm->modules->entries[i].key)
      || IS_UNDEFINED(vm->modules->entries[i].value))
    {
      continue;
    }

    actualCount++;
    ASSERT(actualCount <= vm->modules->count, "too many modules");

    ObjModule *module = AS_MODULE(vm->modules->entries[i].value);

    printf("module: %s\n", MODULE_NAME(module));

    for (int k = 0; k < module->variableNames.count; k++)
    {
      printf("    variableNames %d: %s\n", k, module->variableNames.data[k]->value);
    }
    printf("\n");

  }

  ASSERT(actualCount == vm->modules->count, "too many or too few modules");

  return true;
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

  Buffer nameBuffer;
  Buffer rebindBuffer;

  BufferInit(&nameBuffer);
  BufferInit(&rebindBuffer);

  SymbolTable methodNames;
  wrenSymbolTableInit(&methodNames);

  UserData *userData = (UserData *)vm->config.userData;
  for(MethodNameInfo *info = userData->methodNameInfo; info; info = info->next)
  {
    ObjFn *objFn = info->objFn;
    if (objFn->module != module)
      continue;

    uint16_t operand = *((uint16_t *)&objFn->code.data[info->offset]);

    const char *data       = vm->methodNames.data[operand]->value;
    uint32_t    dataLength = vm->methodNames.data[operand]->length;

    dbgprint("    method: %s\n", data);

    int index = wrenSymbolTableEnsure(vm, &methodNames, data, dataLength);

    MethodNameRebind rebindInfo;
    rebindInfo.fnIndex   = GetFnIndex(vm, module, objFn);
    if (rebindInfo.fnIndex == (uint32_t)-1)
    {
      ret = false;
      break;
    }

    rebindInfo.offset    = info->offset;
    rebindInfo.nameIndex = index;
    
    if (!BufferAppend(&rebindBuffer, (const char *)&rebindInfo, sizeof(rebindInfo)))
    {
      ret = false;
      break;
    }
  }

  if (ret && SaveMethodNamesToBuffer(vm, module, &methodNames, &nameBuffer))
  {
    DataDirectory dataDir[2];

    uint32_t offset = sizeof(dataDir);

    dataDir[METHOD_DATA_DIR_NAME].offset = offset;
    dataDir[METHOD_DATA_DIR_NAME].size = nameBuffer.length;
    offset += dataDir[METHOD_DATA_DIR_NAME].size;

    dataDir[METHOD_DATA_DIR_REBIND].offset = offset;
    dataDir[METHOD_DATA_DIR_REBIND].size = rebindBuffer.length;

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
  
  wrenDumpValue(value);
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
      break;
    case FALSE_VAL:
      break;
    case TRUE_VAL:
      break;
    case UNDEFINED_VAL:
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
    //TODO: header.nameIndex

    uint32_t offset = sizeof(header);

    header.dataDir[FN_DATA_DIR_CODE].size   = fn->code.count;
    header.dataDir[FN_DATA_DIR_CODE].offset = offset;
    offset += header.dataDir[FN_DATA_DIR_CODE].size;

    header.dataDir[FN_DATA_DIR_CONSTANTS].size   = constantBuffer.length;
    header.dataDir[FN_DATA_DIR_CONSTANTS].offset = offset;
    offset += header.dataDir[FN_DATA_DIR_CONSTANTS].size;

    header.dataDir[FN_DATA_DIR_DEBUG].size   = fn->debug->sourceLines.count;
    header.dataDir[FN_DATA_DIR_DEBUG].offset = offset;

    uint32_t oldOffset = buffer->length;

    if (!BufferAppend(buffer, (const char *)&header, sizeof(header))
      || !BufferAppend(buffer, fn->code.data, fn->code.count)
      || !BufferAppend(buffer, constantBuffer.pointer, constantBuffer.length)
      || !BufferAppend(buffer, (const char *)fn->debug->sourceLines.data, 
          fn->debug->sourceLines.count * sizeof(int)))
    {
      ret = false;
      BufferFree(&constantBuffer);
      break;
    }

    BufferFree(&constantBuffer);

    DataDirectory dataDir;
    dataDir.offset = oldOffset;     //offset relative to beginning of dataDir[MODULE_DATA_DIR_FN];
    dataDir.size   = buffer->length - oldOffset;
    if (!BufferAppend(indexBuffer, (const char *)&dataDir, sizeof(dataDir)))
    {
      ret = false;
      break;
    }

    if (!fn->debug->name || !strcmp(fn->debug->name, "(script)"))
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

  char *name = NULL;

  if (rootDirectory)
  {
    name = (char *)malloc(strlen(rootDirectory) + strlen(moduleName) + sizeof(suffix));
    if (name)
    {
      strcpy(name, rootDirectory);
      strcat(name, moduleName);
      strcat(name, suffix);
    }
  }
  else
  {
    name = (char *)malloc(strlen(moduleName) + sizeof(suffix));
    if (name)
    {
      strcpy(name, moduleName);
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

void wrenLoaderInit(void)
{

}

bool IsValid(const ModuleFileHeader *header, long int fileSize)
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
      ASSERT(false, "add code here to process more types!");
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
    return false;

  ObjFn *objFn = GetObjFn(vm, module, rebindInfo->fnIndex);
  if (!objFn)
    return false;

  if (rebindInfo->offset + sizeof(uint16_t) > (uint32_t)objFn->code.count)
    return false;

  int newIndex = wrenSymbolTableEnsure(vm, &vm->methodNames,
    methodNames->data[rebindInfo->nameIndex]->value,
    methodNames->data[rebindInfo->nameIndex]->length);
  if (newIndex >= 0x10000)
  {
    ASSERT(false, "too many method names");
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
    uint32_t *length;
    char     *name;
    while (BufferConsume(buffer, sizeof(uint32_t), (char **)&length)
      && !BufferConsume(buffer, *length, (char **)&name))
    {
      wrenSymbolTableEnsure(vm, &methodNames, name, *length);
    }

    if (BufferSeek(buffer, dataDir[METHOD_DATA_DIR_REBIND].offset))
    {
      MethodNameRebind *rebindInfo;
      while (BufferConsume(buffer, sizeof(MethodNameRebind), (char **)&rebindInfo))
      {
        if (!RebindMethodNames(vm, module, rebindInfo, &methodNames))
        {
          ret = false;
          break;
        }
      }
    }
  }

  return ret;
}

bool LoadConstants(Buffer *buffer, WrenVM *vm, ObjModule *module, ObjFn *fn)
{
  if (!buffer->pointer)
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
      ret = false;
    }
  } while (ret && !IsBufferExhausted(buffer));

  return ret;  
}

bool LoadFNs(Buffer *indexBuffer, Buffer *buffer,
  WrenVM *vm, ObjModule *module)
{
  if (!indexBuffer->pointer || !buffer->pointer)
    return false;

  bool ret = true;

  do
  {
    DataDirectory *dataDir;
    FnHeader      *header;
    uint8_t       *code;
    char          *constants;
    char          *debug;

    if (BufferConsume(indexBuffer, sizeof(DataDirectory), (char **)&dataDir)

      && BufferSeek(buffer, dataDir->offset)
      && BufferConsume(buffer, sizeof(FnHeader), (char **)&header)

      && header->dataDir[FN_DATA_DIR_CODE].size != 0
      && BufferSeek(buffer, dataDir->offset + header->dataDir[FN_DATA_DIR_CODE].offset)
      && BufferConsume(buffer, header->dataDir[FN_DATA_DIR_CODE].size, (char **)&code)

      && BufferSeek(buffer, dataDir->offset + header->dataDir[FN_DATA_DIR_CONSTANTS].offset)
      && BufferConsume(buffer, header->dataDir[FN_DATA_DIR_CONSTANTS].size, (char **)&constants)

      && BufferSeek(buffer, dataDir->offset + header->dataDir[FN_DATA_DIR_DEBUG].offset)
      && BufferConsume(buffer, header->dataDir[FN_DATA_DIR_DEBUG].size, (char **)&debug)
      )
    {
      ObjFn *fn = wrenNewFunction(vm, module, header->maxSlots);
      wrenPushRoot(vm, (Obj *)fn);
      fn->arity       = header->arity;
      fn->numUpvalues = header->numUpvalues;
      //TODO: add function name.

      Buffer constantBuffer;
      BufferSet(&constantBuffer, constants, header->dataDir[FN_DATA_DIR_CONSTANTS].size);
      ret = LoadConstants(&constantBuffer, vm, module, fn);

      wrenByteBufferAppend(vm, &fn->code, code, header->dataDir[FN_DATA_DIR_CODE].size);
      if (header->dataDir[FN_DATA_DIR_DEBUG].size)
      {
        wrenIntBufferAppend(vm, &fn->debug->sourceLines, (int *)debug,
          header->dataDir[FN_DATA_DIR_DEBUG].size / sizeof(int));
      }

      wrenPopRoot(vm);
    }
    else
    {
      ret = false;
    }
  } while (ret && !IsBufferExhausted(indexBuffer));

  return ret;
}

bool wrenLoadModule(WrenVM *vm, const char *moduleName)
{
  Value name = wrenNewString(vm, moduleName);  
  ObjModule* module = getModule(vm, name);
  if (module)
    return true;

  wrenPushRoot(vm, AS_OBJ(name));
  module = wrenNewModule(vm , AS_STRING(name));
  wrenPushRoot(vm, (Obj *)module);

  // Implicitly import the core module.
  if (_stricmp(moduleName, CORE_MODULE_NAME))
  {
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

  Buffer methodNameBuffer;
  Buffer variableBuffer;
  Buffer fnBuffer;
  Buffer fnIndexBuffer;

  BufferInit(&variableBuffer);
  BufferInit(&fnIndexBuffer);
  BufferInit(&fnBuffer);
  BufferInit(&methodNameBuffer);

  char *fileName = NULL;  
  FILE *f        = NULL;
  ModuleFileHeader header;
  long int         fileSize = 0;

  if ((fileName = GetModuleFilePath(moduleName)) != NULL
    && (f = fopen(fileName, "rb")) != NULL
    && fseek(f, SEEK_END, 0) == 0
    && (fileSize = ftell(f)) != -1L

    && fseek(f, SEEK_SET, 0) == 0
    && fread(&header, 1, sizeof(header), f) == sizeof(header)
    && IsValid(&header, fileSize)

    && BufferInitSize(&variableBuffer, header.dataDir[MODULE_DATA_DIR_VARIABLE].size)
    && fseek(f, SEEK_SET, header.dataDir[MODULE_DATA_DIR_VARIABLE].offset) == 0
    && fread(variableBuffer.pointer, 1, variableBuffer.capacity, f) == variableBuffer.capacity

    && BufferInitSize(&fnIndexBuffer, header.dataDir[MODULE_DATA_DIR_FN_INDEX].size)
    && fseek(f, SEEK_SET, header.dataDir[MODULE_DATA_DIR_FN_INDEX].offset) == 0
    && fread(fnIndexBuffer.pointer, 1, fnIndexBuffer.capacity, f) == fnIndexBuffer.capacity

    && BufferInitSize(&fnBuffer, header.dataDir[MODULE_DATA_DIR_FN].size)
    && fseek(f, SEEK_SET, header.dataDir[MODULE_DATA_DIR_FN].offset) == 0
    && fread(fnBuffer.pointer, 1, fnBuffer.capacity, f) == fnBuffer.capacity

    && BufferInitSize(&methodNameBuffer, header.dataDir[MODULE_DATA_DIR_METHOD].size)
    && fseek(f, SEEK_SET, header.dataDir[MODULE_DATA_DIR_METHOD].offset) == 0
    && fread(methodNameBuffer.pointer, 1, methodNameBuffer.capacity, f) == methodNameBuffer.capacity
    )
  {
    variableBuffer.length     = variableBuffer.capacity;
    fnIndexBuffer.length      = fnIndexBuffer.capacity;
    fnBuffer.length           = fnBuffer.capacity;
    methodNameBuffer.length   = methodNameBuffer.capacity;

    ret = LoadVariables(&variableBuffer, vm, module)
      && LoadFNs(&fnIndexBuffer, &fnBuffer, vm ,module)
      && LoadMethods(&methodNameBuffer, vm ,module);
  }

  if (f) fclose(f);
  if (fileName) free(fileName);

  BufferFree(&variableBuffer);
  BufferFree(&fnIndexBuffer);
  BufferFree(&fnBuffer);
  BufferFree(&methodNameBuffer);

  if (ret)
  {
    // Store it in the VM's module registry so we don't load the same module
    // multiple times.
    wrenMapSet(vm, vm->modules, name, OBJ_VAL(module));
  }

  wrenPopRoot(vm);
  wrenPopRoot(vm);

  if (ret)
  {
    ObjClosure *closure;
    ObjFn      *entryFn;
    if (!(entryFn = GetObjFn(vm, module, header.entryFnIndex))
      || !(closure = wrenNewClosure(vm, entryFn))
      )
    {
      return false;
    }

    wrenPushRoot(vm, (Obj *)closure);
    ObjFiber* fiber = wrenNewFiber(vm, closure);
    wrenPopRoot(vm);

    if (!fiber)
      return false;
    
    return runInterpreter(vm, fiber) == WREN_RESULT_SUCCESS;
  }

  return ret;
}