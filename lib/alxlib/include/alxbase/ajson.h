/*****************************************************************/ /**
 * \file   ajson.h
 * \brief  JSON parser (recursive descent) and generator
 *
 * \author alexis
 *
 * Copyright (c) 2026 AlexisVon
 * SPDX-License-Identifier: MIT
 *********************************************************************/

#ifndef _ALEXIS_JSON_H_
#define _ALEXIS_JSON_H_

#include "abase.h"
#include "astring.h"
#include "avariant.h"

namespace alx {
    /// Forward declaration of the byte sink interface defined in astream.h
    class ostream;
    /// Forward declaration of the JSON object defined below
    class json_object;
    /// Forward declaration of the JSON array defined below
    class json_array;
    /// Forward declaration of the JSON value defined below
    class json_value;

    /// The map template a json_object keeps its entries in; iteration follows the key order
    template <typename Key, typename Value>
    using json_base_map = std::map<Key, Value>;

    /**
     * \brief JSON object: an ordered map of key to json_value
     *
     * std::map sits behind it, so entries iterate in key order and not in the order the document
     * listed them: a parse and write round trip reorders the keys. The values are owned and a
     * copy is deep; inserting a key that is already there keeps the value stored first.
     */
    class ALXBASE_API json_object
        : public __varmap_impl<json_base_map, json_value> {
    public:
        using __varmap_impl::__varmap_impl;
        using __varmap_impl::begin;
        using __varmap_impl::cbegin;
        using __varmap_impl::cend;
        using __varmap_impl::end;

    public:

        /// Deep-convert to the variant-based varmap, one entry per key
        varmap to_varmap() const;

        /**
         * \brief Convert to varmap, handing the values over by move
         *
         * A string or a nested container changes hands, so this object keeps the entry with a
         * moved-from payload; a number or a bool is copied. No key is removed.
         */
        varmap take_varmap();

        /// Build from a varmap, converting every value in turn
        static json_object from_varmap(const varmap& _vmap);

        /// Same build, moving the payloads out of _vmap (its keys stay)
        static json_object from_varmap(varmap&& _vmap);
    };

