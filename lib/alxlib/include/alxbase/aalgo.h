/*****************************************************************/ /**
 * \file   aalgo.h
 * \brief  Byte-string search algorithms (KMP, Boyer-Moore, Rabin-Karp, brute-force)
 *
 * \author alexis
 *
 * Copyright (c) 2026 AlexisVon
 * SPDX-License-Identifier: MIT
 *********************************************************************/

#ifndef _ALEXIS_ALGO_H_
#define _ALEXIS_ALGO_H_

#include "autility.h"

namespace alx {

    /**
     * \brief Offset of the first occurrence of a byte pattern
     *
     * The window is [_from, min(_dsz, _to)): _to is exclusive and clamped to the block, _from is
     * not, and the match must lie entirely inside it. A zero length pattern matches at _from. The
     * pattern width and the window length pick the kernel -- 1, 2, 4 and 8 byte patterns go to a
     * whole-value compare, then brute force, Rabin-Karp and Boyer-Moore take over as they grow.
     *
     * \param _dst Block to search; _dsz is its length in bytes
     * \param _val Pattern of _vsz bytes, at any alignment
     * \param _from Offset a match may start at; 0 = the block's start
     * \param _to Offset a match may not reach; clamped to _dsz, uint_64_npos = the whole block
     * \return Offset of the match's first byte; uint_64_npos when the window holds no match, when
     *         _dsz is 0, when _val is null, or when the window is shorter than the pattern
     */
    ALXBASE_API uint_64 find(
        const void* _dst, const uint_64 _dsz,
        const void* _val, const uint_64 _vsz,
        const uint_64 _from, const uint_64 _to);

    /**
     * \brief Offset of the last occurrence of a byte pattern
     *
     * The scan runs from the end: the first candidate is the match whose last byte sits at
     * min(_dsz - 1, _rfrom). A match is reported only when its last byte is at or below _rfrom and
     * its first byte at or above _rto; both bounds are inclusive and anchor the pattern's two ends,
     * not merely its start. A zero length pattern matches at min(_dsz - 1, _rfrom), and the width
     * and the window length pick the kernel exactly as in find().
     *
     * \param _dst Block to search; _dsz is its length in bytes
     * \param _val Pattern of _vsz bytes, at any alignment
     * \param _rfrom Highest offset the pattern's last byte may occupy; clamped to _dsz - 1,
     *               uint_64_npos = the end of the block
     * \param _rto Lowest offset the pattern's first byte may occupy; 0 = the block's start
     * \return Offset of the match's first byte, the highest one that fits; uint_64_npos when the
     *         window holds no match, when _dsz is 0, when _val is null, or when the window is
     *         shorter than the pattern
     */
    ALXBASE_API uint_64 rfind(
        const void* _dst, const uint_64 _dsz,
        const void* _val, const uint_64 _vsz,
        const uint_64 _rfrom, const uint_64 _rto);

    /**
     * \brief Offset of the first occurrence of any one of several byte patterns
     *
     * Every candidate is hashed up front and matched in a single pass, so this costs less than one
     * find() per candidate; the earliest offset wins. Candidates that compare equal are not
     * distinguished, so pass distinct values. A zero length _vsz matches at _from and reports
     * candidate 0.
     *
     * \param _dst Block to search; _dsz is its length in bytes
     * \param _val_lst Array of _val_lsz pointers, each to a candidate of _vsz bytes
     * \param _vsz Length every candidate shares
     * \param _from Offset a match may start at; 0 = the block's start
     * \param _to Offset a match may not reach; clamped to _dsz, uint_64_npos = the whole block
     * \return {offset of the match's first byte, index into _val_lst}; {uint_64_npos, uint_64_npos}
     *         when the window holds no match, when _dsz is 0, when _val_lst is null, when _val_lsz
     *         is 0, or when the window is shorter than a candidate
     */
    ALXBASE_API std::pair<uint_64, uint_64> find_enum(
        const void* _dst, const uint_64 _dsz,
        const void** _val_lst, const uint_64 _val_lsz, const uint_64 _vsz,
        const uint_64 _from, const uint_64 _to);

    /**
     * \brief Offset of the last occurrence of any one of several byte patterns
     *
     * The mirror of rfind(): the first candidate is the one whose last byte sits at
     * min(_dsz - 1, _rfrom), a match is reported only when its last byte is at or below _rfrom and
     * its first byte at or above _rto, and the highest offset wins. Candidates that compare equal
     * are not distinguished, so pass distinct values. A zero length _vsz matches at
     * min(_dsz - 1, _rfrom) and reports candidate 0.
     *
     * \param _dst Block to search; _dsz is its length in bytes
     * \param _val_lst Array of _val_lsz pointers, each to a candidate of _vsz bytes
     * \param _vsz Length every candidate shares
     * \param _rfrom Highest offset a candidate's last byte may occupy; clamped to _dsz - 1,
     *               uint_64_npos = the end of the block
     * \param _rto Lowest offset a candidate's first byte may occupy; 0 = the block's start
     * \return {offset of the match's first byte, index into _val_lst}; {uint_64_npos, uint_64_npos}
     *         when the window holds no match, when _dsz is 0, when _val_lst is null, when _val_lsz
     *         is 0, or when the window is shorter than a candidate
     */
    ALXBASE_API std::pair<uint_64, uint_64> rfind_enum(
        const void* _dst, const uint_64 _dsz,
        const void** _val_lst, const uint_64 _val_lsz, const uint_64 _vsz,
        const uint_64 _rfrom, const uint_64 _rto);

