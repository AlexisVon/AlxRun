// Copyright (c) 2026 AlexisVon
// SPDX-License-Identifier: Apache-2.0

#include "hash.h"

#include "averify.h"

#include "config.h" // the module switch (src/config.h)

#if ALXRUN_HASH_ENABLE

namespace capability {

    // Accepts a string or bytes; anything else is a TypeError.
    void hash_sha256(alx::script::fwrap& _fw) {
        if (_fw[0].is<alx::bytes>()) {
            auto& b = _fw[0].to<alx::bytes>();
            _fw.freturn(alx::variant(alx::verify::exec(alx::verify::SHA_256,
                                                       reinterpret_cast<const alx::uint_8*>(b.data()), b.size())));
            return;
        }
        _fw.freturn(alx::variant(alx::verify::exec(alx::verify::SHA_256, as_str(_fw[0], "hash_sha256 data"))));
    }

    // The form table (capability.h); ex lines are asserted by test/run.sh.
    const form s_hash[] = {
        {"hash_sha256(data: string|bytes): string", R"EX(return $hash_sha256("hello"); => "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824")EX", hash_sha256, "hash_sha256"},
    };

    const table& hash_table() {
        static const table t = {"hash", s_hash, sizeof(s_hash) / sizeof(s_hash[0])};
        return t;
    }

} // namespace capability

#endif // ALXRUN_HASH_ENABLE
