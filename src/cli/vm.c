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
#include <stdio.h>
#include <string.h>

#include "io.h"
#include "modules.h"
#include "scheduler.h"
#include "vm.h"
#include "wren_loader.h"

// The single VM instance that the CLI uses.
static WrenVM* vm;

static WrenBindForeignMethodFn bindMethodFn = NULL;
static WrenBindForeignClassFn bindClassFn = NULL;
static WrenForeignMethodFn afterLoadFn = NULL;

static uv_loop_t* loop;

// The exit code to use unless some other error overrides it.
int defaultExitCode = 0;

// Reads the contents of the file at [path] and returns it as a heap allocated
// string.
//
// Returns `NULL` if the path could not be found. Exits if it was found but
// could not be read.
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

// Converts the module [name] to a file path.
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

// Attempts to read the source for [module] relative to the current root
// directory.
//
// Returns it if found, or NULL if the module could not be found. Exits if the
// module was found but could not be read.
static char* readModule(WrenVM* vm, const char* module)
{
  char* source = readBuiltInModule(module);
  if (source != NULL) return source;
  
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

// Binds foreign methods declared in either built in modules, or the injected
// API test modules.
static WrenForeignMethodFn bindForeignMethod(WrenVM* vm, const char* module,
    const char* className, bool isStatic, const char* signature)
{
  WrenForeignMethodFn method = bindBuiltInForeignMethod(vm, module, className,
                                                        isStatic, signature);
  if (method != NULL) return method;
  
  if (bindMethodFn != NULL)
  {
    return bindMethodFn(vm, module, className, isStatic, signature);
  }

  return NULL;
}

// Binds foreign classes declared in either built in modules, or the injected
// API test modules.
static WrenForeignClassMethods bindForeignClass(
    WrenVM* vm, const char* module, const char* className)
{
  WrenForeignClassMethods methods = bindBuiltInForeignClass(vm, module,
                                                            className);
  if (methods.allocate != NULL) return methods;

  if (bindClassFn != NULL)
  {
    return bindClassFn(vm, module, className);
  }

  return methods;
}

static void write(WrenVM* vm, const char* text)
{
  printf("%s", text);
}

static void reportError(WrenVM* vm, WrenErrorType type,
                        const char* module, int line, const char* message)
{
  switch (type)
  {
    case WREN_ERROR_COMPILE:
      fprintf(stderr, "[%s line %d] %s\n", module, line, message);
      break;
      
    case WREN_ERROR_RUNTIME:
      fprintf(stderr, "%s\n", message);
      break;
      
    case WREN_ERROR_STACK_TRACE:
      fprintf(stderr, "[%s line %d] in %s\n", module, line, message);
      break;
  }
}

static void initVM(VM_MODE vmMode)
{
  WrenConfiguration config;
  wrenInitConfiguration(&config);

  //user-defined vm config data
  UserData *userData = malloc(sizeof(UserData));
  memset(userData, 0, sizeof(*userData));
  userData->size   = sizeof(UserData);
  userData->vmMode = vmMode;
  config.userData = userData;

  config.bindForeignMethodFn = bindForeignMethod;
  config.bindForeignClassFn = bindForeignClass;
  config.loadModuleFn = readModule;
  config.writeFn = write;
  config.errorFn = reportError;

  // Since we're running in a standalone process, be generous with memory.
  config.initialHeapSize = 1024 * 1024 * 100;
  vm = wrenNewVM(&config);

  // Initialize the event loop.
  loop = (uv_loop_t*)malloc(sizeof(uv_loop_t));
  uv_loop_init(loop);
}

static void freeVM()
{
  ioShutdown();
  schedulerShutdown();
  
  uv_loop_close(loop);
  free(loop);
  
  if (vm->config.userData)
  {
    free(vm->config.userData);
  }

  wrenFreeVM(vm);

  uv_tty_reset_mode();
}

char *getModuleNameWithoutSuffix(const char *name)
{
  char *moduleName = _strdup(name);
  if (!moduleName)
  {
    ASSERT(false, "out of memory");
    exit(-1);
  }

  char * lastDot = strrchr(moduleName, '.');
  if (lastDot &&
    (!_stricmp(lastDot, ".wren") || !_stricmp(lastDot, ".wrc")))
  {
    *lastDot = '\0';
  }

  return moduleName;
}

void runFile(const char* path, VM_MODE vmMode)
{
  const char *moduleName;

  // Use the directory where the file is as the root to resolve imports
  // relative to.
  char* root = NULL;
  const char* lastSlash = strrchr(path, '/');
  if (lastSlash != NULL)
  {
    root = (char*)malloc(lastSlash - path + 2);
    memcpy(root, path, lastSlash - path + 1);
    root[lastSlash - path + 1] = '\0';
    rootDirectory = root;

    moduleName = lastSlash + 1;
  }
  else
  {
    moduleName = path;
  }

  char *name = getModuleNameWithoutSuffix(moduleName);

  initVM(vmMode);

  char* source = NULL;
  WrenInterpretResult result;    

  if (vmMode == VM_MODE_BYTECODE)
  {
    if (wrenLoadCompiledModule(vm, name, true, NULL))
      result = WREN_RESULT_SUCCESS;
    else
    {
      ASSERT(false, "failed to execute byte code file");
      result = WREN_RESULT_RUNTIME_ERROR;
    }
  }
  else
  {
    source = readFile(path);
    if (source == NULL)
    {
      fprintf(stderr, "Could not find file \"%s\".\n", path);
      exit(66);
    }

    if (vmMode == VM_MODE_COMPILE)
    {
      ObjClosure* closure = wrenCompileSource(vm, name, source, false, true);
      result = (closure == NULL) ? WREN_RESULT_COMPILE_ERROR : WREN_RESULT_SUCCESS;
    }
    else
    {
      result = wrenInterpret(vm, name, source);
      ASSERT(result == WREN_RESULT_SUCCESS, "failed to interpret file");
    }
  }

  if (afterLoadFn != NULL) afterLoadFn(vm);
  
  if (result == WREN_RESULT_SUCCESS)
  {
    uv_run(loop, UV_RUN_DEFAULT);
  }

  freeVM();

  free(name);
  free(source);
  free(root);

  // Exit with an error code if the script failed.
  if (result == WREN_RESULT_COMPILE_ERROR) exit(65); // EX_DATAERR.
  if (result == WREN_RESULT_RUNTIME_ERROR) exit(70); // EX_SOFTWARE.
  
  if (defaultExitCode != 0) exit(defaultExitCode);
}

int runRepl()
{
  initVM(VM_MODE_INTERPRET);

  printf("\\\\/\"-\n");
  printf(" \\_/   wren v%s\n", WREN_VERSION_STRING);

  wrenInterpret(vm, "main", "import \"repl\"\n");
  
  uv_run(loop, UV_RUN_DEFAULT);

  freeVM();

  return 0;
}

WrenVM* getVM()
{
  return vm;
}

uv_loop_t* getLoop()
{
  return loop;
}

void setExitCode(int exitCode)
{
  defaultExitCode = exitCode;
}

void setTestCallbacks(WrenBindForeignMethodFn bindMethod,
                      WrenBindForeignClassFn bindClass,
                      WrenForeignMethodFn afterLoad)
{
  bindMethodFn = bindMethod;
  bindClassFn = bindClass;
  afterLoadFn = afterLoad;
}
