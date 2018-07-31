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

bool wrenLoadCompiledModuleFromBuffer(WrenVM        *vm,
                                      const char    *moduleName,
                                      const uint8_t *bytecode,
                                      const uint32_t bytecodeLen,
                                      bool           runClosure,
                                      ObjClosure   **objClosure);

bool wrenCallMethod(WrenVM               *vm,
                    const vector<uint8_t> &decodedBytecode,
                    bool                   doConstruct,
                    const string          &callData,
                    const string          &abiJson,
                    const string          &contractHash,
                    const vector<uint8_t> &decodedOldState,
                    vector<uint8_t>       &newState);

//https://blogs.msdn.microsoft.com/vcblog/2018/04/09/msvc-now-correctly-reports-__cplusplus/
#if __cplusplus < 201402L && !defined(_MSC_VER)

//http://www.open-std.org/JTC1/sc22/WG21/docs/papers/2013/n3656.htm
//https ://docs.microsoft.com/en-us/cpp/standard-library/memory-functions#make_unique

#include <cstddef>
#include <memory>
#include <type_traits>
#include <utility>

namespace std {
    template<class T> struct _Unique_if {
        typedef unique_ptr<T> _Single_object;
    };

    template<class T> struct _Unique_if<T[]> {
        typedef unique_ptr<T[]> _Unknown_bound;
    };

    template<class T, size_t N> struct _Unique_if<T[N]> {
        typedef void _Known_bound;
    };

    template<class T, class... Args>
    typename _Unique_if<T>::_Single_object
        make_unique(Args&&... args) {
        return unique_ptr<T>(new T(std::forward<Args>(args)...));
    }

    template<class T>
    typename _Unique_if<T>::_Unknown_bound
        make_unique(size_t n) {
        typedef typename remove_extent<T>::type U;
        return unique_ptr<T>(new U[n]());
    }

    template<class T, class... Args>
    typename _Unique_if<T>::_Known_bound
        make_unique(Args&&...) = delete;
}

#endif

#endif

#endif