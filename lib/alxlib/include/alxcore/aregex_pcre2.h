/*****************************************************************/ /**
 * \file   aregex_pcre2.h
 * \brief  PCRE2-backed regular expression engine
 *
 * \author alexis
 *
 * Copyright (c) 2026 AlexisVon
 * SPDX-License-Identifier: MIT
 *********************************************************************/

#ifndef _ALEXIS_REGEX_PCRE2_H_
#define _ALEXIS_REGEX_PCRE2_H_

#include "abase.h"
#include "autility.h"
#include <string>
#include <utility>
#include <vector>

namespace alx {

    /**
     * \brief Regex with a bounded, interruptible engine: backtracking capped by step count, a
     *        match abortable mid-flight
     *
     * PCRE2 is the engine (8-bit, interpreted -- no JIT), so the pattern syntax and the
     * replacement dialect are both PCRE2's own. Every match runs under a step and a depth limit
     * -- 10^7 each until narrowed through set_step_limit() / set_depth_limit() -- and an
     * installed hook can abort it. _opt is the whole compile-option space: the flag values below,
     * which are PCRE2's own bits, or raw PCRE2_* bits; PCRE2_AUTO_CALLOUT is the one bit the
     * class adds itself. Subjects are bytes: a pointer plus a byte count whose default,
     * uint_64_npos, means strlen(_ptr). A pattern the engine rejects leaves the object invalid --
     * is_valid() false, the compile error in last_error_code() -- and every matcher then answers
     * empty without touching that record.
     *
     * One instance is one matcher: its own hook, its own status, its own match block, and no
     * lock. Not copyable -- a shared hook would let one stop signal reach two matchers -- but
     * movable, a move carrying the hook along. No two methods may run on one instance at once,
     * and no setter may run while a match is in flight: for parallel work, take one instance
     * per thread.
     */
    class ALXCORE_API regex_pcre2 : public noncopyable {
    public:

        /**
         * \brief Common compile options; the values are PCRE2's own option bits, so raw PCRE2_*
         *        compile bits share the space
         */
        enum flag : uint_32 {
            /// PCRE2_CASELESS: matching ignores case
            icase = 0x00000008,
            /// PCRE2_DOTALL: . matches a newline too
            dotall = 0x00000020,
            /// PCRE2_MULTILINE: ^ and $ also match at line breaks
            multiline = 0x00000400,
            /// PCRE2_NO_AUTO_CAPTURE: plain ( ) does not capture
            nosubs = 0x00002000,
        };

        /// Why the last engine call ended the way it did; an empty result alone cannot tell
        /// "nothing matched" from "gave up"
        enum class match_status {
            /// the engine call completed
            ok,
            /// searched to the end of the subject, nothing matched
            no_match,
            /// step or depth limit hit, the match was aborted
            limit,
            /// the hook asked to stop (see set_callout)
            interrupted,
            /// engine error or a rejected pattern -- see last_error_code()
            error,
        };

    public:
        /// Default: nothing compiled -- an invalid object until it is move-assigned from a valid
        /// one
        regex_pcre2() noexcept {}
        /// Free the compiled pattern, the match block and the hook binding
        ~regex_pcre2();
        /**
         * \brief Compile _reg_str
         *
         * Never throws: a pattern the engine rejects leaves the object invalid, with its compile
         * error recorded in last_error_code() and its text still readable through get_pattern().
         * _opt holds compile options -- the flag values or raw PCRE2_* bits -- and
         * PCRE2_AUTO_CALLOUT is or-ed in on top of whatever is given.
         *
         * \param _reg_str Pattern text, stored as given and returned by get_pattern()
         * \param _opt Compile options; 0 for the engine's plain Perl-compatible defaults
         */
        regex_pcre2(const std::string& _reg_str, uint_32 _opt = 0) noexcept;
        /// Take over the compiled pattern, the match block and the hook; _regx is left invalid
        regex_pcre2(regex_pcre2&& _regx) noexcept;
        /// Release what this holds, then take over _regx's as above
        regex_pcre2& operator=(regex_pcre2&& _regx) noexcept;

    public:

