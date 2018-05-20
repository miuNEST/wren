#ifndef WREN_LOADER_H
#define WREN_LOADER_H

#include "wren_value.h"
#include "wren_vm.h"
#include "wren.h"

#define CORE_MODULE_NAME    "core"
#define MODULE_NAME(module) ((module)->name ? (module)->name->value : CORE_MODULE_NAME)

void wrenSaverInit(void);
bool wrenSaveCompiledModule(WrenVM *vm, ObjModule *module);

void wrenLoaderInit(void);
bool wrenLoadModule(WrenVM *vm, const char *moduleName);

#endif