    /**
     * \brief The search kernels find(), rfind() and the _enum pair dispatch to
     *
     * Brute force, Rabin-Karp and Boyer-Moore cover the whole family; the _enum pair has a
     * multi-pattern hashing kernel of its own, and KMP is never dispatched to -- it serves a caller
     * that picks it directly. A kernel receives the window its wrapper already resolved, as plain
     * offsets: _beg inclusive, _end exclusive, both inside _dst, and the window holds at least one
     * pattern. _len is at least 1, and no kernel validates any of that.
     *
     * The reverse kernels read _rbeg as the highest offset the pattern's last byte may occupy and
     * _rend as the lowest offset its first byte may occupy, both inclusive. Every kernel returns
     * the match's first byte, or uint_64_npos when the window holds no match.
     */
    namespace find_func {

        /**
         * \brief KMP failure table of a sequence
         *
         * lps[i] is the length of the longest proper prefix of _bunch[0..i] that is also a suffix
         * of it. O(_len) time and space, whatever T is; entries compare with ==.
         *
         * \return Table of _len entries; entry 0 is always 0
         */
        template <typename T>
        inline std::vector<uint_64> compute_lps(const T* _bunch, uint_64 _len) {
            std::vector<uint_64> lps(_len);
            uint_64 len{0}, index{1};
            while (index < _len) {
                if (_bunch[index] == _bunch[len]) {
                    len++;
                    lps[index] = len;
                    index++;
                } else {
                    if (0 != len) len = lps[len - 1];
                    else {
                        lps[index] = 0;
                        index++;
                    }
                }
            }
            return lps;
        }

        /**
         * \brief KMP failure table of the reversed sequence
         *
         * Entry i describes the _len - 1 - i tail: this is compute_lps() applied to _bunch read
         * backwards, and it is the table the reverse kernels index.
         *
         * \return Table of _len entries; entry 0 is always 0
         */
        template <typename T>
        inline std::vector<uint_64> compute_rlps(const T* _bunch, uint_64 _len) {
            std::vector<uint_64> lps(_len);
            uint_64 len{0}, index{1};
            const uint_64 minus_1 = _len - 1;
            while (index < _len) {
                if (_bunch[minus_1 - index] == _bunch[minus_1 - len]) {
                    len++;
                    lps[index] = len;
                    index++;
                } else {
                    if (0 != len) len = lps[len - 1];
                    else {
                        lps[index] = 0;
                        index++;
                    }
                }
            }
            return lps;
        }

        /**
         * \brief First offset of a single value of width sizeof(T)
         *
         * The window is [_beg, _end), as in find(); _end must leave room for one whole T. Both
         * sides are read with memcpy, so neither pointer needs alignment, and _val is compared as
         * a whole value whatever T is.
         *
         * \return Offset of the first T equal to *_val, or uint_64_npos when the window holds none
         */
        template <typename T>
        uint_64 find_by_val(const void* _dst, const void* _val, uint_64 _beg, uint_64 _end) {
            _end -= sizeof(T);
            const T val = m_interpret<T>(_val);
            for (uint_64 i = _beg; i <= _end; ++i)
                if (val == m_interpret<T>((const uint_8*) _dst + i)) return i;
            return uint_64_npos;
        }
        /**
         * \brief Last offset of a single value of width sizeof(T)
         *
         * The reverse of find_by_val: _rbeg is the highest offset the value's last byte may occupy
         * and _rend the lowest offset its first byte may occupy, so the highest start tested is
         * _rbeg - sizeof(T) + 1.
         *
         * \return Offset of the highest start whose bytes equal *_val, or uint_64_npos when the
         *         window holds none
         */
        template <typename T>
        uint_64 rfind_by_val(const void* _dst, const void* _val, uint_64 _rbeg, uint_64 _rend) {
            _rbeg -= sizeof(T);
            const T val = m_interpret<T>(_val);
            for (uint_64 i = _rbeg + 2; i-- > _rend;)
                if (val == m_interpret<T>((const uint_8*) _dst + i)) return i;
            return uint_64_npos;
        }
        /**
         * \brief First offset matching any of several values of width sizeof(T)
         *
         * The window is [_beg, _end), as in find(). Candidates that compare equal share one map
         * entry, kept under the last of their indices; pass distinct values.
         *
         * \param _val_lst Array of _val_lsz pointers, each to a T
         * \return {offset of the first matching T, index into _val_lst}, or {uint_64_npos,
         *         uint_64_npos} when the window holds none
         */
        template <typename T>
        std::pair<uint_64, uint_64> find_enum_by_val(const void* _dst, const void** _val_lst, const uint_64 _val_lsz, uint_64 _beg, uint_64 _end) {
            _end -= sizeof(T);
            std::unordered_map<T, uint_64> _val_map;
            for (uint_64 i = 0; i < _val_lsz; i++) _val_map[*(((const T**) _val_lst)[i])] = i;
            for (uint_64 i = _beg; i <= _end; ++i) {
                auto iter = _val_map.find(m_interpret<T>((const uint_8*) _dst + i));
                if (iter != _val_map.end()) return {i, iter->second};
            }
            return {uint_64_npos, uint_64_npos};
        }
        /**
         * \brief Last offset matching any of several values of width sizeof(T)
         *
         * The reverse of find_enum_by_val: _rbeg is the highest offset a value's last byte may
         * occupy and _rend the lowest offset its first byte may occupy. Candidates that compare
         * equal report the last of their indices; pass distinct values.
         *
         * \param _val_lst Array of _val_lsz pointers, each to a T
         * \return {offset of the highest matching T, index into _val_lst}, or {uint_64_npos,
         *         uint_64_npos} when the window holds none
         */
        template <typename T>
        std::pair<uint_64, uint_64> rfind_enum_by_val(const void* _dst, const void** _val_lst, const uint_64 _val_lsz, uint_64 _rbeg, uint_64 _rend) {
            _rbeg -= sizeof(T);
            std::unordered_map<T, uint_64> _val_map;
            for (uint_64 i = 0; i < _val_lsz; i++) _val_map[*(((const T**) _val_lst)[i])] = i;
            for (uint_64 i = _rbeg + 2; i-- > _rend;) {
                auto iter = _val_map.find(m_interpret<T>((const uint_8*) _dst + i));
                if (iter != _val_map.end()) return {i, iter->second};
            }
            return {uint_64_npos, uint_64_npos};
        }

