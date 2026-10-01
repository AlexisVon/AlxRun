// Copyright (c) 2026 AlexisVon
// SPDX-License-Identifier: Apache-2.0

#include "regex.h"

#include "aregex_pcre2.h"
#include "stop.h"

#include "config.h" // the module switch (src/config.h)

#if ALXRUN_REGEX_ENABLE

namespace capability {

    // One match is bounded by PCRE2's own default: 10^7 backtracking steps, ~0.14 s measured here.
    static constexpr alx::uint_64 kStepLimit = 10000000;

    // The wall, read per pattern item: find_all / replace_all over a big subject is ONE native call,
    // so this hook is the only checkpoint inside it (the kernel's watchdog raises the flag).
    static bool still_wanted(void*) {
        return !g_stop.load(std::memory_order_relaxed);
    }

    // An empty result cannot tell "no match" from "gave up": the engine reports which, and reporting
    // the second as the first is the silent wrong answer it (and this check) exists to prevent.
    static void check_status(const alx::regex_pcre2& _re, const std::string& _desc) {
        switch (_re.last_status()) {
        case alx::regex_pcre2::match_status::ok:
        case alx::regex_pcre2::match_status::no_match:
            return;
        case alx::regex_pcre2::match_status::limit:
            throw alx::script::script_exception{
                alx::script::error_type::ResourceError,
                _desc + ": backtracking step limit (" + std::to_string(kStepLimit) + ") reached"};
        case alx::regex_pcre2::match_status::interrupted:
            // The kernel rewrites this one when its watchdog was the reason the flag went up.
            throw alx::script::script_exception{alx::script::error_type::InterruptedError,
                                                "time budget exceeded"};
        default:
            throw alx::script::script_exception{
                alx::script::error_type::RuntimeError,
                _desc + ": regex engine error " + std::to_string(_re.last_error_code())};
        }
    }

    // Compile with a validity check: an invalid pattern is a loud error, not a non-match.
    static alx::regex_pcre2 make_re(alx::variant& _v, const std::string& _desc) {
        alx::regex_pcre2 re(as_str(_v, _desc));
        if (!re.is_valid())
            throw alx::script::script_exception{alx::script::error_type::RuntimeError,
                                                _desc + ": invalid regex pattern"};
        re.set_step_limit(kStepLimit);
        re.set_callout(&still_wanted, nullptr);
        return re;
    }

    void regex_valid(alx::script::fwrap& _fw) {
        alx::regex_pcre2 re(as_str(_fw[0], "regex_valid pattern"));
        _fw.freturn(alx::variant(re.is_valid()));
    }

    // Entire-string match, unlike $regex_find which searches anywhere.
    void regex_match(alx::script::fwrap& _fw) {
        auto re = make_re(_fw[0], "regex_match pattern");
        const bool hit = re.is_compliant(as_str(_fw[1], "regex_match s"));
        check_status(re, "regex_match");
        _fw.freturn(alx::variant(hit));
    }

    // No match yields null, an empty match ("x*" against "hello") yields "" -- the engine tells the
    // two apart now, and they must not look the same (same rule as fs.cpp's empty vs missing).
    void regex_find(alx::script::fwrap& _fw) {
        auto re = make_re(_fw[0], "regex_find pattern");
        const std::string hit = re.find(as_str(_fw[1], "regex_find s"));
        check_status(re, "regex_find");
        if (alx::regex_pcre2::match_status::no_match == re.last_status())
            _fw.freturn();
        else
            _fw.freturn(alx::variant(hit));
    }

    void regex_find_all(alx::script::fwrap& _fw) {
        auto re = make_re(_fw[0], "regex_find_all pattern");
        auto hits = re.find_all(as_str(_fw[1], "regex_find_all s"));
        check_status(re, "regex_find_all");
        alx::varvec v;
        for (auto& s : hits)
            v.push_back(alx::variant(std::move(s)));
        _fw.freturn(alx::variant(std::move(v)));
    }

    // Capture groups only -- the full match is not included. No match yields [].
    void regex_group(alx::script::fwrap& _fw) {
        auto re = make_re(_fw[0], "regex_group pattern");
        auto groups = re.match_group(as_str(_fw[1], "regex_group s"));
        check_status(re, "regex_group");
        alx::varvec v;
        for (size_t i = 1; i < groups.size(); ++i)
            v.push_back(alx::variant(std::move(groups[i])));
        _fw.freturn(alx::variant(std::move(v)));
    }

    // vec of matches, each a vec of its capture groups.
    void regex_group_all(alx::script::fwrap& _fw) {
        auto re = make_re(_fw[0], "regex_group_all pattern");
        auto& str = as_str(_fw[1], "regex_group_all s");
        auto first = re.match_group(str);
        check_status(re, "regex_group_all");
        if (first.empty()) {
            _fw.freturn(alx::variant(alx::varvec()));
            return;
        }
        const size_t stride = first.size(); // 1 + group count
        const size_t ngroups = stride - 1;
        auto all = re.match_group_all(str);
        check_status(re, "regex_group_all");

        alx::varvec outer;
        alx::varvec head;
        for (size_t j = 1; j < stride; ++j)
            head.push_back(alx::variant(std::move(first[j])));
        outer.push_back(alx::variant(std::move(head)));
        // all[] repeats the first match, so start past its stride
        for (size_t i = stride; i + ngroups < all.size(); i += stride) {
            alx::varvec group;
            for (size_t j = 1; j < stride; ++j)
                group.push_back(alx::variant(std::move(all[i + j])));
            outer.push_back(alx::variant(std::move(group)));
        }
        _fw.freturn(alx::variant(std::move(outer)));
    }

