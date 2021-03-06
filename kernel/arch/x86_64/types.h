#pragma once

#include <stdint.h>
#include <stddef.h>
#include <limits.h>

#ifdef	__cplusplus
# define restrict __restrict
# define __BEGIN_DECLS	extern "C" {
# define __END_DECLS	}

#define __BEGIN_NAMESPACE(n)        namespace n {
#define __BEGIN_NAMESPACE_DETAIL    __BEGIN_NAMESPACE(detail)
#define __BEGIN_NAMESPACE_STD       __BEGIN_NAMESPACE(std)
#define __BEGIN_NAMESPACE_EXT       __BEGIN_NAMESPACE(ext)
#define __END_NAMESPACE             }
#define __END_NAMESPACE_STD         __END_NAMESPACE
#define __END_NAMESPACE_EXT         __END_NAMESPACE
#else
# define __BEGIN_DECLS
# define __END_DECLS
#endif

#if defined(__GNUC__)

#define _no_asan                __attribute__((__no_address_safety_analysis__))
#define _packed                 __attribute__((__packed__))
#define _const                  __attribute__((__const__))
#define _pure                   __attribute__((__pure__))
#define _malloc                 __attribute__((__malloc__))
#define _assume_aligned(n)      __attribute__((__assume_aligned__(n)))
#define _alloc_size(...)        __attribute__((__alloc_size__(__VA_ARGS__)))
#define _alloc_align(pi)        __attribute__((__alloc_align__(pi)))
#define _unused                 __attribute__((__unused__))
#define _use_result             __attribute__((__warn_unused_result__))
#define _leaf                   __attribute__((__leaf__))
#define __aligned(n)            __attribute__((__aligned__(n)))
#define _warn_unused_result     __attribute__((__warn_unused_result__))
#define _always_inline          __attribute__((__always_inline__)) inline
#define _flatten                __attribute__((__flatten__))
#define _always_optimize        __attribute__((__optimize__("-O2")))
#define _noreturn               __attribute__((__noreturn__))
#define _used                   __attribute__((__used__))
#define _returns_twice          __attribute__((__returns_twice__))
#define _vector_size(n)         __attribute__((__vector_size__(n)))
#define _noinline               __attribute__((__noinline__))
#define _printf_format(m,n)     __attribute__((__format__(printf, m, n)))
#define _printf_format3(m,n,o)  __attribute__((__format__(printf, (m), (n), (o))))
#define _artificial             __attribute__((__artificial__))
#define _no_instrument          __attribute__((__no_instrument_function__))
#define _no_asan                __attribute__((__no_address_safety_analysis__))
#define _no_ubsan               __attribute__((no_sanitize("undefined")))
#define _no_plt                 __attribute__((__noplt__))
#define _constructor(prio)      __attribute__((__constructor__(prio)))
#define _destructor(prio)       __attribute__((__destructor__(prio)))
#define _ifunc_resolver(fn)     __attribute__((__ifunc__(#fn)))
#define _section(name)          __attribute__((__section__(name)))
#define _hot                    __attribute__((__hot__))
#define _cold                   __attribute__((__cold__))

#define _generic_target \
    __attribute__(( \
    __target__("no-sse3"), \
    __target__("no-ssse3"), \
    __target__("no-sse4.1"), \
    __target__("no-sse4.2"), \
    __target__("no-avx"), \
    __target__("no-avx2") \
    ))

#if 0
typedef __UINT64_TYPE__ uint64_t;
typedef __INT64_TYPE__ int64_t;
typedef __UINT32_TYPE__ uint32_t;
typedef __INT32_TYPE__ int32_t;
typedef __UINT16_TYPE__ uint16_t;
typedef __INT16_TYPE__ int16_t;
typedef __UINT8_TYPE__ uint8_t;
typedef __INT8_TYPE__ int8_t;

typedef __UINT_FAST64_TYPE__ uint_fast64_t;
typedef __INT_FAST64_TYPE__ int_fast64_t;
typedef __UINT_FAST32_TYPE__ uint_fast32_t;
typedef __INT_FAST32_TYPE__ int_fast32_t;
typedef __UINT_FAST16_TYPE__ uint_fast16_t;
typedef __INT_FAST16_TYPE__ int_fast16_t;
typedef __UINT_FAST8_TYPE__ uint_fast8_t;
typedef __INT_FAST8_TYPE__ int_fast8_t;

typedef __UINTPTR_TYPE__ uintptr_t;
typedef __INTPTR_TYPE__ intptr_t;

typedef int_fast64_t off_t;
typedef __SIZE_TYPE__ size_t;
typedef long ssize_t;
#endif

#endif

__extension__ typedef __int128 int128_t;
__extension__ typedef __int128 unsigned uint128_t;

typedef int_fast64_t off_t;
typedef intptr_t ssize_t;
typedef decltype(nullptr) nullptr_t;

/// SSE vectors
typedef int8_t _vector_size(16) __i8_vec16;
typedef int16_t _vector_size(16) __i16_vec8;
typedef int32_t _vector_size(16) __i32_vec4;
typedef int64_t _vector_size(16) __i64_vec2;
typedef uint8_t _vector_size(16) __u8_vec16;
typedef uint16_t _vector_size(16) __u16_vec8;
typedef uint32_t _vector_size(16) __u32_vec4;
typedef uint64_t _vector_size(16) __u64_vec2;
typedef float _vector_size(16) __f32_vec4;
typedef double _vector_size(16) __d64_vec2;

// Some builtins unnecessarily insist on long long types
typedef long long _vector_size(16) __i64_vec2LL;
typedef unsigned long long _vector_size(16) __ivec2ULL;

template<typename T, size_t N>
inline _no_instrument
constexpr size_t countof(T const (&)[N]) noexcept
{
    return N;
}

//#define countof(arr) (sizeof((arr))/sizeof(*(arr)))

typedef int pid_t;

struct process_t;
