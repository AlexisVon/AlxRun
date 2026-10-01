// Copyright (c) 2026 AlexisVon
// SPDX-License-Identifier: Apache-2.0

#ifndef ALXRUN_CAPABILITY_H
#define ALXRUN_CAPABILITY_H

#include "aanyptr.h"
#include "ascript.h"
#include "avariant.h"
#include "config.h"

#include <map>
#include <string>

namespace capability {

    /// Which family an entry answers to when a caller asks by family rather than by name. Bits,
    /// because one entry can belong to several (`"hi"[null]` is both index and notation); sparse
    /// on purpose -- a family is named only where the substring pass cannot reach past it (a key
    /// carrying the name literally comes back diluted: `string` answered with 40 keys for its 20).
    enum clanmark : alx::uint_64 {
        clan_none = 0x00LLU,
        clan_notation = 0x01LLU << 0, // the operator-symbol rules (prose keys, no prefix to guess)
        clan_index = 0x01LLU << 1, // the bracket/indexing subset of them
        clan_type = 0x01LLU << 2, // the type family: conversions and reflection are one surface
        clan_func = 0x01LLU << 3, // the function family: how one is defined, and what it refuses
        clan_file = 0x01LLU << 4, // the file facet: the fs table plus the handle's operations
        clan_util = 0x01LLU << 5, // the drawer facet: what has no domain word of its own
    };

    /// What a caller may type, and the bits it selects. A value is a mask, not one bit, so
    /// `syntax` can mean two families at once; every name is a word the model has read (a brief
    /// label) -- a name it has not read is one it will not ask for (measured: `lang`).
    const std::map<std::string, alx::uint_64> kClanNames = {
        {"notation", clan_notation},
        {"syntax", clan_notation | clan_type | clan_func}, // the language layer: all three families
        {"[]", clan_index},
        {"type", clan_type},
        {"func", clan_func},
#if ALXRUN_FS_READ_ENABLE || ALXRUN_FS_WRITE_ENABLE
        {"file", clan_file}, // one word for the whole file surface: whole-file, digest, streams
#endif
#if ALXRUN_UTIL_ENABLE
        {"util", clan_util}, // the drawer, for names that carry their own root instead of a prefix
#endif
    };

    /// One capability: registration and documentation in the same place -- no second copy to
    /// drift, so no reconciliation test.
    struct form {
        const char* decl; // doc key: "name(p: type, q?: type): ret"; no "): ret" = no return value
        const char* ex; // one runnable assertion: "code => expected"
        alx::script::native_func impl; // nullptr = doc-only: an engine builtin we do not register
        const char* origin; // AScript spelling ("" = identical, "-" = not in AScript, else renamed)
        alx::uint_64 clan = clan_none; // families it answers to beyond its own name
    };

    /// One module's forms. `clan` marks a table that is a whole family (the type table): one bit
    /// covers every row, so a row added later cannot be forgotten (hand marks missed one).
    struct table {
        const char* name; // module label in the brief, e.g. "str"
        const form* items;
        size_t count;
        alx::uint_64 clan = clan_none;
    };

    /// Type-checked argument access; _desc is "<fn> <param>", and it goes into the error verbatim,
    /// so the model can see which argument was wrong (prelude.md §2.7).
    inline std::string& as_str(alx::variant& _v, const std::string& _desc) {
        if (!_v.is<std::string>())
            throw alx::script::script_exception{alx::script::error_type::TypeError, _desc + ": expected string"};
        return _v.as<std::string>();
    }

    inline alx::int_64 as_int(alx::variant& _v, const std::string& _desc) {
        if (!_v.is<alx::int_64>())
            throw alx::script::script_exception{alx::script::error_type::TypeError, _desc + ": expected int"};
        return _v.as<alx::int_64>();
    }

    inline alx::varvec& as_vec(alx::variant& _v, const std::string& _desc) {
        if (!_v.is<alx::varvec>())
            throw alx::script::script_exception{alx::script::error_type::TypeError, _desc + ": expected vec"};
        return _v.as<alx::varvec>();
    }

    inline alx::varmap& as_map(alx::variant& _v, const std::string& _desc) {
        if (!_v.is<alx::varmap>())
            throw alx::script::script_exception{alx::script::error_type::TypeError, _desc + ": expected map"};
        return _v.as<alx::varmap>();
    }

    /// How much one read may pull into the script's memory ($fs_read, and $io_read's fallback for
    /// handles whose size is not knowable): the wall is 512 MB, so this sits well under it.
    constexpr size_t kMaxReadBytes = 256u << 20;

    /// One refusal shape for the capabilities that take a path or a handle: "<cap> <path>: <why>".
    [[noreturn]] inline void fail(const char* _cap, const std::string& _path, const std::string& _why) {
        throw alx::script::script_exception{alx::script::error_type::RuntimeError,
                                            std::string(_cap) + " " + _path + ": " + _why};
    }

    /// A non-handle must raise here: the cast dereferences the register tag.
    template <typename T>
    inline T* as_handle(alx::variant& _v, const std::string& _desc) {
        if (!_v.is<alx::anyptr>())
            throw alx::script::script_exception{alx::script::error_type::TypeError,
                                                _desc + ": expected a handle"};
        T* p = alx::anyptr_ex<T>::as(_v.as<alx::anyptr>());
        if (!p)
            throw alx::script::script_exception{alx::script::error_type::TypeError,
                                                _desc + ": handle type mismatch"};
        return p;
    }

} // namespace capability

#endif // ALXRUN_CAPABILITY_H
