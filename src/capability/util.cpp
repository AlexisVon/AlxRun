// Copyright (c) 2026 AlexisVon
// SPDX-License-Identifier: Apache-2.0

#include "util.h"

#include <cmath>
#include <ctime>
#include <random>
#include <string>

#include "config.h" // the module switch (src/config.h)

#if ALXRUN_UTIL_ENABLE

namespace capability {

    namespace {

        // One clock read, both forms of it: every map below is built from this pair, so the fields
        // and the stamp in one map can never come from two different instants.
        bool now(timespec& _ts, struct tm& _tm) {
            if (::clock_gettime(CLOCK_REALTIME, &_ts) != 0) return false;
            return ::localtime_r(&_ts.tv_sec, &_tm) != nullptr;
        }

        [[noreturn]] void no_clock() {
            throw alx::script::script_exception{alx::script::error_type::RuntimeError,
                                                "the system clock is not available"};
        }

        alx::int_64 stamp_of(const timespec& _ts) {
            return static_cast<alx::int_64>(_ts.tv_sec) * 1000000 + _ts.tv_nsec / 1000;
        }

        void put(alx::varmap& _m, const char* _key, alx::int_64 _v) {
            _m[_key] = alx::variant(_v);
        }

        // Which half of the map a caller asked for. The projections are the same code with a
        // narrower part, so "$date() is $datetime()'s date half" is a construction, not a promise.
        enum class part { whole,
                          date_only,
                          time_only };

        alx::varmap fields(const timespec& _ts, const struct tm& _tm, part _what) {
            alx::varmap m;
            if (_what != part::time_only) {
                put(m, "year", _tm.tm_year + 1900);
                put(m, "month", _tm.tm_mon + 1);
                put(m, "day", _tm.tm_mday);
                put(m, "week", _tm.tm_wday); // 0 = Sunday, the C library's convention (asserted)
            }
            if (_what != part::date_only) {
                put(m, "h", _tm.tm_hour);
                put(m, "m", _tm.tm_min);
                put(m, "s", _tm.tm_sec);
                put(m, "ms", (_ts.tv_nsec / 1000000) % 1000); // digit groups, never totals
                put(m, "us", (_ts.tv_nsec / 1000) % 1000);
            }
            if (_what == part::whole) put(m, "stamp_us", stamp_of(_ts));
            return m;
        }

        double as_number(alx::variant& _v, const std::string& _desc) {
            if (_v.is<double>()) return _v.to<double>();
            if (_v.is<alx::int_64>()) return static_cast<double>(_v.to<alx::int_64>());
            throw alx::script::script_exception{alx::script::error_type::TypeError, _desc + ": expected number"};
        }

        // One generator per process, seeded once from the clock. Nothing hands the seed out: the
        // sequence is not a value to reproduce, so the assertions pin the properties, not the numbers.
        std::mt19937_64& rng() {
            static std::mt19937_64 g = [] {
                std::random_device rd;
                const alx::uint_64 a = static_cast<alx::uint_64>(rd());
                const alx::uint_64 b = static_cast<alx::uint_64>(rd());
                return std::mt19937_64((a << 32) ^ b ^ static_cast<alx::uint_64>(::time(nullptr)));
            }();
            return g;
        }

    } // namespace

    void timestamp(alx::script::fwrap& _fw) {
        timespec ts{};
        struct tm tm{};
        if (!now(ts, tm)) no_clock();
        _fw.freturn(alx::variant(stamp_of(ts)));
    }

    void datetime(alx::script::fwrap& _fw) {
        timespec ts{};
        struct tm tm{};
        if (!now(ts, tm)) no_clock();
        _fw.freturn(alx::variant(fields(ts, tm, part::whole)));
    }

    void date(alx::script::fwrap& _fw) {
        timespec ts{};
        struct tm tm{};
        if (!now(ts, tm)) no_clock();
        _fw.freturn(alx::variant(fields(ts, tm, part::date_only)));
    }