        /**
         * \brief Knuth-Morris-Pratt: O(n + m) in the worst case, with no backtracking
         *
         * The block is walked once and never re-read, which is what a streaming caller wants; the
         * price is the failure table, rebuilt in O(m) time and space on every call.
         *
         * \return Offset of the match's first byte; uint_64_npos when the window holds none
         */
        ALXBASE_API uint_64 find_by_kmp(const void* _dst, const void* _val, uint_64 _len, uint_64 _beg, uint_64 _end);

        /**
         * \brief Rabin-Karp: one rolling hash over the window, byte-compared on a hash hit
         *
         * O(n + m) on ordinary data and O(n * m) in the worst case. Every alignment is hashed, so
         * the pattern length does not change the number of steps -- the kernel the wrapper picks
         * for medium patterns in a short window, and the shape the multi-pattern search copies.
         *
         * \return Offset of the match's first byte; uint_64_npos when the window holds none
         */
        ALXBASE_API uint_64 find_by_rabin_karp(const void* _dst, const void* _val, uint_64 _len, uint_64 _beg, uint_64 _end);

        /**
         * \brief Boyer-Moore: a bad-character skip table, sublinear on a large alphabet
         *
         * The table is built from the pattern's last min(_len, 255) bytes and can jump up to _len
         * alignments at once, so a long pattern over a long window costs well under the window
         * length; the worst case stays O(n * m). The bad-character rule only -- with no good-suffix
         * table, a repetitive pattern gives the skips away.
         *
         * \return Offset of the match's first byte; uint_64_npos when the window holds none
         */
        ALXBASE_API uint_64 find_by_boyer_moore(const void* _dst, const void* _val, uint_64 _len, uint_64 _beg, uint_64 _end);

        /**
         * \brief Brute force: one memcmp per alignment
         *
         * O(n * m) in the worst case and the highest cost per rejected alignment, but a memcmp
         * rules out a candidate inside a single call -- the cheapest kernel for a short pattern in
         * a short window, which is the only place the wrapper uses it.
         *
         * \return Offset of the match's first byte; uint_64_npos when the window holds none
         */
        ALXBASE_API uint_64 find_by_brute_force(const void* _dst, const void* _val, uint_64 _len, uint_64 _beg, uint_64 _end);
        /// Knuth-Morris-Pratt from the end: candidates begin at the one whose last byte is at _rbeg
        /// and step down; same worst case, with the failure table built on the reversed pattern
        ALXBASE_API uint_64 rfind_by_kmp(const void* _dst, const void* _val, uint_64 _len, uint_64 _rbeg, uint_64 _rend);
        /// Rabin-Karp from the end: the same candidates, with the hash rolling one byte down
        /// instead of up, at the same expected cost
        ALXBASE_API uint_64 rfind_by_rabin_karp(const void* _dst, const void* _val, uint_64 _len, uint_64 _rbeg, uint_64 _rend);
        /// Boyer-Moore from the end: the same candidates, with the skip table over the pattern's
        /// leading bytes and the same best case
        ALXBASE_API uint_64 rfind_by_boyer_moore(const void* _dst, const void* _val, uint_64 _len, uint_64 _rbeg, uint_64 _rend);
        /// Brute force from the end: the same candidates, one memcmp per alignment
        ALXBASE_API uint_64 rfind_by_brute_force(const void* _dst, const void* _val, uint_64 _len, uint_64 _rbeg, uint_64 _rend);
    }
}

#endif
