#include "wren_loader.h"

void WrenSaverInit(void)
{

}

static bool SaveMethodNames(WrenVM *vm)
{
  for (int i = 0; i < vm->methodNames.count; i++)
  {
    printf("    methodNames %d: %s\n", i, vm->methodNames.data[i]->value);
  }

  printf("\n");

  return true;
}

static bool SaveModules(WrenVM *vm)
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
    ASSERT(actualCount <= vm->modules->count);

    ObjModule *module = AS_MODULE(vm->modules->entries[i].value);
    
    printf("module: %s\n", module->name ? module->name->value : "core");

    for (int k = 0; k < module->variableNames.count; k++)
    {
      printf("    variableNames %d: %s\n", k, module->variableNames.data[k]->value);
    }
    printf("\n");

  }

  ASSERT(actualCount == vm->modules->count);

  return true;
}

static bool SaveFns(WrenVM *vm)
{
  Obj *obj = vm->first;
  for(obj = vm->first; obj; obj = obj->next)
  {
    if (obj->type != OBJ_FN)
      continue;

    ObjFn *fn = (ObjFn *)obj;
    printf("fn %p, %s, module: %s\n", fn, fn->debug->name,
      fn->module->name ? fn->module->name->value : "core");

    for (int k = 0; k < fn->constants.count; k++)
    {
      printf("    const %d: ", k);
      wrenDumpValue(fn->constants.data[k]);
      printf("\n");
    }
    printf("\n");
  }

  return true;
}

bool WrenSaverDump(WrenVM *vm)
{
  if (!SaveMethodNames(vm))
    return false;

  if (!SaveModules(vm))
    return false;

  if (!SaveFns(vm))
    return false;

  return true;
}

void WrenLoaderInit(void)
{

}
