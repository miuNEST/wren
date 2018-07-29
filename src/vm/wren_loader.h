#ifndef WREN_LOADER_H
#define WREN_LOADER_H

#include "wren_value.h"
#include "wren_vm.h"
#include "wren.h"

#define CORE_MODULE_NAME    "core"
#define MODULE_NAME(module) ((module)->name ? (module)->name->value : CORE_MODULE_NAME)

bool wrenIsBuiltInModule(const ObjString *name);

bool wrenSaveCompiledModule(WrenVM *vm, ObjModule *module);

bool wrenLoadCompiledModule(WrenVM      *vm,
                            const char  *moduleName,
                            bool         runClosure,
                            ObjClosure **objClosure);
bool wrenLoadCompiledModule2(WrenVM      *vm,
                             const char  *moduleName,
                             bool         runClosure,
                             ObjClosure **objClosure);

bool wrenLoadCompiledModuleFromBuffer(WrenVM        *vm,
                                      const char    *moduleName,
                                      const uint8_t *bytecode,
                                      const uint32_t bytecodeLen,
                                      bool           runClosure,
                                      ObjClosure   **objClosure);

bool wrenGenerateABI(WrenVM *vm, ObjModule *module);
bool wrenCallMethod(WrenVM        *vm,
                    const uint8_t *bytecode,
                    const uint32_t bytecodeLen,
                    const char    *callData,
                    const char    *abiJson,
                    const char    *contractHash);

#endif