        /**
         * \brief True when the whole subject matches the pattern
         *
         * Matching is anchored at both ends, so a matching prefix or substring is not enough --
         * but the engine may still backtrack to satisfy the end anchor: the pattern a|ab is
         * compliant with "ab".
         *
         * \return false both when the subject does not match and when the object is invalid
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
         *         wide, so the two cases are told apart by last_status(), not by the return value
         */
        std::string find(const char* _ptr, uint_64 _size = uint_64_npos) const;

        /// Same, over the whole string
        std::string find(const std::string& _str) const { return find(_str.c_str(), _str.length()); }

        /**
         * \brief Text of every non-overlapping match, left to right
         *
         * The same scan find_all_spans() walks, reported as text instead of offsets, so the two
         * agree entry for entry. A zero-width match does not stall the scan: the next one is
         * first tried at the same position and only then one byte on.
         *
         * \return One entry per match, in subject order; the entries a cut-short scan collected
         *         so far (check last_status() for limit / interrupted / error)
         */
        std::vector<std::string> find_all(const char* _ptr, uint_64 _size = uint_64_npos) const;

        /// Same, over the whole string
        std::vector<std::string> find_all(const std::string& _str) const { return find_all(_str.c_str(), _str.length()); }

        /**
         * \brief Offsets of every non-overlapping match, left to right
         *
         * The one scan find_all() reports as text, so the two agree entry for entry. Each entry
         * is {offset, length} in bytes from the start of the subject, and an offset cannot be
         * read off the text: a zero-width match lands wherever its assertion puts it, not
         * necessarily at the end of the previous match.
         *
         * \return {offset, length} per match, in subject order; a cut-short scan keeps what it
         *         collected. Status afterwards is the last attempt's: no_match for a scan that
         *         ran to the end (it found nothing on that attempt, however many entries came
         *         before), ok when that attempt matched zero width at the subject end, limit /
         *         interrupted / error when the scan was stopped
         */
        std::vector<std::pair<uint_64, uint_64>> find_all_spans(const char* _ptr, uint_64 _size = uint_64_npos) const;

        /// Same, over the whole string
        std::vector<std::pair<uint_64, uint_64>> find_all_spans(const std::string& _str) const {
            return find_all_spans(_str.c_str(), _str.length());
        }

    public:

        /**
         * \brief Groups of the first match
         *
         * Index 0 is the whole match, then g_1..g_n in pattern order. A group that took no part
         * in the match is an empty string, so the vector is empty only when nothing matched at
         * all, or when the object is invalid. With nosubs the pattern has no capture groups and
         * only g_0 comes back.
         *
         * \return {g_0, ..., g_n}, empty when the pattern does not match
         */
        std::vector<std::string> match_group(const char* _ptr, uint_64 _size = uint_64_npos) const;

        /// Same, over the whole string
        std::vector<std::string> match_group(const std::string& _str) const { return match_group(_str.c_str(), _str.length()); }

        /**
         * \brief Groups of every non-overlapping match, concatenated match by match
         *
         * The same scan and the same group layout as match_group(), so the vector chunks by the
         * pattern's capture-group count plus one.
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
         * _repl is PCRE2's replacement dialect: $n / ${n} name a numbered group, $<name> /
         * ${name} a named one, and $0, $& and $_ the whole match. With the default substitute
         * options a reference to a group that exists but took no part substitutes the empty
         * string; one to a group that does not exist fails the call -- error status, subject
         * returned unchanged. The match obeys this instance's limits and hook, an abort returning
         * the subject as well.
         *
         * \return The subject with its first match replaced; a copy of _str when the pattern does
         *         not match, still reported as ok
         */
        std::string replace(const std::string& _str, const std::string& _repl) const;

        /**
         * \brief Replace every non-overlapping match, left to right
         *
         * The same replacement dialect as replace(), under the same limits and hook; the walk
         * over the matches is PCRE2's own PCRE2_SUBSTITUTE_GLOBAL, added on top of the configured
         * substitute options.
         *
         * \return The subject with all its matches replaced; a copy of _str when the pattern does
         *         not match, still reported as ok
         */
        std::string replace_all(const std::string& _str, const std::string& _repl) const;

    public:

