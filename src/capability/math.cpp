// Copyright (c) 2026 AlexisVon
// SPDX-License-Identifier: Apache-2.0

#include "math.h"

#include <cmath>

#include "config.h" // the module switch (src/config.h)

#if ALXRUN_MATH_ENABLE

namespace capability {

    // Arity check: raises ArgError on a mismatch.
    static void arg_count(alx::script::fwrap& _fw, size_t _n, const char* _name) {
        if (_fw.size() != _n)
            throw alx::script::script_exception{
                alx::script::error_type::ArgError,
                std::string("math_") + _name + ": expected " + std::to_string(_n) + " argument(s), got " +
                    std::to_string(_fw.size())};
    }

    inline double as_number(alx::variant& _v, const std::string& _desc) {
        if (_v.is<double>()) return _v.to<double>();
        if (_v.is<alx::int_64>()) return static_cast<double>(_v.to<alx::int_64>());
        throw alx::script::script_exception{alx::script::error_type::TypeError, _desc + ": expected number"};
    }

#    define MATH_FN1(_name, _expr)                                                    \
        void math_##_name(alx::script::fwrap& _fw) {                                  \
            arg_count(_fw, 1, #_name);                                                \
            _fw.freturn(alx::variant(_expr(as_number(_fw[0], "math_" #_name " x")))); \
        }
#    define MATH_FN2(_name, _expr)                                                    \
        void math_##_name(alx::script::fwrap& _fw) {                                  \
            arg_count(_fw, 2, #_name);                                                \
            _fw.freturn(alx::variant(_expr(as_number(_fw[0], "math_" #_name " a"),    \
                                           as_number(_fw[1], "math_" #_name " b")))); \
        }

    MATH_FN1(abs, std::fabs)
    MATH_FN1(sqrt, std::sqrt)
    MATH_FN1(cbrt, std::cbrt)
    MATH_FN1(exp, std::exp)
    MATH_FN1(ln, std::log)
    MATH_FN1(log2, std::log2)
    MATH_FN1(log10, std::log10)
    MATH_FN1(sin, std::sin)
    MATH_FN1(cos, std::cos)
    MATH_FN1(tan, std::tan)
    MATH_FN1(asin, std::asin)
    MATH_FN1(acos, std::acos)
    MATH_FN1(atan, std::atan)
    MATH_FN1(floor, std::floor)
    MATH_FN1(ceil, std::ceil)
    MATH_FN1(round, std::round)
    MATH_FN1(trunc, std::trunc)
    MATH_FN1(sign, [](double _x) { return _x > 0 ? 1.0 : (_x < 0 ? -1.0 : 0.0); })
    MATH_FN1(rad, [](double _d) { return _d * 3.141592653589793 / 180.0; })
    MATH_FN1(deg, [](double _r) { return _r * 180.0 / 3.141592653589793; })

    MATH_FN2(min, std::fmin)
    MATH_FN2(max, std::fmax)
    MATH_FN2(pow, std::pow)
    MATH_FN2(atan2, std::atan2)
    MATH_FN2(hypot, std::hypot)
    MATH_FN2(fmod, std::fmod)

    void math_clamp(alx::script::fwrap& _fw) {
        arg_count(_fw, 3, "clamp");
        const double v = as_number(_fw[0], "math_clamp v");
        const double lo = as_number(_fw[1], "math_clamp lo");
        const double hi = as_number(_fw[2], "math_clamp hi");
        _fw.freturn(alx::variant(std::fmin(std::fmax(v, lo), hi)));
    }

#    undef MATH_FN1
#    undef MATH_FN2

    // The form table (capability.h); ex lines are asserted by test/run.sh.
    const form s_math[] = {
        {"math_abs(x: number): float", R"EX(return $math_abs(-3); => 3.0)EX", math_abs, ""},
        {"math_sqrt(x: number): float", R"EX(return $math_sqrt(9); => 3.0)EX", math_sqrt, ""},
        {"math_cbrt(x: number): float", R"EX(return $math_cbrt(1); => 1.0)EX", math_cbrt, ""},
        {"math_exp(x: number): float", R"EX(return $math_exp(0); => 1.0)EX", math_exp, ""},
        {"math_ln(x: number): float", R"EX(return $math_ln(1); => 0.0)EX", math_ln, ""},
        {"math_log2(x: number): float", R"EX(return $math_log2(8); => 3.0)EX", math_log2, ""},
        {"math_log10(x: number): float", R"EX(return $math_log10(1000); => 3.0)EX", math_log10, ""},
        {"math_sin(x: number): float", R"EX(return $math_sin(0); => 0.0)EX", math_sin, ""},
        {"math_cos(x: number): float", R"EX(return $math_cos(0); => 1.0)EX", math_cos, ""},
        {"math_tan(x: number): float", R"EX(return $math_tan(0); => 0.0)EX", math_tan, ""},
        {"math_asin(x: number): float", R"EX(return $math_asin(0); => 0.0)EX", math_asin, ""},
        {"math_acos(x: number): float", R"EX(return $math_acos(1); => 0.0)EX", math_acos, ""},
        {"math_atan(x: number): float", R"EX(return $math_atan(0); => 0.0)EX", math_atan, ""},
        {"math_floor(x: number): float", R"EX(return $math_floor(2.7); => 2.0)EX", math_floor, ""},
        {"math_ceil(x: number): float", R"EX(return $math_ceil(2.1); => 3.0)EX", math_ceil, ""},
        {"math_round(x: number): float", R"EX(return $math_round(2.5); => 3.0)EX", math_round, ""},
        {"math_trunc(x: number): float", R"EX(return $math_trunc(2.9); => 2.0)EX", math_trunc, ""},
        {"math_sign(x: number): float", R"EX(return $math_sign(-5); => -1.0)EX", math_sign, ""},
        {"math_rad(deg: number): float", R"EX(return $math_rad(0); => 0.0)EX", math_rad, ""},
        {"math_deg(rad: number): float", R"EX(return $math_deg(0); => 0.0)EX", math_deg, ""},
        {"math_min(a: number, b: number): float", R"EX(return $math_min(3, 7); => 3.0)EX", math_min, ""},
        {"math_max(a: number, b: number): float", R"EX(return $math_max(3, 7); => 7.0)EX", math_max, ""},
        {"math_pow(a: number, b: number): float", R"EX(return $math_pow(2, 10); => 1024.0)EX", math_pow, ""},
        {"math_atan2(y: number, x: number): float", R"EX(return $math_atan2(0, 1); => 0.0)EX", math_atan2, ""},
        {"math_hypot(a: number, b: number): float", R"EX(return $math_hypot(3, 4); => 5.0)EX", math_hypot, ""},
        {"math_fmod(a: number, b: number): float", R"EX(return $math_fmod(7, 3); => 1.0)EX", math_fmod, ""},
        {"math_clamp(v: number, lo: number, hi: number): float", R"EX(return $math_clamp(9, 0, 5); => 5.0)EX", math_clamp, ""},
    };

    const table& math_table() {
        static const table t = {"math", s_math, sizeof(s_math) / sizeof(s_math[0])};
        return t;
    }

} // namespace capability

#endif // ALXRUN_MATH_ENABLE