    void regex_replace(alx::script::fwrap& _fw) {
        auto re = make_re(_fw[0], "regex_replace pattern");
        std::string out = re.replace(as_str(_fw[1], "regex_replace s"), as_str(_fw[2], "regex_replace repl"));
        check_status(re, "regex_replace"); // e.g. a replacement naming a group the pattern has not got
        _fw.freturn(alx::variant(std::move(out)));
    }

    void regex_replace_all(alx::script::fwrap& _fw) {
        auto re = make_re(_fw[0], "regex_replace_all pattern");
        std::string out =
            re.replace_all(as_str(_fw[1], "regex_replace_all s"), as_str(_fw[2], "regex_replace_all repl"));
        check_status(re, "regex_replace_all");
        _fw.freturn(alx::variant(std::move(out)));
    }

    // Cut on every match, tail included -- $str_split's rule, so the two split forms read alike.
    // The offsets are what makes it exact: a zero-width match need not sit where the scan resumed.
    void regex_split(alx::script::fwrap& _fw) {
        auto re = make_re(_fw[0], "regex_split pattern");
        auto& str = as_str(_fw[1], "regex_split s");
        auto spans = re.find_all_spans(str);
        check_status(re, "regex_split");

        alx::varvec v;
        size_t cursor = 0;
        for (const auto& span : spans) {
            v.push_back(alx::variant(str.substr(cursor, span.first - cursor)));
            cursor = span.first + span.second;
        }
        v.push_back(alx::variant(str.substr(cursor)));
        _fw.freturn(alx::variant(std::move(v)));
    }

    // The form table (capability.h); ex lines are asserted by test/run.sh.
    const form s_regex[] = {
        {"regex_valid(pattern: string): bool", R"EX(return $regex_valid("[0-9]+"); => true)EX", regex_valid, "regx_valid"},
        {"regex_match(pattern: string, s: string): bool", R"EX(return $regex_match("[0-9]+", "123"); => true)EX", regex_match, "regx_match"},
        {"regex_match(pattern: string, s: string): bool", R"EX(return $regex_match("[0-9]+", "a12"); => false)EX", regex_match, "regx_match"},
        {"regex_match(pattern: string, s: string): bool", R"EX(return $regex_match("^(a+)+$", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaa!"); => !step limit)EX", regex_match, "regx_match"},
        {"regex_find(pattern: string, s: string): string|null", R"EX(return $regex_find("[0-9]+", "a12b34"); => "12")EX", regex_find, "regx_find"},
        {"regex_find(pattern: string, s: string): string|null", R"EX(return $regex_find("[0-9]+", "abc"); => null)EX", regex_find, "regx_find"},
        {"regex_find(pattern: string, s: string): string|null", R"EX(return $regex_find("x*", "abc"); => "")EX", regex_find, "regx_find"},
        {"regex_find_all(pattern: string, s: string): vec", R"EX(return $regex_find_all("[0-9]+", "a12b34"); => ["12","34"])EX", regex_find_all, "regx_find_all"},
        {"regex_group(pattern: string, s: string): vec", R"EX(return $regex_group("(\\d+)-(\\d+)", "12-34"); => ["12","34"])EX", regex_group, "regx_group"},
        {"regex_group_all(pattern: string, s: string): vec", R"EX(return $regex_group_all("(\\d)", "1 2"); => [["1"],["2"]])EX", regex_group_all, "regx_group_all"},
        {"regex_replace(pattern: string, s: string, repl: string): string", R"EX(return $regex_replace("[0-9]+", "a12b34", "#"); => "a#b34")EX", regex_replace, "regx_replace"},
        {"regex_replace_all(pattern: string, s: string, repl: string): string", R"EX(return $regex_replace_all("[0-9]+", "a12b34", "#"); => "a#b#")EX", regex_replace_all, "regx_replace_all"},
        {"regex_split(pattern: string, s: string): vec", R"EX(return $regex_split("[,;]", "a,b;c"); => ["a","b","c"])EX", regex_split, "regx_split"},
        {"regex_split(pattern: string, s: string): vec", R"EX(return $regex_split("[,;]", "a,"); => ["a",""])EX", regex_split, "regx_split"},
        {"regex_split(pattern: string, s: string): vec", R"EX(return $regex_split("x*", "abc"); => ["","a","b","c",""])EX", regex_split, "regx_split"},
    };

    const table& regex_table() {
        static const table t = {"regex", s_regex, sizeof(s_regex) / sizeof(s_regex[0])};
        return t;
    }

} // namespace capability

#endif // ALXRUN_REGEX_ENABLE