        /**
         * \brief Cap a single match by backtracking steps; 0 restores the engine default (10^7)
         *
         * Every attempt gets the budget on its own -- an unanchored search restarts the count at
         * each position, so a scan of N positions gets N budgets. Reaching it aborts the match in
         * progress (limit status, code -47), leaving any match an earlier attempt of the same
         * call found standing. Values past 2^32-1 clamp; a no-op on an invalid object.
         *
         * \param _steps Step cap; 0 puts the engine's build-time default back
         */
        void set_step_limit(uint_64 _steps);

        /**
         * \brief Cap a single match by backtracking depth; 0 restores the engine default (the
         *        step limit's value)
         *
         * Depth bounds the nested backtracking points a match may hold at once, so it bounds the
         * memory it uses as well. The abort lands in the same status as a step-limit one but
         * reports -53. Clamping and the no-op on an invalid object are as in set_step_limit().
         *
         * \param _depth Depth cap; 0 puts the engine's build-time default back
         */
        void set_depth_limit(uint_64 _depth);

        /**
         * \brief Interrupt hook, called while a match runs; returning false aborts it with
         *        match_status::interrupted
         *
         * PCRE2_AUTO_CALLOUT is compiled in, so the hook is called before every pattern item --
         * often, on a long match. It is handed only _ud (the engine's own callout block is not
         * exposed) and runs on the thread that called the matcher, inside every path:
         * is_compliant(), find(), the scans and the replacements.
         *
         * The binding that holds _ud is owned by this object and freed by the next set_callout()
         * or by destruction, so _ud must outlive the hook -- and calling this while a match is in
         * flight is a use-after-free, not just a race: the old binding goes as the new one is
         * installed.
         *
         * \param _fn Predicate; false stops the match. Null clears the hook
         * \param _ud Caller data handed back to _fn, unread by the class
         */
        void set_callout(bool (*_fn)(void*), void* _ud);

        /**
         * \brief Raw PCRE2_SUBSTITUTE_* bits for replace() and replace_all(); the default is
         *        UNSET_EMPTY
         *
         * The bits given are passed through as they are: PCRE2_SUBSTITUTE_GLOBAL is added by
         * replace_all() and PCRE2_SUBSTITUTE_OVERFLOW_LENGTH by both, so those two are not the
         * caller's to set. Clearing UNSET_EMPTY (passing 0) turns a reference to a group that
         * took no part into a failed call instead of an empty substitution.
         *
         * \param _opt Option bits, stored as given and used by the next replacement
         */
        void set_substitute_options(uint_32 _opt);

    public:
        /// True when a compiled pattern is held -- false before the first compile, after a
        /// rejected pattern and after a move-out; a matcher called anyway answers empty rather
        /// than misbehaving
        bool is_valid() const { return nullptr != code_; }
        /// The pattern text exactly as constructed; kept even when the compile failed. A
        /// reference into this object: use it while the object lives and is not assigned to
        const std::string& get_pattern() const { return pattern; }
        /**
         * \brief Why the last engine call ended the way it did
         *
         * Every matcher records here, so the answer belongs to the last call made, and an empty
         * result alone does not tell "nothing matched" from "gave up" or "never ran": a fresh
         * instance reports ok. A call made on an invalid object answers empty without touching
         * the record, leaving the compile failure standing.
         */
        match_status last_status() const { return status_; }
        /**
         * \brief The engine's error code for the last failing call; 0 when the last call
         *        succeeded
         *
         * Both signs occur: a rejected pattern leaves a positive PCRE2 compile-error code, while
         * a match or replacement failure is negative -- -1 nothing matched, -47 step limit, -53
         * depth limit, -37 the hook. Because a call on an invalid object never runs the engine,
         * the compile code is what stands there.
         */
        int_32 last_error_code() const { return last_err_; }

    private:
        void release() noexcept;
        void note_result(int _rc) const;
        std::string substitute(const std::string& _str, const std::string& _repl, bool _global) const;

    private:
        void* code_{nullptr};
        void* mctx_{nullptr};
        void* mdata_{nullptr};
        void* callout_{nullptr};
        uint_32 sub_opt_{0x00000400u};
        mutable int_32 last_err_{0};
        mutable match_status status_{match_status::ok};
        std::string pattern;
    };
}

#endif
