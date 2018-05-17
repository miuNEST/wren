#include "wren_loader.h"

#include <stdio.h>
#include "wren_value.h"

#define WREN_FILE_MAGIC       'NERW'
#define WREN_BYTECODDE_MAJOR  1
#define WREN_BYTECODDE_MINOR  0

#ifndef _countof
#define _countof(x)    (sizeof(x) / sizeof((x)[0]))
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
  uint32_t      entryFn;           //offset relative to beginning of dataDir[MODULE_DATA_DIR_FN];
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
  FN_CONST_TYPE_NUM  = 0,
  FN_CONST_TYPE_STR  = 1,
  FN_CONST_TYPE_FN   = 2,
};

#pragma pack(pop)

typedef struct Buffer
{
  char    *pointer;
  uint32_t length;
  uint32_t capacity;
  uint32_t consumed;
} Buffer;

extern char const* rootDirectory;

Buffer methodNameBuffer;
Buffer variableBuffer;
Buffer fnBuffer;
Buffer fnIndexBuffer;

uint32_t      entryFn;

bool SaveValueToBuffer(WrenVM *vm, ObjModule *module,
  Value value, Buffer *buffer);

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

void WrenSaverInit(void)
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

    printf("module: %s\n", module->name ? module->name->value : "core");

    for (int k = 0; k < module->variableNames.count; k++)
    {
      printf("    variableNames %d: %s\n", k, module->variableNames.data[k]->value);
    }
    printf("\n");

  }

  ASSERT(actualCount == vm->modules->count, "too many or too few modules");

  return true;
}

//TODO: only save method names of a single module.
static bool SaveModuleMethodToBuffer(WrenVM *vm, ObjModule *module, Buffer *buffer)
{
  for (int k = 0; k < vm->methodNames.count; k++)
  {
    const char *data       = vm->methodNames.data[k]->value;
    uint32_t    dataLength = vm->methodNames.data[k]->length;
    if (!BufferAppend(buffer, (const char *)&dataLength, sizeof(uint32_t))
      || !BufferAppend(buffer, data, dataLength))
    {
      return false;
    }
  }

  return true;
}

// save all variable names of a single module.
static bool SaveMoudleVariableToBuffer(WrenVM *vm, ObjModule *module, Buffer *buffer)
{
  for (int k = 0; k < module->variableNames.count; k++)
  {
    const char *data       = module->variableNames.data[k]->value;
    uint32_t    dataLength = module->variableNames.data[k]->length;
    if (!BufferAppend(buffer, (const char *)&dataLength, sizeof(uint32_t))
      || !BufferAppend(buffer, data, dataLength)
      || !SaveValueToBuffer(vm, module, module->variables.data[k], buffer))
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

    index++;

    if (fn == fnTarget)
      return index;
  }

  ASSERT(false, "ObjFn not found!");
  return (uint32_t)-1;
}

//TODO: 此处增加对新的类型的处理。
bool SaveValueToBuffer(WrenVM *vm, ObjModule *module,
  Value value, Buffer *buffer)
{
#if WREN_NAN_TAGGING
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
          tlvStr.type = FN_CONST_TYPE_STR;
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
      default:
        ASSERT(false, "add code here to process more types!");
        return false;
    }
  }
  else
  {
    ASSERT(false, "add code here to process more types!");
    return false;
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
  for (int k = 0; k < fn->constants.count; k++)
  {
    Value value = fn->constants.data[k];
    if (!SaveValueToBuffer(vm, module, value, buffer))
      return false;
  }

  return true;
}

static bool SaveModuleFnToBuffer(WrenVM *vm, ObjModule *module,
  Buffer *indexBuffer, Buffer *buffer)
{
  bool ret = true;

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
      entryFn = dataDir.offset;     //offset relative to beginning of dataDir[MODULE_DATA_DIR_FN];
    }
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
  const char *moduleName = module->name ? module->name->value : "core";
  return GetModuleFilePath(moduleName);
}

// save all info of a single module to a file.
static bool SaveModule(WrenVM *vm, ObjModule *module)
{
  bool ret = false;
  
  entryFn = 0;

  BufferInit(&methodNameBuffer);
  BufferInit(&variableBuffer);
  BufferInit(&fnBuffer);
  BufferInit(&fnIndexBuffer);

  if (SaveMoudleVariableToBuffer(vm, module, &variableBuffer)
    && SaveModuleFnToBuffer(vm, module, &fnIndexBuffer, &fnBuffer)
    && entryFn != 0
    && SaveModuleMethodToBuffer(vm, module, &methodNameBuffer))
  {
    const char *moduleName = module->name ? module->name->value : "core";

    ModuleFileHeader header;
    memset(&header, 0, sizeof(header));

    header.magic        = WREN_FILE_MAGIC;
    header.majorVersion = WREN_BYTECODDE_MAJOR;
    header.minorVersion = WREN_BYTECODDE_MINOR;
    header.entryFn      = entryFn;
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

bool WrenSaveBytecode(WrenVM *vm)
{
  DumpModules(vm);

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
    if (!SaveModule(vm, module))
      return false;
  }

  ASSERT(actualCount == vm->modules->count, "too many or too few modules");

  return true;
}

void WrenLoaderInit(void)
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

    case FN_CONST_TYPE_STR:
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

  for(;;)
  {
    if (IsBufferExhausted(buffer))
      break;

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
      break;
    }
  }

  return ret;
}

/*
  TODO: 对引用到vm->methodNames表的下标的指令进行重定位。
  
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

  do
  {
    if (IsBufferExhausted(buffer))
      break;

    uint32_t *nameLength;
    char     *methodName;
    int       symbol;

    if (BufferConsume(buffer, sizeof(uint32_t), (char **)&nameLength)
      && BufferConsume(buffer, *nameLength, &methodName)
      )
    {      
      symbol = wrenSymbolTableEnsure(vm, &vm->methodNames, methodName, *nameLength);
    }
    else
    {
      ret = false;
    }

    if (methodName)  free(methodName);
  } while (ret);

  return ret;
}

bool LoadConstants(Buffer *buffer, WrenVM *vm, ObjModule *module, ObjFn *fn)
{
  if (!buffer->pointer)
    return true;

  bool ret = true;

  do
  {
    if (IsBufferExhausted(buffer))
      break;

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
  } while (ret);

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
    if (IsBufferExhausted(indexBuffer))
      break;

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
  } while (ret);

  return ret;
}

bool WrenLoadModule(WrenVM *vm, const char *moduleName)
{
  Value name = wrenNewString(vm, moduleName);  
  ObjModule* module = getModule(vm, name);
  if (module)
    return true;

  wrenPushRoot(vm, AS_OBJ(name));
  module = wrenNewModule(vm , AS_STRING(name));
  wrenPushRoot(vm, (Obj *)module);

  // Implicitly import the core module.
  if (_stricmp(moduleName, "core"))
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

  //TODO: 创建fiber、closure，执行本module的主fn。

  return ret;
}
