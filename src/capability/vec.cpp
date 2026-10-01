// Copyright (c) 2026 AlexisVon
// SPDX-License-Identifier: Apache-2.0

#include "vec.h"

#include <algorithm>

#include "config.h" // the module switch (src/config.h)

#if ALXRUN_VEC_ENABLE

namespace capability {

    // Same-type equality (int / float / string / bool).
    static bool veq(const alx::variant& _a, const alx::variant& _b) {
        if (_a.is<alx::int_64>() && _b.is<alx::int_64>()) return _a.to<alx::int_64>() == _b.to<alx::int_64>();
        if (_a.is<double>() && _b.is<double>()) return _a.to<double>() == _b.to<double>();
        if (_a.is<std::string>() && _b.is<std::string>()) return _a.to<std::string>() == _b.to<std::string>();
        if (_a.is<bool>() && _b.is<bool>()) return _a.to<bool>() == _b.to<bool>();
        return false;
    }

    void vec_size(alx::script::fwrap& _fw) {
        const auto& v = as_vec(_fw[0], "vec_size vec");
        _fw.freturn(alx::variant(static_cast<alx::int_64>(v.size())));
    }

    void vec_find(alx::script::fwrap& _fw) {
        const auto& v = as_vec(_fw[0], "vec_find vec");
        if (_fw.size() < 2)
            throw alx::script::script_exception{alx::script::error_type::ArgError, "vec_find: expected a value"};
        for (size_t i = 0; i < v.size(); ++i)
            if (veq(v[i], _fw[1])) {
                _fw.freturn(alx::variant(static_cast<alx::int_64>(i)));
                return;
            }
        _fw.freturn(alx::variant(static_cast<alx::int_64>(-1)));
    }

    void vec_has(alx::script::fwrap& _fw) {
        const auto& v = as_vec(_fw[0], "vec_has vec");
        if (_fw.size() < 2)
            throw alx::script::script_exception{alx::script::error_type::ArgError, "vec_has: expected a value"};
        for (const auto& e : v)
            if (veq(e, _fw[1])) {
                _fw.freturn(alx::variant(true));
                return;
            }
        _fw.freturn(alx::variant(false));
    }

    // Not in AScript: appending had only the v[null] = x shorthand, and agents write
    // the obvious function name instead (same call as $str_size).
    void vec_push(alx::script::fwrap& _fw) {
        auto& v = as_vec(_fw[0], "vec_push vec");
        if (_fw.size() < 2)
            throw alx::script::script_exception{alx::script::error_type::ArgError, "vec_push: expected a value"};
        v.push_back(_fw[1]);
        _fw.freturn();
    }

    // pos is clamped into [0, size]: inserting at or past the end appends.
    void vec_insert(alx::script::fwrap& _fw) {
        auto& v = as_vec(_fw[0], "vec_insert vec");
        if (_fw.size() < 3)
            throw alx::script::script_exception{alx::script::error_type::ArgError,
                                                "vec_insert: expected position and value"};
        alx::int_64 pos = as_int(_fw[1], "vec_insert pos");
        if (pos < 0) pos = 0;
        if (pos > static_cast<alx::int_64>(v.size())) pos = static_cast<alx::int_64>(v.size());
        v.insert(v.begin() + static_cast<size_t>(pos), _fw[2]);
        _fw.freturn();
    }

    // Shared by $vec_sort and $vec_sort_desc; unlike-typed pairs compare equal, so a mixed vec
    // keeps its relative order there.
    static void sort_vec(alx::varvec& _v, bool _desc) {
        std::sort(_v.begin(), _v.end(), [_desc](const alx::variant& x, const alx::variant& y) {
            if (x.is<alx::int_64>() && y.is<alx::int_64>())
                return _desc ? x.to<alx::int_64>() > y.to<alx::int_64>() : x.to<alx::int_64>() < y.to<alx::int_64>();
            if (x.is<double>() && y.is<double>())
                return _desc ? x.to<double>() > y.to<double>() : x.to<double>() < y.to<double>();
            if (x.is<std::string>() && y.is<std::string>())
                return _desc ? x.to<std::string>() > y.to<std::string>() : x.to<std::string>() < y.to<std::string>();
            return false;
        });
    }

    void vec_sort(alx::script::fwrap& _fw) {
        sort_vec(as_vec(_fw[0], "vec_sort vec"), false);
        _fw.freturn();
    }

    void vec_sort_desc(alx::script::fwrap& _fw) {
        sort_vec(as_vec(_fw[0], "vec_sort_desc vec"), true);
        _fw.freturn();
    }

