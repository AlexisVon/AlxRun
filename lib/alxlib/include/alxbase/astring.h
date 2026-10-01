/*****************************************************************/ /**
 * \file   astring.h
 * \brief  String processing utilities (multi-byte/wide character support)
 *
 * \author alexis
 *
 * Copyright (c) 2026 AlexisVon
 * SPDX-License-Identifier: MIT
 *********************************************************************/

#ifndef _ALEXIS_STRING_H_
#define _ALEXIS_STRING_H_

#include "abase.h"
#include "abytes.h"

#include <list>
#include <string>

namespace alx {
    namespace strutil {
        /**
         * \brief Code formats: an encoding plus the byte properties needed to read it
         *
         * A value is a mask: the low byte holds one or more PROPERTY_* flags, the high byte one
         * ENCODE_* value, and the named formats below are the combinations meant to be passed as
         * arguments. AUTO (0) names no format at all.
         */
        enum CODE_FORMAT : uint_16 {
            /// Let the callee pick: as a source format, detect it; as a destination, no conversion
            AUTO = 0,

            /// Multi-byte encoding: a character is one or more bytes
            PROPERTY_MULTI = 0X0001U,
            /// Wide encoding: code units of 2 bytes rather than 1-byte characters
            PROPERTY_WIDE = 0X0002U,
            /// The wide units are little-endian
            PROPERTY_LE = 0X0004U,
            /// The wide units are big-endian
            PROPERTY_BE = 0X0008U,
            /// A byte-order mark opens the buffer
            PROPERTY_BOM = 0X0010U,
            /// Bits 0..7: the PROPERTY_* flags above
            PROPERTY_MASK = 0X00FFU,

            /// GBK, which on Windows means the ANSI code page
            ENCODE_GBK = 0X0100U,
            /// UTF-8
            ENCODE_UTF8 = 0X0200U,
            /// UTF-16, whose byte order LE or BE must still supply to make it usable
            ENCODE_UTF16 = 0X0400U,
            /// Bits 8..15: the ENCODE_* value above
            ENCODE_MASK = 0XFF00U,

            /// GBK multi-byte
            GBK = ENCODE_GBK | PROPERTY_MULTI,
            /// UTF-8 multi-byte, no BOM
            UTF8 = ENCODE_UTF8 | PROPERTY_MULTI,
            /// UTF-8 multi-byte with a BOM
            UTF8_BOM = ENCODE_UTF8 | PROPERTY_MULTI | PROPERTY_BOM,
            /// UTF-16 little-endian, no BOM
            UTF16_LE = ENCODE_UTF16 | PROPERTY_WIDE | PROPERTY_LE,
            /// UTF-16 little-endian with a BOM
            UTF16_LE_BOM = ENCODE_UTF16 | PROPERTY_WIDE | PROPERTY_LE | PROPERTY_BOM,
            /// UTF-16 big-endian, no BOM
            UTF16_BE = ENCODE_UTF16 | PROPERTY_WIDE | PROPERTY_BE,
            /// UTF-16 big-endian with a BOM
            UTF16_BE_BOM = ENCODE_UTF16 | PROPERTY_WIDE | PROPERTY_BE | PROPERTY_BOM,
        };

        /**
         * \brief Decode bytes into wide characters
         *
         * Characters are decoded the way the current locale defines them -- on POSIX LC_CTYPE,
         * which this call sets from the environment; on Windows the ANSI code page -- so the
         * same bytes decode differently under a different locale.
         *
         * \param _str Bytes to decode; null gives an empty string
         * \param _len Bytes to read; uint_64_npos = up to the first null byte
         * \return The decoded characters, empty when there is nothing to decode. Input the
         *         locale cannot decode does not come back empty: POSIX throws std::length_error,
         *         Windows returns an empty string
         */
        ALXBASE_API std::wstring to_wstring(const char* _str, uint_64 _len = uint_64_npos);
        /// Decode a whole std::string, embedded null bytes included
        inline std::wstring to_wstring(const std::string& _str) { return to_wstring(_str.data(), _str.length()); }

