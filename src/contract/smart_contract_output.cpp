#include "./smart_contract_output.hpp"
#include "../include/wren.hpp"
#include <fc/exception/exception.hpp>
#include <graphene/chain/account_evaluator.hpp>
#include <graphene/chain/database.hpp>
#include <cctype>

using namespace graphene::chain;

extern char * rootDirectory;

void wrenVmDeleter(WrenVM *vm)
{
    wrenFreeVM(vm);
}

void reportError(WrenVM* vm, WrenErrorType type,
                        const char* module, int line, const char* message)
{
    switch (type)
    {
    case WREN_ERROR_COMPILE:
        elog("vm compile error: module ${m}, line ${l}, msg ${s}", 
            ("m", module)("l", line)("s", message));
        break;
      
    case WREN_ERROR_RUNTIME:
        elog("vm runtime error: ${s}", ("s", message));
        break;
      
    case WREN_ERROR_STACK_TRACE:
        elog("vm stack trace: module ${m}, line ${l}, msg ${s}",
            ("m", module)("l", line)("s", message));
        break;
    }
}

static void write(WrenVM* vm, const char* text)
{
    ilog("vm log print: ${m}", ("m", text));
}

static char* readFile(const char* path)
{
  FILE* file = fopen(path, "rb");
  if (file == NULL) return NULL;
  
  // Find out how big the file is.
  fseek(file, 0L, SEEK_END);
  size_t fileSize = ftell(file);
  rewind(file);
  
  // Allocate a buffer for it.
  char* buffer = (char*)malloc(fileSize + 1);
  if (buffer == NULL)
  {
    fprintf(stderr, "Could not read file \"%s\".\n", path);
    exit(74);
  }
  
  // Read the entire file.
  size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
  if (bytesRead < fileSize)
  {
    fprintf(stderr, "Could not read file \"%s\".\n", path);
    exit(74);
  }
  
  // Terminate the string.
  buffer[bytesRead] = '\0';
  
  fclose(file);
  return buffer;
}

static char* wrenFilePath(const char* name)
{
  // The module path is relative to the root directory and with ".wren".
  size_t rootLength = rootDirectory == NULL ? 0 : strlen(rootDirectory);
  size_t nameLength = strlen(name);
  size_t pathLength = rootLength + nameLength + 5;
  char* path = (char*)malloc(pathLength + 1);
  
  if (rootDirectory != NULL)
  {
    memcpy(path, rootDirectory, rootLength);
  }
  
  memcpy(path + rootLength, name, nameLength);
  memcpy(path + rootLength + nameLength, ".wren", 5);
  path[pathLength] = '\0';
  
  return path;
}

static char* readModule(WrenVM* vm, const char* module)
{
 // char* source = readBuiltInModule(module);
//  if (source != NULL) return source;
  
  // First try to load the module with a ".wren" extension.
  char* modulePath = wrenFilePath(module);
  char* moduleContents = readFile(modulePath);
  free(modulePath);
  
  if (moduleContents != NULL) return moduleContents;
  
  // If no contents could be loaded treat the module name as specifying a
  // directory and try to load the "module.wren" file in the directory.
  size_t moduleLength = strlen(module);
  size_t moduleDirLength = moduleLength + 7;
  char* moduleDir = (char*)malloc(moduleDirLength + 1);
  memcpy(moduleDir, module, moduleLength);
  memcpy(moduleDir + moduleLength, "/module", 7);
  moduleDir[moduleDirLength] = '\0';
  
  char* moduleDirPath = wrenFilePath(moduleDir);
  free(moduleDir);
  
  moduleContents = readFile(moduleDirPath);
  free(moduleDirPath);
  
  return moduleContents;
}

string smart_contract_deploy_evaluator::construct_smart_contract(const string &bytecode,
                                                                 const contract_addr_type &contract_addr,
                                                                 const string &construct_data,
                                                                 const string &abi_json)
{
    UserData userData;
    userData.size   = sizeof(userData);
    userData.vmMode = VM_MODE_BYTECODE;
    userData.db     = &db();

    WrenConfiguration config;
    wrenInitConfiguration(&config);
    config.bindForeignMethodFn = NULL;
    config.writeFn             = write;
    config.errorFn             = reportError;
    config.initialHeapSize     = 1024 * 1024 * 10;
    config.loadModuleFn        = readModule;
    config.userData            = &userData;

    string newStatestring;

    if (userData.vmMode == VM_MODE_BYTECODE)
    {
        std::unique_ptr<WrenVM, decltype(&wrenVmDeleter)> vm(wrenNewVM(&config), &wrenVmDeleter);

        //TODO: optimize, no need to do base64 decode every time.
        string decoded = fc::base64_decode(bytecode);
        vector<uint8_t> decodedBytecode(decoded.begin(), decoded.end());

        //call constructor through ABI-based function call, and get ret value of construction.
        vector<uint8_t> oldState;
        vector<uint8_t> newState;
        bool ret = wrenCallMethod(vm.get(), decodedBytecode, true,
                                    construct_data, abi_json, contract_addr,
                                    oldState, newState);
        FC_ASSERT(ret, "failed to construct smart contract");

        if (!newState.empty())
            newStatestring = fc::base64_encode(&newState[0], newState.size());
    }
    else
    {
        FC_ASSERT(false, "bad vm mode");
    }

    return newStatestring;
}

string smart_contract_call_evaluator::call_smart_contract(const string &bytecode,
                                                          const contract_addr_type &contract_addr,
                                                          const string &call_data,
                                                          const string &abi_json,
                                                          const string &starting_state)
{
    UserData userData;
    userData.size   = sizeof(userData);
    userData.vmMode = VM_MODE_BYTECODE;
    userData.db     = &db();

    WrenConfiguration config;
    wrenInitConfiguration(&config);
    config.bindForeignMethodFn = NULL;
    config.writeFn             = write;
    config.errorFn             = reportError;
    config.initialHeapSize     = 1024 * 1024 * 10;
    config.loadModuleFn        = readModule;
    config.userData            = &userData;

    string newStatestring;

    if (userData.vmMode == VM_MODE_BYTECODE)
    {
        std::unique_ptr<WrenVM, decltype(&wrenVmDeleter)> vm(wrenNewVM(&config), &wrenVmDeleter);

        //TODO: optimize, no need to do base64 decode every time.
        string decoded1 = fc::base64_decode(bytecode);
        vector<uint8_t> decodedBytecode(decoded1.begin(), decoded1.end());

        string decoded2 = fc::base64_decode(starting_state);
        vector<uint8_t> decodedState(decoded2.begin(), decoded2.end());

        vector<uint8_t> newState;
        bool ret = wrenCallMethod(vm.get(), decodedBytecode, false,
                                    call_data, abi_json, contract_addr,
                                    decodedState, newState);
        FC_ASSERT(ret, "failed to call smart contract");

        if (!newState.empty())
            newStatestring = fc::base64_encode(&newState[0], newState.size());
    }
    else
    {
        FC_ASSERT(false, "bad vm mode");
    }

    return newStatestring;
}