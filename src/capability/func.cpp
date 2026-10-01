// Copyright (c) 2026 AlexisVon
// SPDX-License-Identifier: Apache-2.0

#include "func.h"

namespace capability {

    // `eval` is deliberately absent: an anonymous function has no scenario here (a named `def` is
    // already a first-class value), and what it really offers is building code as a string -- the
    // shape this project exists to avoid. The engine keeps it; the material does not teach it, and
    // the model does not reach for what it has not read (same as `lst`).
    const form s_func[] = {
        {"def f(a, b) {}", R"EX(def f(x) { return x+1; } return f(1); => 2)EX", nullptr, ""},
        {"def f(a, b) {}", R"EX(def add(a,b){ return a+b; } def ap(f,a,b){ return f(a,b); } return ap(add,1,2); => 3)EX", nullptr, ""},
        {"def f(a, b) {}", R"EX(def f(a, b) { return a; } return f(); => ![ArgError] Missing argument)EX", nullptr, ""},
        {"def f(a, b) {}", R"EX(def f(a) { return a; } return f(1,2); => ![ArgError] Too many arguments)EX", nullptr, ""},
        {"def f(a, b) {}", R"EX(return f(1); def f(x) { return x; } => ![NameError] Undefined function)EX", nullptr, ""},
        {"def f(a, b) {}", R"EX(def f(x) { return x; } var f = 2; => ![NameError] Name conflict)EX", nullptr, ""},
        {"def f(a, b) {}", R"EX(def f() { return 1; } delete f; def f() { return 2; } return f(); => 2)EX", nullptr, ""},
        {"def f(a, b) {}", R"EX(def f(a=1) { return a; } return f(); => 1)EX", nullptr, ""},
        {"def f(a, b) {}", R"EX(function f(x) { return x; } => !Expected ;)EX", nullptr, ""},
        {"def f(a, b) {}", R"EX(var f = def(x) { return x; }; => !Unexpected token)EX", nullptr, ""},
    };

    const table& func_table() {
        static const table t = {"func", s_func, sizeof(s_func) / sizeof(s_func[0]), clan_func};
        return t;
    }

} // namespace capability
