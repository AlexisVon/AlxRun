// Copyright (c) 2026 AlexisVon
// SPDX-License-Identifier: Apache-2.0

/// libFuzzer entry point: every input is an AScript script, run through the real kernel (same
/// registry, same capabilities, same gate).
///
/// Seeds are the scripts the model actually wrote (agent/*.axc) plus the cases' reference
/// solutions: random bytes are blind, "the shapes a model really writes" are not. A crash found
/// here is a real finding -- it reaches the real kernel through a script, so grade it before
/// calling it a bug; this file just feeds the machine.
///
/// Build and run: test/fuzz/run.sh.
#include "kernel.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* _data, size_t _size) {
    // Positions cost ~33% on compute-bound loops and buy nothing here.
    static const bool prepared = [] {
        kernel::set_debug(false);
        std::atexit([] { kernel::shutdown(); }); // before LSan's check: the engine is not a leak
        return true;
    }();
    (void) prepared;

    if (_size == 0 || _size > 64 * 1024) return 0; // the lexer/parser is the target, not bulk data

    // One pre-bound variable: the data channel is part of the surface, and the seeds taken
    // from the pre-bound cases read it by name.
    alx::json_object data;
    data["rows"] = alx::json_value("1\n2\n3\n4\n5");

    std::string code(reinterpret_cast<const char*>(_data), _size);
    kernel::run(code, data, "");
    return 0;
}
