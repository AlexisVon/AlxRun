/*****************************************************************/ /**
 * \file   abase.h
 * \brief  Basic types and macro definitions
 *
 * \author alexis
 *
 * Copyright (c) 2026 AlexisVon
 * SPDX-License-Identifier: MIT
 *********************************************************************/

#ifndef _ALEXIS_BASE_H_
#define _ALEXIS_BASE_H_

/// 1 = the library is linked into the caller rather than loaded as a shared object, which makes
/// the ALX*_API macros below empty; 0 (the default set here) = shared modules
#ifndef ALEXISLIB_STATIC
#    define ALEXISLIB_STATIC false
#endif

/**
 * \brief Export markers of the four modules
 *
 * Building a module as a shared object defines its own marker (ALXBASELIB_EXPORTS for alxbase, and
 * one per other module) and its symbols become dllexport on Windows, dllimport for the consumer;
 * elsewhere they are marked visibility("default"). All four are empty under ALEXISLIB_STATIC.
 */
#if ALEXISLIB_STATIC
/// Export marker for the alxbase API
#    define ALXBASE_API
/// Export marker for the alxcore API
#    define ALXCORE_API
/// Export marker for the alxcomm API
#    define ALXCOMM_API
/// Export marker for the alxscpt API
#    define ALXSCPT_API
#else
#    ifdef _WIN32
#        ifdef ALXBASELIB_EXPORTS
#            define ALXBASE_API __declspec(dllexport)
#        else
#            define ALXBASE_API __declspec(dllimport)
#        endif
#        ifdef ALXCORELIB_EXPORTS
#            define ALXCORE_API __declspec(dllexport)
#        else
#            define ALXCORE_API __declspec(dllimport)
#        endif
#        ifdef ALXCOMMLIB_EXPORTS
#            define ALXCOMM_API __declspec(dllexport)
#        else
#            define ALXCOMM_API __declspec(dllimport)
#        endif
#        ifdef ALXSCPTLIB_EXPORTS
#            define ALXSCPT_API __declspec(dllexport)
#        else
#            define ALXSCPT_API __declspec(dllimport)
#        endif
#    else
#        define ALXBASE_API __attribute__((visibility("default")))
#        define ALXCORE_API __attribute__((visibility("default")))
#        define ALXCOMM_API __attribute__((visibility("default")))
#        define ALXSCPT_API __attribute__((visibility("default")))
#    endif
#endif

/// Language standard in use, spelled like __cplusplus: MSVC reports it through _MSVC_LANG, and
/// any other compiler leaves CPP_VERSION undefined -- no CPP_XX macro is defined then either
#ifdef _MSC_VER
#    define CPP_VERSION _MSVC_LANG
#elif defined __GNUC__ || defined __clang__
#    define CPP_VERSION __cplusplus
#endif

/// CPP_VERSION when the translation unit is compiled as C++98
#define CPP_98_ID 199711L
/// CPP_VERSION when the translation unit is compiled as C++11
#define CPP_11_ID 201103L
/// CPP_VERSION when the translation unit is compiled as C++14
#define CPP_14_ID 201402L
/// CPP_VERSION when the translation unit is compiled as C++17
#define CPP_17_ID 201703L
/// CPP_VERSION when the translation unit is compiled as C++20
#define CPP_20_ID 202002L
/// Lowest CPP_VERSION that reports C++23 or later
#define CPP_23_ID 202302L

#ifdef CPP_VERSION
#    if CPP_VERSION == CPP_98_ID
/// Compiling as C++98
#        define CPP_98
#    elif CPP_VERSION == CPP_11_ID
/// Compiling as C++11
#        define CPP_11
#    elif CPP_VERSION == CPP_14_ID
/// Compiling as C++14
#        define CPP_14
#    elif CPP_VERSION == CPP_17_ID
/// Compiling as C++17
#        define CPP_17
#    elif CPP_VERSION == CPP_20_ID
/// Compiling as C++20
#        define CPP_20
#    elif CPP_VERSION >= CPP_23_ID
/// Compiling as C++23 or later
#        define CPP_23
#    endif
#endif