    void vec_reverse(alx::script::fwrap& _fw) {
        auto& v = as_vec(_fw[0], "vec_reverse vec");
        std::reverse(v.begin(), v.end());
        _fw.freturn();
    }

    void vec_merge(alx::script::fwrap& _fw) {
        auto& v = as_vec(_fw[0], "vec_merge vec");
        const auto& other = as_vec(_fw[1], "vec_merge other");
        if (&other == &v) { // self-merge: snapshot, insert would invalidate the iterators
            const alx::varvec tmp = v;
            v.insert(v.end(), tmp.begin(), tmp.end());
        } else {
            v.insert(v.end(), other.begin(), other.end());
        }
        _fw.freturn();
    }

    // Same comparison rules as $vec_sort (int / double / string).
    static bool vless(const alx::variant& _a, const alx::variant& _b) {
        if (_a.is<alx::int_64>() && _b.is<alx::int_64>()) return _a.to<alx::int_64>() < _b.to<alx::int_64>();
        if (_a.is<double>() && _b.is<double>()) return _a.to<double>() < _b.to<double>();
        if (_a.is<std::string>() && _b.is<std::string>()) return _a.to<std::string>() < _b.to<std::string>();
        return false;
    }

    // Not in AScript: $math_max/$math_min take two scalars only, and reducing a whole vec
    // is the common case (agents otherwise sort and index, which is a leap they skip).
    void vec_max(alx::script::fwrap& _fw) {
        const auto& v = as_vec(_fw[0], "vec_max vec");
        if (v.empty()) {
            _fw.freturn();
            return;
        }
        size_t best = 0;
        for (size_t i = 1; i < v.size(); ++i)
            if (vless(v[best], v[i])) best = i;
        _fw.freturn(v[best]);
    }

    void vec_min(alx::script::fwrap& _fw) {
        const auto& v = as_vec(_fw[0], "vec_min vec");
        if (v.empty()) {
            _fw.freturn();
            return;
        }
        size_t best = 0;
        for (size_t i = 1; i < v.size(); ++i)
            if (vless(v[i], v[best])) best = i;
        _fw.freturn(v[best]);
    }

    // The form table (capability.h); ex lines are asserted by test/run.sh.
    const form s_vec[] = {
        {"vec_size(v: vec): int", R"EX(return $vec_size([1,2,3]); => 3)EX", vec_size, ""},
        {"vec_push(v: vec, val: any)", R"EX(var a = [1,2]; $vec_push(a, 3); return a; => [1,2,3])EX", vec_push, "-"},
        {"vec_find(v: vec, val: any): int", R"EX(return $vec_find([5,6,7], 6); => 1)EX", vec_find, ""},
        {"vec_find(v: vec, val: any): int", R"EX(return $vec_find([5,6], 9); => -1)EX", vec_find, ""},
        {"vec_has(v: vec, val: any): bool", R"EX(return $vec_has([5,6], 9); => false)EX", vec_has, ""},
        {"vec_insert(v: vec, pos: int, val: any)", R"EX(var a = [1,3]; $vec_insert(a, 1, 2); return a; => [1,2,3])EX", vec_insert, ""},
        {"vec_sort(v: vec)", R"EX(var a = [3,1,2]; $vec_sort(a); return a; => [1,2,3])EX", vec_sort, ""},
        // scalars only: unlike-typed pairs compare equal, so a nested vec comes back untouched
        {"vec_sort(v: vec)", R"EX(var v = [[2],[1]]; $vec_sort(v); return v; => [[2],[1]])EX", vec_sort, ""},
        {"vec_sort_desc(v: vec)", R"EX(var a = [3,1,2]; $vec_sort_desc(a); return a; => [3,2,1])EX", vec_sort_desc, ""},
        {"vec_reverse(v: vec)", R"EX(var a = [1,2,3]; $vec_reverse(a); return a; => [3,2,1])EX", vec_reverse, ""},
        {"vec_merge(v: vec, other: vec)", R"EX(var a = [1,2]; $vec_merge(a, [3,4]); return a; => [1,2,3,4])EX", vec_merge, ""},
        {"vec_max(v: vec): any|null", R"EX(return $vec_max([3,9,2]); => 9)EX", vec_max, "-"},
        {"vec_min(v: vec): any|null", R"EX(return $vec_min([3,9,2]); => 2)EX", vec_min, "-"},
    };

    const table& vec_table() {
        static const table t = {"vec", s_vec, sizeof(s_vec) / sizeof(s_vec[0])};
        return t;
    }

} // namespace capability

#endif // ALXRUN_VEC_ENABLE
