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
#ifndef vm_h
#define vm_h

#include "uv.h"
#include "wren.h"

// Executes the Wren script at [path] in a new VM.
//
// Exits if the script failed or could not be loaded.
void runFile(const char* path, VM_MODE vmMode);

// Runs the Wren interactive REPL.
int runRepl();

// Gets the currently running VM.
WrenVM* getVM();

// Gets the event loop the VM is using.
uv_loop_t* getLoop();

// Set the exit code the CLI should exit with when done.
void setExitCode(int exitCode);

// Adds additional callbacks to use when binding foreign members from Wren.
//
// Used by the API test executable to let it wire up its own foreign functions.
// This must be called before calling [createVM()].
void setTestCallbacks(WrenBindForeignMethodFn bindMethod,
                      WrenBindForeignClassFn bindClass,
                      void (*afterLoad)(WrenVM* vm));

#endif