/// Byte offset of _MEMBER_ inside _CLASS_, as a size_t; no object of the class is needed
#define _ALEXIS_OFFSET_OF_(_CLASS_, _MEMBER_) \
    ((size_t) ((char*) &((_CLASS_*) nullptr)->_MEMBER_ - (char*) (_CLASS_*) nullptr))

namespace alx {

    /// 8-bit signed integer; plain char, whose signedness is implementation-defined
    typedef char int_8;
    /// 16-bit signed integer
    typedef short int_16;
    /// 32-bit signed integer
    typedef int int_32;
    /// 64-bit signed integer
    typedef long long int_64;

    /// 8-bit unsigned integer
    typedef unsigned char uint_8;
    /// 16-bit unsigned integer
    typedef unsigned short uint_16;
    /// 32-bit unsigned integer
    typedef unsigned int uint_32;
    /// 64-bit unsigned integer
    typedef unsigned long long uint_64;

    /// 32-bit floating point
    typedef float real_32;
    /// 64-bit floating point
    typedef double real_64;

    /// Largest value an int_8 holds
    constexpr int_8 max_int_8_ = 0X7F;
    /// Largest value an int_16 holds
    constexpr int_16 max_int_16 = 0X7FFF;
    /// Largest value an int_32 holds
    constexpr int_32 max_int_32 = 0X7FFFFFFF;
    /// Largest value an int_64 holds
    constexpr int_64 max_int_64 = 0X7FFFFFFFFFFFFFFF;

    /// Largest value a uint_8 holds
    constexpr uint_8 max_uint_8_ = 0XFFU;
    /// Largest value a uint_16 holds
    constexpr uint_16 max_uint_16 = 0XFFFFU;
    /// Largest value a uint_32 holds
    constexpr uint_32 max_uint_32 = 0XFFFFFFFFU;
    /// Largest value a uint_64 holds
    constexpr uint_64 max_uint_64 = 0XFFFFFFFFFFFFFFFFU;

    /// Largest finite real_32, i.e. FLT_MAX
    constexpr real_32 max_real_32 = 0X1.FFFFFEP127f;
    /// Largest finite real_64, i.e. DBL_MAX
    constexpr real_64 max_real_64 = 0X1.FFFFFFFFFFFFFP1023;

    /// Smallest value an int_8 holds: -max_int_8_ - 1
    constexpr int_8 min_int_8_ = ~max_int_8_;
    /// Smallest value an int_16 holds: -max_int_16 - 1
    constexpr int_16 min_int_16 = ~max_int_16;
    /// Smallest value an int_32 holds: -max_int_32 - 1
    constexpr int_32 min_int_32 = ~max_int_32;
    /// Smallest value an int_64 holds: -max_int_64 - 1
    constexpr int_64 min_int_64 = ~max_int_64;

    /// Smallest value a uint_8 holds: 0
    constexpr uint_8 min_uint_8_ = 0X0U;
    /// Smallest value a uint_16 holds: 0
    constexpr uint_16 min_uint_16 = 0X0U;
    /// Smallest value a uint_32 holds: 0
    constexpr uint_32 min_uint_32 = 0X0U;
    /// Smallest value a uint_64 holds: 0
    constexpr uint_64 min_uint_64 = 0X0U;

    /// Most negative finite real_32, i.e. -FLT_MAX; -infinity is not it
    constexpr real_32 min_real_32 = -max_real_32;
    /// Most negative finite real_64, i.e. -DBL_MAX; -infinity is not it
    constexpr real_64 min_real_64 = -max_real_64;

    /// Not-found sentinel for uint_8: max_uint_8_, a value no valid index can hold
    constexpr uint_8 uint_8_npos = max_uint_8_;
    /// Not-found sentinel for uint_16: max_uint_16, a value no valid index can hold
    constexpr uint_16 uint_16_npos = max_uint_16;
    /// Not-found sentinel for uint_32: max_uint_32, a value no valid index can hold
    constexpr uint_32 uint_32_npos = max_uint_32;
    /**
     * \brief Not-found sentinel for uint_64, and the "no bound" argument of the ranged helpers
     *
     * max_uint_64, a value no valid index can hold. The search helpers return it when nothing
     * matched; the ranged ones take it as a bound that is not there -- read the rest of the
     * stream, up to the terminating NUL, or do not seek at all.
     */
    constexpr uint_64 uint_64_npos = max_uint_64;
}

#endif