        /**
         * \brief Encode wide characters into bytes
         *
         * The counterpart of to_wstring(), under the same locale.
         *
         * \param _str Characters to encode; null gives an empty string
         * \param _len Characters to read; uint_64_npos = up to the first null character
         * \return The encoded bytes, empty when there is nothing to encode. Characters the
         *         locale cannot represent do not come back empty: POSIX throws
         *         std::length_error, Windows returns an empty string
         */
        ALXBASE_API std::string from_wstring(const wchar_t* _str, uint_64 _len = uint_64_npos);
        /// Encode a whole std::wstring, embedded null characters included
        inline std::string from_wstring(const std::wstring& _str) { return from_wstring(_str.data(), _str.length()); }

        /**
         * \brief Encoding the current locale uses
         *
         * The locale is set from the environment as a side effect of the call. On POSIX its
         * LC_CTYPE name is matched case-insensitively -- a name naming UTF-8 gives UTF8, one
         * naming UTF-16 gives UTF16_LE, gbk/gb2312/chinese/zh-cn gives GBK, anything else UTF8;
         * on Windows the ANSI code page decides: 65001, 1200 and 1201 give UTF8, UTF16_LE and
         * UTF16_BE, every other page GBK.
         *
         * \return GBK, UTF8, UTF16_LE or UTF16_BE; never a BOM variant
         */
        ALXBASE_API CODE_FORMAT locale_format();

        /**
         * \brief Guess the format of a buffer
         *
         * A BOM wins first: EF BB BF -> UTF8_BOM, FF FE -> UTF16_LE_BOM, FE FF -> UTF16_BE_BOM.
         * Otherwise the first 1024 bytes decide: bytes shaped like valid UTF-8, with at least one
         * multi-byte character -> UTF8; a buffer of even size in which more than a quarter of
         * those bytes are null -> UTF16_LE or UTF16_BE, by which half of each unit holds them.
         * Everything else, plain ASCII included, comes back as GBK, which is the fallback rather
         * than a finding of its own.
         *
         * \param _bom_only Recognize a BOM only, skipping the content scan
         * \return The detected format, never AUTO
         */
        ALXBASE_API CODE_FORMAT detect_format(const char* _str, uint_64 _len, bool _bom_only = false);

        /**
         * \brief Convert a buffer from one code format to another
         *
         * A BOM the source carries is dropped before converting; a BOM the destination asks for
         * is prepended to the result.
         *
         * \param _len Bytes to read; 0 gives an empty string
         * \param _dst_format Target format; AUTO copies the bytes through untouched
         * \param _src_format Source format; AUTO (the default) has detect_format() decide
         * \return The converted bytes, or an empty string when the conversion cannot be made:
         *         an unsupported or nameless format (UTF-16 without LE/BE, for instance), input
         *         that is not valid in the source format, or a source holding nothing but a BOM.
         *         An empty input also converts to empty, so an empty result is not always a
         *         failure
         */
        ALXBASE_API std::string code_conver(const char* _ptr, size_t _len, CODE_FORMAT _dst_format, CODE_FORMAT _src_format = CODE_FORMAT::AUTO);
        /// Same, taking the bytes and the length from _str
        inline std::string code_conver(const std::string& _str, CODE_FORMAT _dst_format, CODE_FORMAT _src_format = CODE_FORMAT::AUTO) {
            return code_conver(_str.data(), _str.length(), _dst_format, _src_format);
        }
        /// Same, taking the bytes and the length from _str
        inline std::string code_conver(const bytes_view& _str, CODE_FORMAT _dst_format, CODE_FORMAT _src_format = CODE_FORMAT::AUTO) {
            return code_conver((const char*) _str.data(), _str.size(), _dst_format, _src_format);
        }

        /**
         * \brief Substitute numbered placeholders
         *
         * %N (N decimal and 1-based) becomes _args[N-1], and %% becomes one %. A % followed by
         * anything else is dropped, so a literal percent has to be written %%; an index that is 0
         * or past the end of _args is left in the result as it was written (%9 stays %9), and
         * consecutive digits are one index (%12 is the twelfth argument). Substituted text is not
         * scanned again.
         *
         * \param _format Template; text outside the placeholders is copied as it is
         * \param _args Arguments, one per index; extra arguments are ignored
         * \return The text with the placeholders replaced
         */
        ALXBASE_API std::string format(const std::string& _format, const std::vector<std::string>& _args);
        /// Same, with any number of arguments, each convertible to std::string
        template <typename... Args>
        inline std::string format(const std::string& _format, const Args&... _args) { return format(_format, {_args...}); }

