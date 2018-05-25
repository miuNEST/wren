#include <stdio.h>
#include <string.h>

#include "os.h"
#include "vm.h"
#include "wren.h"

int main(int argc, const char* argv[])
{
  if (argc == 2 && strcmp(argv[1], "--help") == 0)
  {
    printf("Usage: wren [file] [arguments...]\n");
    printf("  --help  Show command line usage\n");
    return 0;
  }
  
  if (argc == 2 && strcmp(argv[1], "--version") == 0)
  {
    printf("wren %s\n", WREN_VERSION_STRING);
    return 0;
  }
  
  osSetArguments(argc, argv);

  if (argc == 1)
  {
    runRepl();
  }
  else
  {
    VM_MODE vmMode = VM_MODE_INTERPRET;

    if (strstr(argv[1], ".wrc"))
    {
      vmMode = VM_MODE_BYTECODE;
      runFile(argv[1], vmMode);
    }
    else if (argc >= 3 && (_stricmp(argv[1], "-c") == 0))
    {
      vmMode = VM_MODE_COMPILE;
      runFile(argv[2], vmMode);
    }
    else if (argc >= 3 && (_stricmp(argv[2], "-c") == 0))
    {
      vmMode = VM_MODE_COMPILE;
      runFile(argv[1], vmMode);
    }
    else
    {
      runFile(argv[1], vmMode);
    }    
  }

  return 0;
}
