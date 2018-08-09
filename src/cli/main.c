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