        /**
         * \brief Substring ending at _index, counted leftwards
         *
         * \param _index Index of the last byte taken, counted from 0 in bytes and included
         * \param _length How many bytes to take; SIZE_MAX (the default) takes everything from the
         *                start of the buffer up to _index
         * \return Empty when _index is past the end of the buffer; otherwise min(_length,
         *         _index + 1) bytes, so a length reaching past the start is clamped to it
         */
        ALXBASE_API std::string left(const char* _ptr, size_t _len, size_t _index, size_t _length = SIZE_MAX);
        /// Same, over a std::string
        inline std::string left(const std::string& _str, size_t _index, size_t _length = SIZE_MAX) {
            return left(_str.data(), _str.length(), _index, _length);
        }

        /**
         * \brief Substring starting at _index, counted rightwards
         *
         * \param _index Index of the first byte taken, counted from 0 in bytes and included
         * \param _length How many bytes to take; SIZE_MAX (the default) takes everything up to
         *                the end of the buffer
         * \return Empty when _index is past the end of the buffer; otherwise min(_length,
         *         _len - _index) bytes, so a length reaching past the end is clamped to it
         */
        ALXBASE_API std::string right(const char* _ptr, size_t _len, size_t _index, size_t _length = SIZE_MAX);
        /// Same, over a std::string
        inline std::string right(const std::string& _str, size_t _index, size_t _length = SIZE_MAX) {
            return right(_str.data(), _str.length(), _index, _length);
        }

        /**
         * \brief Compare bytes at a given offset
         *
         * Raw bytes are compared, so the tag needs no terminator and none is looked for.
         *
         * \param _start Index to compare at; must not exceed _len
         * \return false when fewer than _tag_len bytes are left from _start
         */
        ALXBASE_API bool check(const char* _ptr, size_t _len, const char* _tag, size_t _tag_len, size_t _start);
        /// Same, over a std::string with a std::string tag
        inline bool check(const std::string& _str, const std::string& _tag, size_t _start = 0) {
            return check(_str.data(), _str.length(), _tag.data(), _tag.length(), _start);
        }
        /// Same, with a null-terminated tag whose length strlen() gives
        inline bool check(const std::string& _str, const char* _tag, size_t _start = 0) {
            return check(_str.data(), _str.length(), _tag, strlen(_tag), _start);
        }

        /// Same, over a raw buffer, with a null-terminated tag
        inline bool check(const char* _ptr, size_t _len, const char* _tag, size_t _start) {
            return check(_ptr, _len, _tag, strlen(_tag), _start);
        }

        /**
         * \brief Search a byte sequence forwards
         *
         * \param _from First index the tag may start at
         * \param _to End of the searched region, exclusive; uint_64_npos = the end of the buffer
         * \return Index of the tag's first byte, or uint_64_npos when it does not occur there --
         *         the tag must fit inside [_from, _to), so one running past _to is not reported
         */
        ALXBASE_API size_t find(const char* _ptr, size_t _len, const char* _tag, size_t _tag_len, uint_64 _from = 0, uint_64 _to = uint_64_npos);
        /// Same, over a std::string with a std::string tag
        inline size_t find(const std::string& _str, const std::string& _tag, uint_64 _from = 0, uint_64 _to = uint_64_npos) {
            return find(_str.data(), _str.length(), _tag.data(), _tag.length(), _from, _to);
        }
        /**
         * \brief Same, with the tag given as a pointer and a length
         *
         * A string literal binds to this overload rather than to the one above, so the third
         * argument is the tag's length and never _from.
         */
        inline size_t find(const std::string& _str, const char* _tag, uint_64 _tag_len, uint_64 _from = 0, uint_64 _to = uint_64_npos) {
            return find(_str.data(), _str.length(), _tag, _tag_len, _from, _to);
        }

        /**
         * \brief Search a byte sequence backwards
         *
         * \param _rfrom Highest index the tag's last byte may sit at; uint_64_npos = the end of
         *               the buffer
         * \param _rto Lowest index the tag may start at; 0 (the default) = the start of the buffer
         * \return Index of the tag's first byte for the last occurrence lying inside
         *         [_rto, min(_rfrom, _len - 1)], or uint_64_npos when there is none
         */
        ALXBASE_API size_t rfind(const char* _ptr, size_t _len, const char* _tag, size_t _tag_len, uint_64 _rfrom = uint_64_npos, uint_64 _rto = 0);
        /// Same, over a std::string with a std::string tag
        inline size_t rfind(const std::string& _str, const std::string& _tag, uint_64 _rfrom = uint_64_npos, uint_64 _rto = 0) {
            return rfind(_str.data(), _str.length(), _tag.data(), _tag.length(), _rfrom, _rto);
        }
        /**
         * \brief Same, with the tag given as a pointer and a length
         *
         * A string literal binds to this overload rather than to the one above, so the third
         * argument is the tag's length and never _rfrom.
         */
        inline size_t rfind(const std::string& _str, const char* _tag, uint_64 _tag_len, uint_64 _rfrom = uint_64_npos, uint_64 _rto = 0) {
            return rfind(_str.data(), _str.length(), _tag, _tag_len, _rfrom, _rto);
        }