    /**
     * \brief JSON array: an owned sequence of json_value
     *
     * The std::vector base is public, but the elements are heap objects this class owns: append(),
     * the constructors and clear() keep the ownership bookkeeping, while the inherited mutators
     * (push_back, insert, erase, resize, pop_back) move raw pointers around instead and will leak
     * or double-free. Elements iterate in document order, and a copy duplicates every one of them.
     */
    class ALXBASE_API json_array
        : public std::vector<json_value*> {
    public:
        /// An empty array
        json_array() {}
        /// Build from a braced list, each value copied into a node of its own
        json_array(std::initializer_list<json_value> _list);
        /// Deep copy: every element is duplicated
        json_array(const json_array& _value);
        /// Take over the elements of _value, which is left empty
        json_array(json_array&& _value) noexcept;
        /// Deep copy by copy-and-swap; self-assignment is a no-op
        json_array& operator=(const json_array& _value);
        /// Replace the contents with _value's, which is left empty; self-assignment is a no-op
        json_array& operator=(json_array&& _value) noexcept;
        /// Delete every element
        ~json_array();

    public:

        /// Delete every element and empty the array
        void clear();
        /// Append _value as a node of its own, leaving _value null
        void append(json_value&& _value);
        /// Append a copy of _value as a node of its own
        void append(const json_value& _value);

        /// Element-wise equality; the inherited vector comparison compared the pointers
        bool operator==(const json_array& _value) const;
        /// Negation of operator==
        bool operator!=(const json_array& _value) const;
        /// Element at _index, unchecked: an index outside the array is undefined, never a throw
        inline json_value& at(int _index) { return *vector::operator[](_index); }
        /// Element at _index for reading; unchecked like the mutable one
        inline const json_value& at(int _index) const { return *vector::operator[](_index); }
        /// Element at _index, the same unchecked access as at()
        inline json_value& operator[](int _index) { return *vector::operator[](_index); }
        /// Element at _index for reading, the same unchecked access as at()
        inline const json_value& operator[](int _index) const { return *vector::operator[](_index); }

    public:

        /// Deep-convert to the variant-based varvec, one element per entry
        varvec to_varvec() const;

        /// Convert to varvec, moving the payloads out of the elements (the array keeps them)
        varvec take_varvec();

        /// Deep-convert to the variant-based varlst, one element per entry
        varlst to_varlst() const;

        /// Convert to varlst, moving the payloads out of the elements (the array keeps them)
        varlst take_varlst();

        /// Build from a varvec, converting every element in turn
        static json_array from_varvec(const varvec& _vvec);

        /// Same build, moving the payloads out of _vvec (its elements stay)
        static json_array from_varvec(varvec&& _vvec);

        /// Build from a varlst, converting every element in turn
        static json_array from_varlst(const varlst& _vlst);

        /// Same build, moving the payloads out of _vlst (its elements stay)
        static json_array from_varlst(varlst&& _vlst);

        /**
         * \brief Build from a std::list, converting every element on the way
         *
         * An element that already is T_TYPE is stored as it is, any other goes through
         * T_TYPE(element), so the conversion has to be one T_TYPE accepts.
         *
         * \tparam F_TYPE Element type of _vlst
         * \tparam T_TYPE Type every element is converted to before it is stored
         */
        template <typename F_TYPE, typename T_TYPE>
        static json_array from_list(const std::list<F_TYPE>& _vlst) {
            json_array result;
            for (const F_TYPE& it : _vlst) result.append(json_value(std::is_same_v<F_TYPE, T_TYPE> ? it : T_TYPE(it)));
            return result;
        }

        /// Same from a list taken by rvalue: an element that already is T_TYPE is moved out of
        /// _vlst, one that needs converting is copied through T_TYPE(element)
        template <typename F_TYPE, typename T_TYPE>
        static json_array from_list(std::list<F_TYPE>&& _vlst) {
            json_array result;
            for (auto&& it : _vlst) result.append(json_value(std::is_same_v<F_TYPE, T_TYPE> ? F_TYPE(std::move(it)) : T_TYPE(it)));
            return result;
        }

        /// Build from a std::vector, converting every element the same way from_list does
        template <typename F_TYPE, typename T_TYPE>
        static json_array from_vector(const std::vector<F_TYPE>& _vvec) {
            json_array result;
            for (const F_TYPE& it : _vvec) result.append(json_value(std::is_same_v<F_TYPE, T_TYPE> ? it : T_TYPE(it)));
            return result;
        }

        /// Same from a vector taken by rvalue: an element that already is T_TYPE is moved out of
        /// _vvec, so a std::string or a bytes payload changes hands; a converted one is copied
        template <typename F_TYPE, typename T_TYPE>
        static json_array from_vector(std::vector<F_TYPE>&& _vvec) {
            json_array result;
            for (auto&& it : _vvec) result.append(json_value(std::is_same_v<F_TYPE, T_TYPE> ? F_TYPE(std::move(it)) : T_TYPE(it)));
            return result;
        }
    };

