#ifndef WREN_LOADER_H
#define WREN_LOADER_H

#include "wren_value.h"
#include "wren_vm.h"
#include "wren.h"

void WrenSaverInit(void);
bool WrenSaverDump(WrenVM *vm);

void WrenLoaderInit(void);


#endif