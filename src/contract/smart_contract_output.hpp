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
#ifndef smart_contract_output_h
#define smart_contract_output_h
#include <stdio.h>
#include <string.h>
#include <iostream>
#include <string>
#include "wren.hpp"


using namespace std;

// This module defines the built-in classes and their primitives methods that
// are implemented directly in C code. Some languages try to implement as much
// of the core module itself in the primary language instead of in the host
// language.
//
// With Wren, we try to do as much of it in C as possible. Primitive methods
// are always faster than code written in Wren, and it minimizes startup time
// since we don't have to parse, compile, and execute Wren code.
//
// There is one limitation, though. Methods written in C cannot call Wren ones.
// They can only be the top of the callstack, and immediately return. This
// makes it difficult to have primitive methods that rely on polymorphic
// behavior. For example, `IO.write` should call `toString` on its argument,
// including user-defined `toString` methods on user-defined classes.

int smart_contract_output( );
//int smart_contract_output(double &d1)//, double &d2, double &d3);
string run_wren_vm_when_activating_smart_contract(string input_source_code,string initargu);//首次激活合约时调用的接口函数 
string InvokeSmartContract(string input_source_code, string contranct_method, string input_data);//激活之后，合约每次被调用时，需要调用该函数
void Trim(char* str);


#endif