    /**
     * \brief JSON value: object, array, long long, double, bool, std::string, or nothing
     *
     * A default-constructed value is null, and so is what an unsupported conversion or a failed
     * parse gives. Narrower integers and floats are stored as long long and double, bytes as a
     * Base64 std::string, so a value has exactly one stored shape. A to_X() accessor answers a
     * type mismatch with _def and reports the match through *_ok when that pointer is not null.
     */
    class ALXBASE_API json_value
        : public __variant_impl<
              __variant_utils_base,
              json_object, json_array,
              long long, double, bool, std::string> {
    public:
        using __variant_impl::__variant_impl;
        /// A null value: no type, no payload
        json_value() : __variant_impl() {}

        /// Widened to long long; a char is stored as its code, not as a one-character string
        json_value(char _v) : __variant_impl((long long) _v) {}
        /// Widened to long long
        json_value(unsigned char _v) : __variant_impl((long long) _v) {}
        /// Widened to long long
        json_value(short _v) : __variant_impl((long long) _v) {}
        /// Widened to long long
        json_value(unsigned short _v) : __variant_impl((long long) _v) {}
        /// Widened to long long
        json_value(int _v) : __variant_impl((long long) _v) {}
        /// Widened to long long
        json_value(unsigned int _v) : __variant_impl((long long) _v) {}
        /// Widened to long long, and a value above LLONG_MAX does not fit and is not rejected
        json_value(unsigned long long _v) : __variant_impl((long long) _v) {}

        /// The text is copied into a std::string, so _v is not kept
        json_value(const char* _v) : __variant_impl(std::string(_v)) {}

        /// Stored Base64-encoded as a std::string; no overload of the four takes _v over
        json_value(bytes& _v) : __variant_impl(_v.to_base64()) {}
        /// Stored Base64-encoded as a std::string; no overload of the four takes _v over
        json_value(bytes&& _v) : __variant_impl(_v.to_base64()) {}
        /// Stored Base64-encoded as a std::string; no overload of the four takes _v over
        json_value(const bytes& _v) : __variant_impl(_v.to_base64()) {}
        /// Stored Base64-encoded as a std::string; no overload of the four takes _v over
        json_value(const bytes&& _v) : __variant_impl(_v.to_base64()) {}

    public:

        /// Mutable read of the stored long long; a mismatch returns _def by reference to write into
        long long& to_intg(long long& _def, bool* _ok = nullptr);

        /// Mutable read of the stored bool; a mismatch returns _def by reference to write into
        bool& to_bool(bool& _def, bool* _ok = nullptr);

        /// Mutable read of the stored double; a mismatch returns _def by reference to write into
        double& to_double(double& _def, bool* _ok = nullptr);

        /// Mutable read of the stored string; a mismatch returns _def by reference to write into
        std::string& to_string(std::string& _def, bool* _ok = nullptr);

        /// Mutable read of the stored array; a mismatch returns _def by reference to write into
        json_array& to_array(json_array& _def, bool* _ok = nullptr);

        /// Mutable read of the stored object; a mismatch returns _def by reference to write into
        json_object& to_object(json_object& _def, bool* _ok = nullptr);

    public:

        /// Read of the stored long long; a mismatch gives _def, by value
        long long to_intg(const long long& _def = 0, bool* _ok = nullptr) const;

        /// Read of the stored bool; a mismatch gives _def, by value
        bool to_bool(const bool& _def = false, bool* _ok = nullptr) const;

        /// Read of the stored double; a mismatch gives _def, by value
        double to_double(const double& _def = 0.0, bool* _ok = nullptr) const;

        /**
         * \brief Read of the stored string
         *
         * A mismatch gives _def, which defaults to the shared static def_val<std::string>() and so
         * lives as long as the process; a caller-supplied _def has to outlive the reference it is
         * returned as. The stored string lives as long as this value does.
         */
        const std::string& to_string(const std::string& _def = def_val<std::string>(), bool* _ok = nullptr) const;

        /// Read of the stored array; a mismatch gives _def, with the lifetime rule of to_string()
        const json_array& to_array(const json_array& _def = def_val<json_array>(), bool* _ok = nullptr) const;

        /// Read of the stored object; a mismatch gives _def, with the lifetime rule of to_string()
        const json_object& to_object(const json_object& _def = def_val<json_object>(), bool* _ok = nullptr) const;

    public:
        /// True when nothing is stored: the state of a default-constructed or moved-from value
        inline bool is_null() const { return null(); }
        /// True when the stored type is long long
        inline bool is_intg() const { return is<long long>(); }
        /// True when the stored type is bool
        inline bool is_bool() const { return is<bool>(); }
        /// True when the stored type is double
        inline bool is_double() const { return is<double>(); }
        /// True when the stored type is std::string
        inline bool is_string() const { return is<std::string>(); }
        /// True when the stored type is json_array
        inline bool is_array() const { return is<json_array>(); }
        /// True when the stored type is json_object
        inline bool is_object() const { return is<json_object>(); }

        /// Deep-convert to the variant-based value: an object becomes a varmap, an array a
        /// varvec, a null value a null variant
        variant to_variant() const;

        /**
         * \brief Convert to a variant, handing the payloads over by move
         *
         * A string, an object and an array change hands, leaving this value with a moved-from
         * payload of the same type; a number, a bool and a null value are not moved.
         */
        variant take_variant();

        /**
         * \brief Convert a variant into a json_value
         *
         * Every numeric type becomes long long or double, bytes a Base64 std::string, and the
         * containers a json_array (varvec, varlst, std::vector<T>, std::list<T>) or a json_object
         * (varmap); a type outside that set gives a null value.
         *
         * \param _var The value to convert
         */
        static json_value from_variant(const variant& _var);

        /// Same conversion, taking the strings and containers over from _var by move; a number
        /// or a bool is copied, and _var keeps its type with a moved-from payload
        static json_value from_variant(variant&& _var);
    };
    /**
     * \brief JSON text in and out: the parser and the writer, all static
     *
     * Parsing takes a whole document from a flat buffer and builds a fresh tree; writing walks a
     * tree and emits text, either into the std::string it returns or into a caller-provided
     * ostream. The rvalue overloads consume the tree as they write it.
     */
    class ALXBASE_API json_doc {
    public:

