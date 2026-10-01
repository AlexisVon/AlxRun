// Copyright (c) 2026 AlexisVon
// SPDX-License-Identifier: Apache-2.0

#include "str.h"

#include "astring.h"

#include <algorithm>
#include <cctype>

#include "config.h" // the module switch (src/config.h)

#if ALXRUN_STR_ENABLE

namespace capability {

    // Not in AScript: the language has a size syntax (v[null]) but strings reject it,
    // and no other route to a string's length exists.
    void str_size(alx::script::fwrap& _fw) {
        const auto& str = as_str(_fw[0], "str_size str");
        _fw.freturn(alx::variant(static_cast<alx::int_64>(str.size())));
    }

    void str_find(alx::script::fwrap& _fw) {
        const auto& str = as_str(_fw[0], "str_find str");
        const auto& tag = as_str(_fw[1], "str_find tag");
        alx::uint_64 from = _fw.size() > 2 ? static_cast<alx::uint_64>(as_int(_fw[2], "str_find from")) : 0;
        size_t pos = alx::strutil::find(str, tag, from);
        _fw.freturn(alx::variant(static_cast<alx::int_64>(pos == alx::uint_64_npos ? -1 : pos)));
    }

    void str_rfind(alx::script::fwrap& _fw) {
        const auto& str = as_str(_fw[0], "str_rfind str");
        const auto& tag = as_str(_fw[1], "str_rfind tag");
        size_t pos = alx::strutil::rfind(str, tag);
        _fw.freturn(alx::variant(static_cast<alx::int_64>(pos == alx::uint_64_npos ? -1 : pos)));
    }

    void str_left(alx::script::fwrap& _fw) {
        const auto& str = as_str(_fw[0], "str_left str");
        auto n = static_cast<size_t>(as_int(_fw[1], "str_left n"));
        if (n >= str.size())
            _fw.freturn(alx::variant(str));
        else
            _fw.freturn(alx::variant(str.substr(0, n)));
    }

    void str_right(alx::script::fwrap& _fw) {
        const auto& str = as_str(_fw[0], "str_right str");
        auto n = static_cast<size_t>(as_int(_fw[1], "str_right n"));
        if (n >= str.size())
            _fw.freturn(alx::variant(str));
        else
            _fw.freturn(alx::variant(str.substr(str.size() - n)));
    }

    void str_mid(alx::script::fwrap& _fw) {
        const auto& str = as_str(_fw[0], "str_mid str");
        auto pos = static_cast<size_t>(as_int(_fw[1], "str_mid pos"));
        if (pos >= str.size()) {
            _fw.freturn(alx::variant(std::string()));
            return;
        }
        if (_fw.size() > 2) {
            auto len = static_cast<size_t>(as_int(_fw[2], "str_mid len"));
            _fw.freturn(alx::variant(str.substr(pos, len)));
        } else {
            _fw.freturn(alx::variant(str.substr(pos)));
        }
    }

    void str_split(alx::script::fwrap& _fw) {
        const auto& str = as_str(_fw[0], "str_split str");
        const auto& delim = as_str(_fw[1], "str_split delim");
        auto lst = alx::strutil::split(str, delim);
        alx::varvec v;
        for (auto& s : lst)
            v.push_back(alx::variant(std::move(s)));
        _fw.freturn(alx::variant(std::move(v)));
    }

    // Vec only -- $lst_* is not shipped (design.md §6).
    void str_join(alx::script::fwrap& _fw) {
        const auto& parts = as_vec(_fw[0], "str_join parts");
        const auto& delim = as_str(_fw[1], "str_join delim");
        std::list<std::string> list;
        for (auto& v : parts)
            list.push_back(v.to<std::string>());
        _fw.freturn(alx::variant(alx::strutil::join(list, delim)));
    }

    void str_replace(alx::script::fwrap& _fw) {
        const auto& str = as_str(_fw[0], "str_replace str");
        const auto& old_s = as_str(_fw[1], "str_replace old");
        const auto& new_s = as_str(_fw[2], "str_replace new");
        if (old_s.empty()) {
            _fw.freturn(alx::variant(str));
            return;
        }
        std::string result;
        size_t pos = 0, last = 0;
        while ((pos = str.find(old_s, last)) != std::string::npos) {
            result.append(str, last, pos - last);
            result.append(new_s);
            last = pos + old_s.size();
        }
        result.append(str, last, std::string::npos);
        _fw.freturn(alx::variant(std::move(result)));
    }

