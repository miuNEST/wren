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
#ifndef wren_utils_h
#define wren_utils_h

#include "wren.h"
#include "wren_common.h"

// Reusable data structures and other utility functions.

// Forward declare this here to break a cycle between wren_utils.h and
// wren_value.h.
typedef struct sObjString ObjString;

// We need buffers of a few different types. To avoid lots of casting between
// void* and back, we'll use the preprocessor as a poor man's generics and let
// it generate a few type-specific ones.
#define DECLARE_BUFFER(name, type) \
    typedef struct \
    { \
      type* data; \
      int count; \
      int capacity; \
    } name##Buffer; \
    void wren##name##BufferInit(name##Buffer* buffer); \
    void wren##name##BufferClear(WrenVM* vm, name##Buffer* buffer); \
    void wren##name##BufferFill(WrenVM* vm, name##Buffer* buffer, type data, \
                                int count); \
    void wren##name##BufferWrite(WrenVM* vm, name##Buffer* buffer, type data); \
    void wren##name##BufferAppend(WrenVM* vm, name##Buffer* buffer, type *data, \
                                int count)

// This should be used once for each type instantiation, somewhere in a .c file.
#define DEFINE_BUFFER(name, type) \
    void wren##name##BufferInit(name##Buffer* buffer) \
    { \
      buffer->data = NULL; \
      buffer->capacity = 0; \
      buffer->count = 0; \
    } \
    \
    void wren##name##BufferClear(WrenVM* vm, name##Buffer* buffer) \
    { \
      wrenReallocate(vm, buffer->data, 0, 0); \
      wren##name##BufferInit(buffer); \
    } \
    \
    void wren##name##BufferFill(WrenVM* vm, name##Buffer* buffer, type data, \
                                int count) \
    { \
      if (buffer->capacity < buffer->count + count) \
      { \
        int capacity = wrenPowerOf2Ceil(buffer->count + count); \
        buffer->data = (type*)wrenReallocate(vm, buffer->data, \
            buffer->capacity * sizeof(type), capacity * sizeof(type)); \
        buffer->capacity = capacity; \
      } \
      \
      for (int i = 0; i < count; i++) \
      { \
        buffer->data[buffer->count++] = data; \
      } \
    } \
    \
    void wren##name##BufferWrite(WrenVM* vm, name##Buffer* buffer, type data) \
    { \
      wren##name##BufferFill(vm, buffer, data, 1); \
    } \
    \
    void wren##name##BufferAppend(WrenVM* vm, name##Buffer* buffer, type *data, \
                                int count) \
    { \
      if (buffer->capacity < buffer->count + count) \
      { \
        int capacity = wrenPowerOf2Ceil(buffer->count + count); \
        buffer->data = (type*)wrenReallocate(vm, buffer->data, \
            buffer->capacity * sizeof(type), capacity * sizeof(type)); \
        buffer->capacity = capacity; \
      } \
      \
      for (int i = 0; i < count; i++) \
      { \
        buffer->data[buffer->count++] = data[i]; \
      } \
    }

DECLARE_BUFFER(Byte, uint8_t);
DECLARE_BUFFER(Int, int);
DECLARE_BUFFER(String, ObjString*);

// TODO: Change this to use a map.
typedef StringBuffer SymbolTable;

// Initializes the symbol table.
void wrenSymbolTableInit(SymbolTable* symbols);

// Frees all dynamically allocated memory used by the symbol table, but not the
// SymbolTable itself.
void wrenSymbolTableClear(WrenVM* vm, SymbolTable* symbols);

// Adds name to the symbol table. Returns the index of it in the table.
int wrenSymbolTableAdd(WrenVM* vm, SymbolTable* symbols,
                       const char* name, size_t length);

// Adds name to the symbol table. Returns the index of it in the table. Will
// use an existing symbol if already present.
int wrenSymbolTableEnsure(WrenVM* vm, SymbolTable* symbols,
                          const char* name, size_t length);

// Looks up name in the symbol table. Returns its index if found or -1 if not.
int wrenSymbolTableFind(const SymbolTable* symbols,
                        const char* name, size_t length);

void wrenBlackenSymbolTable(WrenVM* vm, SymbolTable* symbolTable);

// Returns the number of bytes needed to encode [value] in UTF-8.
//
// Returns 0 if [value] is too large to encode.
int wrenUtf8EncodeNumBytes(int value);

// Encodes value as a series of bytes in [bytes], which is assumed to be large
// enough to hold the encoded result.
//
// Returns the number of written bytes.
int wrenUtf8Encode(int value, uint8_t* bytes);

// Decodes the UTF-8 sequence starting at [bytes] (which has max [length]),
// returning the code point.
//
// Returns -1 if the bytes are not a valid UTF-8 sequence.
int wrenUtf8Decode(const uint8_t* bytes, uint32_t length);

// Returns the number of bytes in the UTF-8 sequence starting with [byte].
//
// If the character at that index is not the beginning of a UTF-8 sequence,
// returns 0.
int wrenUtf8DecodeNumBytes(uint8_t byte);

// Returns the smallest power of two that is equal to or greater than [n].
int wrenPowerOf2Ceil(int n);

#endif
