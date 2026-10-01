// Copyright (c) 2026 AlexisVon
// SPDX-License-Identifier: Apache-2.0

#include "json.h"

#include "ajson.h"

#include "config.h" // the module switch (src/config.h)

#if ALXRUN_JSON_ENABLE

namespace capability {

    // Serialize a value; handles are not serializable, so report that instead of crashing.
    static std::string to_json_text(const alx::variant& _v, bool _compact) {
        try {
            return alx::json_doc::to_string(alx::json_value::from_variant(_v), _compact);
        } catch (...) {
            throw alx::script::script_exception{alx::script::error_type::TypeError,
                                                "value is not serializable to JSON"};
        }
    }

    // Raises on bad JSON. Returning null (what AScript's io_fmjs does) is the failure shape
    // prelude.md §2.6 warns about: the script keeps computing on a null and looks fine.
    void json_parse(alx::script::fwrap& _fw) {
        const auto& text = as_str(_fw[0], "json_parse str");
        bool ok = false;
        alx::json_value v = alx::json_doc::from_value(text, &ok);
        if (!ok) {
            std::string head = text.size() > 40 ? text.substr(0, 40) + "..." : text;
            throw alx::script::script_exception{alx::script::error_type::ParseError,
                                                "json_parse: invalid JSON — " + head};
        }
        _fw.freturn(v.to_variant());
    }

    void json_str(alx::script::fwrap& _fw) {
        _fw.freturn(alx::variant(to_json_text(_fw[0], false)));
    }

    void json_compact(alx::script::fwrap& _fw) {
        _fw.freturn(alx::variant(to_json_text(_fw[0], true)));
    }

    // The form table (capability.h); ex lines are asserted by test/run.sh.
    const form s_json[] = {
        {"json_parse(text: string): any", R"EX(var m = $json_parse("{\"n\": 7}"); return m["n"]; => 7)EX", json_parse, "io_fmjs"},
        {"json_str(v: any): string", R"EX(return $json_str([1,2]); => "[\n\t1,\n\t2\n]")EX", json_str, "io_tojs"},
        {"json_compact(v: any): string", R"EX(return $json_compact(map{"a": 1}); => "{\"a\":1}")EX", json_compact, "-"},
    };

    const table& json_table() {
        static const table t = {"json", s_json, sizeof(s_json) / sizeof(s_json[0])};
        return t;
    }

} // namespace capability

#endif // ALXRUN_JSON_ENABLE