        /**
         * \brief Write a json_object as JSON text
         *
         * \param _json The object to write
         * \param _compact true emits the shortest form, false breaks lines and indents with tabs
         * \return The document text
         */
        static std::string to_json(const json_object& _json, bool _compact = false);

        /**
         * \brief Write a json_object as JSON text, consuming the tree as it goes
         *
         * Every entry is handed back as soon as its text is written, so the peak stays
         * max(tree, text) instead of tree + text, and _json is empty afterwards.
         *
         * \param _json The object to write; left empty
         * \param _compact true emits the shortest form, false breaks lines and indents with tabs
         * \return The document text
         */
        static std::string to_json(json_object&& _json, bool _compact = false);

        /**
         * \brief Write a json_value as JSON text
         *
         * A scalar is written as a bare JSON scalar, so the result is a whole document only when
         * the value holds an object or an array; a null value is written as null.
         *
         * \param _json The value to write
         * \param _compact true emits the shortest form, false breaks lines and indents with tabs
         * \return The document text
         */
        static std::string to_string(const json_value& _json, bool _compact = false);

        /**
         * \brief Write a json_value as JSON text, consuming it as it goes
         *
         * A string, an object and an array change hands as they are written; _json keeps its type
         * with a moved-from payload afterwards.
         *
         * \param _json The value to write
         * \param _compact true emits the shortest form, false breaks lines and indents with tabs
         * \return The document text
         */
        static std::string to_string(json_value&& _json, bool _compact = false);

        /**
         * \brief Write a json_object into a caller-provided stream
         *
         * Lets a host serialize straight into storage it owns (a mapped block, a file, a socket)
         * instead of building the text in memory first.
         *
         * \param _json The object to write
         * \param _out Sink; writing stops as soon as its append() returns false
         * \param _compact true emits the shortest form, false breaks lines and indents with tabs
         * \return false as soon as _out refused bytes, true when the whole document went out
         */
        static bool to_json(const json_object& _json, ostream& _out, bool _compact = false);

        /// Same write of a tree taken by rvalue: it is emptied as it goes, and an _out that
        /// refuses bytes leaves it holding the entries that were not written yet
        static bool to_json(json_object&& _json, ostream& _out, bool _compact = false);

        /// Write a json_value into _out, with the same stopping rule as the object overload
        static bool to_string(const json_value& _json, ostream& _out, bool _compact = false);

        /// Same write of a value taken by rvalue, consuming it as it goes
        static bool to_string(json_value&& _json, ostream& _out, bool _compact = false);

        /**
         * \brief Parse a whole document into a json_object
         *
         * The root has to be an object; an array or a scalar at the root belongs to from_value().
         * The parser is lenient in two places: a trailing comma before a closing brace or bracket
         * is accepted, and anything after the root's own closing brace is ignored, not refused.
         *
         * \param _data Document text; it does not have to be NUL-terminated
         * \param _size Length of _data in bytes
         * \param _ok When not null, receives the parse result
         * \return The tree, empty when the parse failed
         */
        static json_object from_json(const char* _data, size_t _size, bool* _ok = nullptr);

