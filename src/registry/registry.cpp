// Copyright (c) 2026 AlexisVon
// SPDX-License-Identifier: Apache-2.0

#include "registry.h"

#include "capability.h"
#include "config.h"
#include "fs.h"
#include "func.h"
#include "hash.h"
#include "io.h"
#include "json.h"
#include "type.h"
#include "map.h"
#include "math.h"
#include "notation.h"
#include "regex.h"
#include "str.h"
#include "util.h"
#include "vec.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace registry {

    namespace {

        // The enabled modules. The type table is always present: its rows are engine builtins,
        // which no switch can remove.
        const std::vector<const capability::table*>& tables() {
            static const std::vector<const capability::table*> v = {
#if ALXRUN_STR_ENABLE
                &capability::str_table(),
#endif
#if ALXRUN_VEC_ENABLE
                &capability::vec_table(),
#endif
#if ALXRUN_MAP_ENABLE
                &capability::map_table(),
#endif
#if ALXRUN_REGEX_ENABLE
                &capability::regex_table(),
#endif
#if ALXRUN_MATH_ENABLE
                &capability::math_table(),
#endif
#if ALXRUN_HASH_ENABLE
                &capability::hash_table(),
#endif
#if ALXRUN_IO_ENABLE || ALXRUN_FS_READ_ENABLE || ALXRUN_FS_WRITE_ENABLE
                &capability::io_table(),
#endif
#if ALXRUN_FS_READ_ENABLE || ALXRUN_FS_WRITE_ENABLE
                &capability::fs_table(),
#endif
#if ALXRUN_JSON_ENABLE
                &capability::json_table(),
#endif
#if ALXRUN_UTIL_ENABLE
                &capability::util_table(),
#endif
                &capability::type_table(),
                &capability::func_table(),
                &capability::notation_table(),
            };
            return v;
        }

        // decl -> its clan bits, filled while the doc is built; the query walks the doc once and
        // needs the bits by key, not by table row.
        std::map<std::string, alx::uint_64>& clan_bits() {
            static std::map<std::string, alx::uint_64> v;
            return v;
        }

        // The bits a query names; 0 when the query is not a clan name.
        alx::uint_64 clan_of(const std::string& _q) {
            const auto it = capability::kClanNames.find(_q);
            return it == capability::kClanNames.end() ? 0 : it->second;
        }

        // "str_split" from "str_split(s: string, delim: string): vec".
        std::string decl_head(const char* _decl) {
            const std::string d(_decl);
            return d.substr(0, d.find('('));
        }

        // "$str_split(s, delim)": the decl with types and return stripped. The `$` marks a
        // registered capability only: engine builtins are called bare (`float(v)`). A doc-only form
        // with no types (`def f(a, b) {}`) is written as it is -- stripping a form would eat its
        // body; anything registered keeps the `$` even without type annotations.
        std::string brief_entry(const capability::form& _f) {
            const std::string d(_f.decl);
            const size_t open = d.find('(');
            if (open == std::string::npos) return d;
            if (!_f.impl && d.find(':') == std::string::npos) return d;
            const std::string params = d.substr(open + 1, d.rfind(')') - open - 1);
            // One plain parameter: the name alone carries it ("$str_upper"), and the full signature
            // is one `capabilities` call away -- the parens cost three bytes each, times 87.
            if (params.find(',') == std::string::npos && params.find('?') == std::string::npos)
                return (_f.impl ? "$" : "") + d.substr(0, open);
            std::string out = (_f.impl ? "$" : "") + d.substr(0, open) + "(";
            bool first = true;
            size_t pos = 0;
            while (pos <= params.size()) {
                const size_t comma = params.find(',', pos);
                std::string piece = params.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
                const size_t begin = piece.find_first_not_of(' ');
                const size_t colon = piece.find(':');
                if (begin != std::string::npos) {
                    std::string key = colon == std::string::npos ? piece.substr(begin) : piece.substr(begin, colon - begin);
                    while (!key.empty() && key.back() == ' ')
                        key.pop_back();
                    if (!first) out += ", ";
                    out += key;
                    first = false;
                }
                if (comma == std::string::npos) break;
                pos = comma + 1;
            }
            return out + ")";
        }

    } // namespace

    void install(alx::script::engine* _e) {
        for (const capability::table* t : tables())
            for (size_t i = 0; i < t->count; ++i)
                if (t->items[i].impl)
                    _e->set_extend(decl_head(t->items[i].decl), t->items[i].impl);
    }

    // {decl: [assertions]}: the doc key is the declaration itself, so a reader needs no legend, and
    // repeating a decl adds another assertion to it. "_" keys are metadata.
    const alx::json_object& document() {
        static alx::json_object cached;
        static bool built = false;
        if (built) return cached;

        std::vector<std::pair<std::string, alx::json_array>> forms;
        std::vector<std::string> added; // origin "-": a capability AScript has no spelling for
        for (const capability::table* t : tables()) {
            for (size_t i = 0; i < t->count; ++i) {
                const capability::form& f = t->items[i];
                size_t at = forms.size();
                for (size_t k = 0; k < forms.size(); ++k) {
                    if (forms[k].first == f.decl) {
                        at = k;
                        break;
                    }
                }
                if (at == forms.size()) forms.emplace_back(f.decl, alx::json_array());
                forms[at].second.append(alx::json_value(f.ex));
                if (std::strcmp(f.origin, "-") == 0) {
                    const std::string name = decl_head(f.decl);
                    if (std::find(added.begin(), added.end(), name) == added.end()) added.push_back(name);
                }
            }
        }
        for (auto& e : forms) cached.insert(e.first, alx::json_value(e.second));
        alx::json_array names;
        for (const std::string& n : added) names.append(alx::json_value(n));
        cached.insert("_not_in_ascript", alx::json_value(names));
        alx::json_object clans; // _clan -- which family each entry answers to, from its bits
        for (const capability::table* t : tables()) {
            for (size_t i = 0; i < t->count; ++i) {
                const capability::form& f = t->items[i];
                const alx::uint_64 bits = f.clan | t->clan; // a whole table can be one family
                if (!bits || clans.contain(f.decl)) continue;
                clan_bits()[f.decl] |= bits;
                alx::json_array labels;
                alx::uint_64 seen = capability::clan_none; // one label per bit: the first name in key order
                for (const auto& c : capability::kClanNames) {
                    if (!(bits & c.second) || (seen & c.second)) continue;
                    if (c.second & (c.second - 1)) continue; // `syntax` is a union, not a label of its own
                    seen |= c.second;
                    labels.append(alx::json_value(c.first));
                }
                clans.insert(f.decl, alx::json_value(labels));
            }
        }
        cached.insert("_clan", alx::json_value(clans));
        built = true;
        return cached;
    }

    // The doc filtered by a query: a clan name answers with that family whole, anything else is a
    // literal substring of the key. No regex: `[null]` as a character class drowned four rules in
    // 90 keys (measured). "." alone is the sentinel the tool description promises.
    alx::json_object document(const std::string& _q) {
        const alx::json_object& all = document();
        if (_q == ".") return all;
        alx::json_object kept;
        const alx::uint_64 clan = clan_of(_q);
        for (auto it = all.begin(); it != all.end(); ++it) {
            const auto bits = clan_bits().find(it.key());
            const bool by_clan = clan && bits != clan_bits().end() && (bits->second & clan);
            if (by_clan || it.key().find(_q) != std::string::npos)
                kept.insert(it.key(), it.value());
        }
        return kept;
    }

    // The two vocabularies a caller can ask by, plus the one sentinel: the clans and the module
    // labels. They are not the same list -- a label is a prefix, a clan is a facet, and `file` /
    // `util` are the two faceted ones -- so a caller that has not read the brief finds both here.
    // Names only: a caller comes to look things up, and a size answers no question it has.
    alx::json_object index() {
        alx::json_array clans;
        for (const auto& c : capability::kClanNames) clans.append(alx::json_value(c.first));
        alx::json_array labels;
        for (const capability::table* t : tables()) labels.append(alx::json_value(std::string(t->name)));
        alx::json_object out;
        out.insert("_clans", alx::json_value(clans));
        out.insert("_labels", alx::json_value(labels));
        out.insert("_hint", alx::json_value("ask by clan (see _clans) or by module label (see _labels), "
                                            "by capability name/prefix, or by pattern — \".\" takes everything"));
        return out;
    }

    std::string demo() {
        return capability::demo();
    }

    std::string demo_data() {
        return capability::demo_data();
    }

    std::string clans_line() {
        std::string out;
        for (const auto& c : capability::kClanNames) {
            if (!out.empty()) out += " \u00b7 ";
            out += c.first;
        }
        return out;
    }

    std::string brief() {
        std::string out;
        for (const capability::table* t : tables()) {
            const std::string sep = std::strchr(t->items[0].decl, '(') ? " " : " · ";
            std::string line = std::string(t->name) + ":";
            std::vector<std::string> seen; // one entry per name: the first form is the primary
            for (size_t i = 0; i < t->count; ++i) {
                const std::string name = decl_head(t->items[i].decl);
                if (std::find(seen.begin(), seen.end(), name) != seen.end()) continue;
                const std::string listed = brief_entry(t->items[i]);
                const std::string joiner = seen.empty() ? " " : sep;
                if (line.size() + listed.size() + joiner.size() > 100) {
                    out += line + "\n";
                    line = "   ";
                }
                line += joiner + listed;
                seen.push_back(name);
            }
            out += line + "\n";
        }
#if ALXRUN_FS_READ_ENABLE || ALXRUN_FS_WRITE_ENABLE
        out += capability::fs_rules();
#endif
        return out;
    }

} // namespace registry
