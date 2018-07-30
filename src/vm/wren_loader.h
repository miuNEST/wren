#ifndef WREN_LOADER_H
#define WREN_LOADER_H

#ifdef __cplusplus
#include <string>
#include <vector>
using namespace std;
#endif

#ifdef __cplusplus
extern "C" {
#endif

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
bool wrenGenerateABI(WrenVM *vm, ObjModule *module);

#ifdef __cplusplus 
}
#endif 

#ifdef __cplusplus

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


bool wrenCallMethod2(WrenVM        *vm,
                    const uint8_t *bytecode,
                    const uint32_t bytecodeLen,
                    bool           doConstruct,
                    const char    *callData,
                    const char    *abiJson,
                    const char    *contractHash,
                    const char    *oldState,
                    uint32_t       oldStateLen,
                    char         **newState,
                    uint32_t      *newStateLen
                    );

bool wrenCallMethod(WrenVM               *vm,
                    const vector<uint8_t> &decodedBytecode,
                    bool                   doConstruct,
                    const string          &callData,
                    const string          &abiJson,
                    const string          &contractHash,
                    const vector<uint8_t> &decodedOldState,
                    vector<uint8_t>       &newState);

#endif

#endif