    void time(alx::script::fwrap& _fw) {
        timespec ts{};
        struct tm tm{};
        if (!now(ts, tm)) no_clock();
        _fw.freturn(alx::variant(fields(ts, tm, part::time_only)));
    }

    void rand(alx::script::fwrap& _fw) {
        if (_fw.size() != 2)
            throw alx::script::script_exception{
                alx::script::error_type::ArgError,
                "rand: expected 2 arguments, got " + std::to_string(_fw.size())};
        const bool ints = !_fw[0].is<double>() && !_fw[1].is<double>();
        if (ints) {
            const alx::int_64 a = as_int(_fw[0], "rand a");
            const alx::int_64 b = as_int(_fw[1], "rand b");
            if (a > b)
                throw alx::script::script_exception{alx::script::error_type::ArgError,
                                                    "rand: a must not exceed b"};
            std::uniform_int_distribution<alx::int_64> d(a, b); // no modulo bias: the distribution knows
            _fw.freturn(alx::variant(d(rng())));
            return;
        }
        const double a = as_number(_fw[0], "rand a");
        const double b = as_number(_fw[1], "rand b");
        // !(a <= b) rather than a > b so that NaN falls in here too: every comparison against NaN is
        // false, and a NaN bound would otherwise sail through and come back as a silent null.
        if (!std::isfinite(a) || !std::isfinite(b) || !(a <= b))
            throw alx::script::script_exception{alx::script::error_type::ArgError,
                                                "rand: a and b must be finite, with a <= b"};
        if (a == b) { // a degenerate span: both ends are the same value
            _fw.freturn(alx::variant(a));
            return;
        }
        if (!std::isfinite(b - a)) // the span itself overflows (the distribution would return inf)
            throw alx::script::script_exception{alx::script::error_type::ArgError,
                                                "rand: the span b - a is too wide"};
        std::uniform_real_distribution<double> d(a, b);
        _fw.freturn(alx::variant(d(rng())));
    }

    // The form table (capability.h); ex lines are asserted by test/run.sh.
    const form s_util[] = {
        {"timestamp(): int (us since epoch)", R"EX(return $timestamp() > 1700000000000000; => true)EX", timestamp, "-"},
        {"datetime(): map", R"EX(var d = $datetime(); return d.stamp_us % 1000 == d.us && d.stamp_us / 1000 % 1000 == d.ms; => true)EX", datetime, "-"},
        {"datetime(): map", R"EX(var d = $datetime(); return d.month >= 1 && d.month <= 12 && d.week >= 0 && d.week <= 6 && d.us < 1000; => true)EX", datetime, "-"},
        {"date(): map", R"EX(var d = $date(); return d.year > 2020 && d.month >= 1 && d.day >= 1; => true)EX", date, "-"},
        {"time(): map", R"EX(var t = $time(); return t.h < 24 && t.m < 60 && t.s < 60 && t.ms < 1000; => true)EX", time, "-"},
        {"rand(a: int|float, b: int|float): int|float", R"EX(return $rand(5, 5); => 5)EX", rand, "-"},
        {"rand(a: int|float, b: int|float): int|float", R"EX(var z = 0; var o = 0; var i = 0; while (i < 200) { if ($rand(0, 1) == 0) { z = 1; } else { o = 1; } i = i + 1; } return z + o; => 2)EX", rand, "-"},
        {"rand(a: int|float, b: int|float): int|float", R"EX(var f = $rand(0.0, 1.0); return f >= 0.0 && f < 1.0; => true)EX", rand, "-"},
        {"rand(a: int|float, b: int|float): int|float", R"EX(return $rand(3, 1); => !a must not exceed b)EX", rand, "-"},
    };

    const table& util_table() {
        static const table t = {"util", s_util, sizeof(s_util) / sizeof(s_util[0]), clan_util};
        return t;
    }

} // namespace capability

#endif // ALXRUN_UTIL_ENABLE