        /// Same parse over a bytes_view
        inline static json_object from_json(const bytes_view& _bytes, bool* _ok = nullptr) {
            return from_json((const char*) _bytes.data(), _bytes.size(), _ok);
        }

        /// Same parse over a std::string
        inline static json_object from_json(const std::string& _string, bool* _ok = nullptr) {
            return from_json(_string.data(), _string.size(), _ok);
        }

        /**
         * \brief Parse one JSON value: an object, an array or a bare scalar
         *
         * A failed parse and a parsed null both come back null, so _ok is the only way to tell
         * the two apart.
         *
         * \param _data Document text; it does not have to be NUL-terminated
         * \param _size Length of _data in bytes
         * \param _ok When not null, receives the parse result
         * \return The parsed value, null when the parse failed
         */
        static json_value from_value(const char* _data, size_t _size, bool* _ok = nullptr);

        /// Same parse over a bytes_view
        inline static json_value from_value(const bytes_view& _bytes, bool* _ok = nullptr) {
            return from_value((const char*) _bytes.data(), _bytes.size(), _ok);
        }

        /// Same parse over a std::string
        inline static json_value from_value(const std::string& _string, bool* _ok = nullptr) {
            return from_value(_string.data(), _string.size(), _ok);
        }

    private:
        inline static bool syn_json(const char* _data, size_t _size, json_object& _json);
        inline static bool syn_json_value(const char* _data, size_t _size, json_value& _val);
        inline static bool syn_json_array(const char* _data, size_t _size, const size_t _from, size_t& _next, json_array& _array);
        inline static bool syn_json_object(const char* _data, size_t _size, const size_t _from, size_t& _next, json_object& _json);

    private:
        enum json_target { LCURLY,
                           RCURLY,
                           LSQUAR,
                           RSQUAR,
                           COLON_,
                           COMMA_,
                           VALUE_,
                           ERROR_ };
        struct json_token {
            json_target type;
            json_value value;
        };
        inline static json_token lex_json(const char* _data, size_t _size, size_t _index, size_t& _next);
        inline static json_value lex_json_value(const char* _data, size_t _size, size_t _index, size_t& _next);
        inline static json_value lex_json_number(const char* _data, size_t _size, size_t _index, size_t& _next);

    public:

        /**
         * \brief Escape _src as the inside of a JSON string
         *
         * The first _ofst bytes of _dst are kept and the escaped text is appended from there, so
         * _dst must already hold _ofst bytes: a shorter one is padded with NUL, a longer one is
         * cut at _ofst. Quote, backslash and the control shortcuts get a two-character escape, any
         * other byte below 0x20 becomes \u00XX, and a byte from 0x80 up passes through unvalidated.
         *
         * \param _src Text to escape; may be _dst itself, and then _ofst must be 0
         * \param _dst Buffer to append the escaped text to
         * \param _ofst Bytes of _dst to keep untouched
         */
        static void escape(const std::string& _src, std::string& _dst, size_t _ofst);

        /// Escape _str in place; the same as escape(_str, _str, 0)
        static void escape(std::string& _str) { escape(_str, _str, 0); }

        /**
         * \brief Unescape the inside of a JSON string, the inverse of escape()
         *
         * The first _ofst bytes of _dst are kept, a shorter _dst being padded with NUL, and the
         * decoded text lands from there on; _dst ends up as long as what was written. \uXXXX is
         * decoded to UTF-8, three bytes at most and without pairing surrogates; a malformed
         * escape is passed through as well as it can be, since there is no way to report one.
         *
         * \param _src Text to unescape; may be _dst itself, and then _ofst must be 0
         * \param _dst Buffer to write the decoded text into
         * \param _ofst Bytes of _dst to keep untouched
         */
        static void descape(const std::string& _src, std::string& _dst, size_t _ofst);

        /// Unescape _str in place; the same as descape(_str, _str, 0)
        static void descape(std::string& _str) { descape(_str, _str, 0); }
    };
}

#endif