// Copyright (c) 2026 AlexisVon
// SPDX-License-Identifier: Apache-2.0

#include "map.h"

#include "config.h" // the module switch (src/config.h)

#if ALXRUN_MAP_ENABLE

namespace capability {

    void map_keys(alx::script::fwrap& _fw) {
        const auto& m = as_map(_fw[0], "map_keys map");
        alx::varvec out;
        for (auto it = m.begin(); it != m.end(); ++it)
            out.push_back(alx::variant(it.key()));
        _fw.freturn(alx::variant(std::move(out)));
    }

    void map_values(alx::script::fwrap& _fw) {
        const auto& m = as_map(_fw[0], "map_values map");
        alx::varvec out;
        for (auto it = m.begin(); it != m.end(); ++it)
            out.push_back(it.value());
        _fw.freturn(alx::variant(std::move(out)));
    }

    void map_has(alx::script::fwrap& _fw) {
        const auto& m = as_map(_fw[0], "map_has map");
        _fw.freturn(alx::variant(m.contain(as_str(_fw[1], "map_has key"))));
    }

    // Missing key: the default if given, else null -- reading m[k] directly raises KeyError.
    void map_get(alx::script::fwrap& _fw) {
        const auto& m = as_map(_fw[0], "map_get map");
        const auto& key = as_str(_fw[1], "map_get key");
        if (m.contain(key)) {
            _fw.freturn(m.value(key));
        } else if (_fw.size() > 2) {
            _fw.freturn(_fw[2]);
        } else {
            _fw.freturn();
        }
    }

    void map_size(alx::script::fwrap& _fw) {
        const auto& m = as_map(_fw[0], "map_size map");
        _fw.freturn(alx::variant(static_cast<alx::int_64>(m.size())));
    }

    // Keys present in both maps take the other map's value.
    void map_merge(alx::script::fwrap& _fw) {
        auto& m = as_map(_fw[0], "map_merge map");
        const auto& other = as_map(_fw[1], "map_merge other");
        if (&other == &m) {
            _fw.freturn();
            return;
        }
        for (auto it = other.begin(); it != other.end(); ++it)
            m[it.key()] = it.value();
        _fw.freturn();
    }

    // The form table (capability.h); ex lines are asserted by test/run.sh.
    const form s_map[] = {
        {"map_keys(m: map): vec", R"EX(var ks = $map_keys(map{"a": 1}); return ks[0]; => "a")EX", map_keys, ""},
        // sorted, not insertion order: the literal below is written b, a
        {"map_keys(m: map): vec", R"EX(var ks = $map_keys(map{"b": 1, "a": 2}); return ks; => ["a","b"])EX", map_keys, ""},
        {"map_values(m: map): vec", R"EX(var vs = $map_values(map{"a": 7}); return vs[0]; => 7)EX", map_values, ""},
        {"map_has(m: map, key: string): bool", R"EX(return $map_has(map{"a": 1}, "b"); => false)EX", map_has, ""},
        {"map_get(m: map, key: string, def?: any): any", R"EX(return $map_get(map{"a": 1}, "b", 0); => 0)EX", map_get, ""},
        {"map_size(m: map): int", R"EX(return $map_size(map{"a": 1, "b": 2}); => 2)EX", map_size, ""},
        {"map_merge(m: map, other: map)", R"EX(var m = map{"a": 1}; $map_merge(m, map{"b": 2}); return $map_get(m, "b"); => 2)EX", map_merge, ""},
    };

    const table& map_table() {
        static const table t = {"map", s_map, sizeof(s_map) / sizeof(s_map[0])};
        return t;
    }

} // namespace capability

#endif // ALXRUN_MAP_ENABLE
