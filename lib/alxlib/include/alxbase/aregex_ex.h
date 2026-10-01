/*****************************************************************/ /**
 * \file   aregex_ex.h
 * \brief  Regular expression extensions
 *
 * \author alexis
 *
 * Copyright (c) 2026 AlexisVon
 * SPDX-License-Identifier: MIT
 *********************************************************************/

#ifndef _ALEXIS_REGEX_EX_H_
#define _ALEXIS_REGEX_EX_H_

#include "abase.h"
#include <regex>

namespace alx {
    /**
     * \brief Regular expression compiled once, matched many times
     *
     * The pattern is compiled at construction with std::regex -- ECMAScript grammar unless _opt
     * picks another -- and every matcher runs against that one compiled object. A pattern the
     * engine rejects does not throw: the object is left invalid, and calling a matcher on it is
     * undefined -- is_valid() is the gate. Subjects arrive as a pointer plus a byte count whose
     * default, uint_64_npos, means strlen(_ptr). The matchers only read the compiled pattern, so
     * one instance may serve several threads as long as no thread assigns to it.
     *
     * Matching cost is unbounded -- the engine has no step limit and cannot be interrupted --
     * so this class is for trusted patterns only.
     */
    class ALXBASE_API regex_ex {
    public:
        /// Default: nothing compiled -- invalid until assigned from a valid instance
        regex_ex() noexcept {}
        /// Delete the compiled pattern
        ~regex_ex() { delete regx; }
        /**
         * \brief Compile _reg_str
         *
         * Never throws: a pattern the engine rejects leaves the object invalid, but its text
         * stays readable through get_pattern().
         *
         * \param _reg_str Pattern text, stored as given and returned by get_pattern()
         * \param _opt Grammar and matching options; ECMAScript when omitted
         */
        regex_ex(const std::string& _reg_str, std::regex::flag_type _opt = std::regex::ECMAScript) noexcept;
        /// Copy the pattern text and the compiled pattern; an invalid source copies as invalid
        regex_ex(const regex_ex& _regx) noexcept;
        /// Replace both, freeing the old compiled pattern first. noexcept: a failed allocation
        /// while copying the pattern text terminates
        regex_ex& operator=(const regex_ex& _regx) noexcept;
        /// Take over _regx's pattern; _regx is left invalid
        regex_ex(regex_ex&& _regx) noexcept;
        /// Exchange the compiled patterns and take _regx's pattern text: _regx is left holding
        /// this object's former regex, with an empty pattern text
        regex_ex& operator=(regex_ex&& _regx) noexcept;

    public:

        /**
         * \brief True when the whole subject matches the pattern
         *
         * Matching spans the subject end to end: a matching prefix or substring is not enough,
         * use find() for that. A pattern that failed to compile never reaches here -- the
         * constructor rejects it up front and is_valid() reports that.
         *
         * \return false both when the subject does not match and when it matches only in part
         */
        bool is_compliant(const char* _ptr, uint_64 _size = uint_64_npos) const;

        /// Same, over the whole string: its terminating null is not part of the subject
        bool is_compliant(const std::string& _str) const { return is_compliant(_str.c_str(), _str.length()); }

    public:

        /**
         * \brief Text of the first match, in subject order
         *
         * Matching anywhere in the subject, not only at its start.
         *
         * \return The matched bytes; empty both when nothing matches and when the match is zero
         *         wide, so the two cases are not told apart
         */
        std::string find(const char* _ptr, uint_64 _size = uint_64_npos) const;

        /// Same, over the whole string
        std::string find(const std::string& _str) const { return find(_str.c_str(), _str.length()); }

        /**
         * \brief Text of every non-overlapping match, left to right
         *
         * A zero-width match does not stall the scan: the next one starts at the following
         * byte.
         *
         * \return One entry per match, in subject order; empty when nothing matches
         */
        std::vector<std::string> find_all(const char* _ptr, uint_64 _size = uint_64_npos) const;

        /// Same, over the whole string
        std::vector<std::string> find_all(const std::string& _str) const { return find_all(_str.c_str(), _str.length()); }

    public:

        /**
         * \brief Groups of the first match
         *
         * Index 0 is the whole match, then g_1..g_n in pattern order. A group that took no part
         * in the match is an empty string, so the vector is empty only when nothing matched at
         * all.
         *
         * \return {g_0, ..., g_n}, empty when the pattern does not match
         */
        std::vector<std::string> match_group(const char* _ptr, uint_64 _size = uint_64_npos) const;

        /// Same, over the whole string
        std::vector<std::string> match_group(const std::string& _str) const { return match_group(_str.c_str(), _str.length()); }

        /**
         * \brief Groups of every non-overlapping match, concatenated match by match
         *
         * \return {g0_0, ..., g0_n, g1_0, ..., g1_n, ...}, empty when the pattern does not match
         */
        std::vector<std::string> match_group_all(const char* _ptr, uint_64 _size = uint_64_npos) const;

        /// Same, over the whole string
        std::vector<std::string> match_group_all(const std::string& _str) const { return match_group_all(_str.c_str(), _str.length()); }

    public:

        /**
         * \brief Replace the first match, leaving the rest of the subject alone
         *
         * _repl is the ECMAScript replacement format: $n names capture group n, $& the whole
         * match and $$ a literal dollar. A reference to a group that took no part in the match,
         * or one past the last group, substitutes nothing.
         *
         * \return The subject with its first match replaced; a copy of _str when the pattern
         *         does not match
         */
        std::string replace(const std::string& _str, const std::string& _repl) const;

        /**
         * \brief Replace every non-overlapping match, left to right
         *
         * Takes the same replacement format as replace(), applied to every match.
         *
         * \return The subject with all its matches replaced; a copy of _str when the pattern
         *         does not match
         */
        std::string replace_all(const std::string& _str, const std::string& _repl) const;

    public:
        /// True when a compiled pattern is held; the matchers may only be called then
        bool is_valid() const { return nullptr != regx; }
        /**
         * \brief The compiled pattern, to reach the std::regex entry points not wrapped here
         *
         * Null when invalid. Owned by this object: do not delete it, and use it only while the
         * object lives and is not assigned to.
         */
        const std::regex* get_regex() const { return regx; }
        /// The pattern text exactly as constructed; kept even when the compile failed
        const std::string& get_pattern() const { return pattern; }

    private:
        std::regex* regx{nullptr};
        std::string pattern;
    };
}

#endif