#ifndef WREN_LOADER_H
#define WREN_LOADER_H

#include "wren_value.h"
#include "wren_vm.h"
#include "wren.h"

#define CORE_MODULE_NAME    "core"
#define MODULE_NAME(module) ((module)->name ? (module)->name->value : CORE_MODULE_NAME)

bool wrenSaveCompiledModule(WrenVM *vm, ObjModule *module);

bool wrenLoadCompiledModule(WrenVM *vm, const char *moduleName,
  bool runClosure, ObjClosure **closure);

#endif