    void str_upper(alx::script::fwrap& _fw) {
        std::string s = as_str(_fw[0], "str_upper str");
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::toupper(c); });
        _fw.freturn(alx::variant(std::move(s)));
    }

    void str_lower(alx::script::fwrap& _fw) {
        std::string s = as_str(_fw[0], "str_lower str");
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
        _fw.freturn(alx::variant(std::move(s)));
    }

    void str_trim(alx::script::fwrap& _fw) {
        std::string s = as_str(_fw[0], "str_trim str");
        std::string chars = _fw.size() > 1 ? as_str(_fw[1], "str_trim chars") : " \t\n\r";
        auto begin = s.find_first_not_of(chars);
        if (begin == std::string::npos) {
            _fw.freturn(alx::variant(std::string()));
            return;
        }
        auto end = s.find_last_not_of(chars);
        _fw.freturn(alx::variant(s.substr(begin, end - begin + 1)));
    }

    void str_starts(alx::script::fwrap& _fw) {
        const auto& str = as_str(_fw[0], "str_starts str");
        const auto& prefix = as_str(_fw[1], "str_starts prefix");
        _fw.freturn(alx::variant(str.size() >= prefix.size() && str.compare(0, prefix.size(), prefix) == 0));
    }

    void str_ends(alx::script::fwrap& _fw) {
        const auto& str = as_str(_fw[0], "str_ends str");
        const auto& suffix = as_str(_fw[1], "str_ends suffix");
        _fw.freturn(alx::variant(str.size() >= suffix.size() &&
                                 str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0));
    }

    void str_contains(alx::script::fwrap& _fw) {
        const auto& str = as_str(_fw[0], "str_contains str");
        const auto& sub = as_str(_fw[1], "str_contains sub");
        _fw.freturn(alx::variant(str.find(sub) != std::string::npos));
    }

    // The form table (capability.h); ex lines are asserted by test/run.sh.
    const form s_str[] = {
        {"str_size(s: string): int", R"EX(return $str_size("hello"); => 5)EX", str_size, "-"},
        {"str_find(s: string, sub: string, from?: int): int", R"EX(return $str_find("hello", "l"); => 2)EX", str_find, ""},
        {"str_find(s: string, sub: string, from?: int): int", R"EX(return $str_find("hello", "z"); => -1)EX", str_find, ""},
        {"str_rfind(s: string, sub: string): int", R"EX(return $str_rfind("hello", "l"); => 3)EX", str_rfind, ""},
        {"str_left(s: string, n: int): string", R"EX(return $str_left("hello", 2); => "he")EX", str_left, ""},
        {"str_right(s: string, n: int): string", R"EX(return $str_right("hello", 2); => "lo")EX", str_right, ""},
        {"str_mid(s: string, pos: int, len?: int): string", R"EX(return $str_mid("hello", 1, 3); => "ell")EX", str_mid, ""},
        // 0-based, unlike the %1-based fmt convention
        {"str_mid(s: string, pos: int, len?: int): string", R"EX(return $str_mid("hello", 0, 1); => "h")EX", str_mid, ""},
        {"str_split(s: string, delim: string): vec", R"EX(return $str_split("a,b,c", ","); => ["a","b","c"])EX", str_split, ""},
        {"str_split(s: string, delim: string): vec", R"EX(return $str_split("a,,b", ","); => ["a","","b"])EX", str_split, ""},
        {"str_split(s: string, delim: string): vec", R"EX(return $str_split("a,b,", ","); => ["a","b",""])EX", str_split, ""},
        {"str_join(parts: vec, delim: string): string", R"EX(return $str_join(["a","b"], "-"); => "a-b")EX", str_join, ""},
        {"str_replace(s: string, old: string, new: string): string", R"EX(return $str_replace("a-b", "-", "+"); => "a+b")EX", str_replace, ""},
        {"str_upper(s: string): string", R"EX(return $str_upper("ab"); => "AB")EX", str_upper, ""},
        {"str_lower(s: string): string", R"EX(return $str_lower("AB"); => "ab")EX", str_lower, ""},
        {"str_trim(s: string, chars?: string): string", R"EX(return $str_trim("  hi  "); => "hi")EX", str_trim, ""},
        {"str_starts(s: string, prefix: string): bool", R"EX(return $str_starts("hello", "he"); => true)EX", str_starts, ""},
        {"str_ends(s: string, suffix: string): bool", R"EX(return $str_ends("hello", "lo"); => true)EX", str_ends, ""},
        {"str_contains(s: string, sub: string): bool", R"EX(return $str_contains("hello", "ell"); => true)EX", str_contains, ""},
    };

    const table& str_table() {
        static const table t = {"str", s_str, sizeof(s_str) / sizeof(s_str[0])};
        return t;
    }

} // namespace capability

#endif // ALXRUN_STR_ENABLE