        /**
         * \brief Cut a buffer at every occurrence of a delimiter
         *
         * The delimiter is matched as raw bytes and left out of the result. Empty elements are
         * kept, so a buffer ending with the delimiter ends with an empty element, and a buffer
         * holding no delimiter at all yields one element with the whole buffer in it.
         *
         * \param _tag Delimiter; any byte length is allowed
         * \param _tag_len Bytes of the tag; 0 cuts into single bytes, with one empty element
         *                 before the first and after the last (_len + 2 elements)
         * \return The elements in order; never an empty list
         */
        ALXBASE_API std::list<std::string> split(const char* _ptr, size_t _len, const char* _tag, size_t _tag_len);
        /// Same, over a std::string with a std::string delimiter
        inline std::list<std::string> split(const std::string& _str, const std::string& _tag) {
            return split(_str.data(), _str.length(), _tag.data(), _tag.length());
        }

        /// Concatenate a list of strings, _tag between the elements and nothing at either end
        ALXBASE_API std::string join(const std::list<std::string>& _strl, const std::string& _tag);

        /**
         * \brief The prefix function a Knuth-Morris-Pratt search is built on
         *
         * Entry i is the length of the longest proper prefix of _str[0..i] that is also a suffix
         * of it; entry 0 is always 0.
         *
         * \return One entry per byte, in byte order; empty for an empty buffer
         */
        ALXBASE_API std::vector<size_t> compute_lps(const char* _str, size_t _len);
        /// Same, over a std::string
        inline std::vector<size_t> compute_lps(const std::string& _str) {
            return compute_lps(_str.data(), _str.length());
        }

        /// Copy the bytes wrapped in one double quote at each end; nothing inside is escaped
        ALXBASE_API std::string stringfy(const char* _ptr, size_t _len);
        /// Same, over a std::string
        inline std::string stringfy(const std::string& _str) { return stringfy(_str.data(), _str.length()); }

        /**
         * \brief Delete every occurrence of a byte from a buffer, in place
         *
         * The surviving bytes are moved to the front of the buffer.
         *
         * \param _ch Byte to delete; one byte is one deletion, so cutting a byte out of a
         *            multi-byte character leaves text that is no longer valid
         * \return Length after the deletion; the bytes past it are unspecified
         */
        ALXBASE_API size_t remove_all(char* _ptr, size_t _len, char _ch);
        /// Same, over a std::string, which is resized to the new length; no-op when empty
        inline std::string& remove_all(std::string& _str, char _ch) {
            if (!_str.empty())
                _str.resize(remove_all(&_str[0], _str.size(), _ch));
            return _str;
        }

        /**
         * \brief Encode one code point as UTF-8
         *
         * \param _cp Code point; above 0x10FFFF or in the surrogate range 0xD800..0xDFFF it has
         *            no UTF-8 form
         * \return The 1 to 4 bytes of the sequence, or an empty string for such a code point --
         *         U+0000 gives one null byte, so an empty result always means a rejected code
         *         point
         */
        ALXBASE_API std::string to_utf8(uint_32 _cp);

        /**
         * \brief Decode one code point from UTF-8 bytes
         *
         * Overlong forms, surrogates and code points above 0x10FFFF are rejected, and no byte
         * past the end of _s is read.
         *
         * \param _s Bytes to decode; they need not be valid UTF-8 as a whole
         * \param _offset Byte to start at; at or past the end of _s it is already an error
         * \param _next [out] Offset just past the decoded sequence, or uint_64_npos on error --
         *                   the only reliable error signal, since a decoded U+0000 returns 0 too
         * \return The code point, or 0 on error
         */
        ALXBASE_API uint_32 from_utf8(const std::string& _s, size_t _offset, size_t& _next);
    }
}

#endif
