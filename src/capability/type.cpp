// Copyright (c) 2026 AlexisVon
// SPDX-License-Identifier: Apache-2.0

#include "type.h"

namespace capability {

// The type family -- doc-only (impl = nullptr): the engine cannot register or enumerate these, so
// this table is the only way to say they exist. The encoding names are left out on purpose -- the
// engine's own error hands out the valid set, and a copy here had already drifted.
#define ENC "hex|base64|charset"
    const form s_type[] = {
        {"int(v: any): int", R"EX(return int("42"); => 42)EX", nullptr, ""},
        {"float(v: any): float", R"EX(return float("3.5"); => 3.5)EX", nullptr, ""},
        {"string(v: any): string", R"EX(return string(42); => "42")EX", nullptr, ""},
        {"string(v: any): string",
         R"EX(return string(map{"a":1}); => ![ConvError] Cannot convert to string)EX", nullptr, ""},
        {"string(d: double, prec: int): string", R"EX(return string(3.14159, 3); => "3.142")EX", nullptr, ""},
        // ties-to-even: 2.5->2 rules out half-up, 1.5->2 rules out floor/trunc
        {"string(d: double, prec: int): string", R"EX(return string(2.5, 0); => "2")EX", nullptr, ""},
        {"string(d: double, prec: int): string", R"EX(return string(1.5, 0); => "2")EX", nullptr, ""},
        {"string(i: int, base: int): string", R"EX(return string(255, 16); => "0xff")EX", nullptr, ""},
        // The fmt convention is a language rule: it lives in notation (`fmt = Qt-style %1 %2`).
        {"string(fmt: string, args...): string", R"EX(return string("%1-%2", 1, 2); => "1-2")EX", nullptr, ""},
        {"string(b: bytes, enc: " ENC "): string", R"EX(return string(bytes("6869", "hex"), "utf-8"); => "hi")EX", nullptr, ""},
        // A name folds at both ends: case and separators are ignored.
        {"string(b: bytes, enc: " ENC "): string", R"EX(return string(bytes("6869", "HEX"), "Utf 8"); => "hi")EX", nullptr, ""},
        {"bool(v: any): bool", R"EX(return bool(1); => true)EX", nullptr, ""},
        {"bytes("
         "s: string, enc?: " ENC "): bytes",
         R"EX(return string(bytes("6869", "hex"), "utf-8"); => "hi")EX", nullptr, ""},
        {"type(v: any): string", R"EX(return type(3); => "int")EX", nullptr, ""},
        // `var` is untyped by design: one declaration, any value, and no second declaration of the
        // same name -- the two habits a JS prior brings (implicit globals, re-`var`) are errors here.
        {"var x = any", R"EX(var a, b, c; a = 1; b = "s"; c = [1]; return type(b); => "string")EX", nullptr, ""},
        {"var x = any", R"EX(var a; return type(a); => "null")EX", nullptr, ""},
        {"var x = any", R"EX(var a = 1; a = "s"; a = [1]; return a; => [1])EX", nullptr, ""},
        {"var x = any", R"EX(var a = 1; var a = 2; => ![NameError] Name conflict)EX", nullptr, ""},
        {"var x = any", R"EX(var a = 1; delete a; var a = 2; return a; => 2)EX", nullptr, ""},
        {"var x = any", R"EX(a = 5; => ![NameError] Undefined variable)EX", nullptr, ""},
    };
#undef ENC

    const table& type_table() {
        static const table t = {"type", s_type, sizeof(s_type) / sizeof(s_type[0]), clan_type};
        return t;
    }

    // The resident example: it runs (test/run.sh), it is not the answer to any real task, and it
    // takes `orders` from the host -- the data channel scripts should use (design.md §7). It also
    // teaches two measured traps: $map_get's default must be 0.0, and string(v, 2) rounds.
    const char* demo() {
        return
            R"EX(// Example — `orders` is bound by the host; do not inline the data
var tot = map{};
for (var o : orders) {
    tot[o.region] = $map_get(tot, o.region, 0.0) + o.amount;
}
var out = [];
for (var k : $map_keys(tot)) {
    $vec_push(out, k + " " + string(tot[k], 2));
}
return out;
// => ["eu 80.25", "us 120.50"])EX";
    }

    // What the host binds when running the demo (test/run.sh passes it as --data).
    const char* demo_data() {
        return R"EX({"orders": [{"region": "eu", "amount": 80.25}, {"region": "us", "amount": 120.5}]})EX";
    }

} // namespace capability
