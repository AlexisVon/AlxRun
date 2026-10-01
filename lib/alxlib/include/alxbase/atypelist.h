/*****************************************************************/ /**
 * \file   atypelist.h
 * \brief  Compile-time type list (template metaprogramming)
 *
 * \author alexis
 *
 * Copyright (c) 2026 AlexisVon
 * SPDX-License-Identifier: MIT
 *********************************************************************/

#ifndef _ALEXIS_TYPELIST_H_
#define _ALEXIS_TYPELIST_H_

#include "autility.h"

namespace alx {
    namespace typelist {

        /// Sentinel for "no type to name": declared only, never instantiated
        struct nulltype;
        /**
         * \brief Compile-time list of types
         *
         * Declared only, never defined: the list exists to carry a type pack into the traits
         * below, so it is only ever passed around as a type and cannot be instantiated.
         */
        template <typename... Args> struct type_list;
        template <typename H, typename... T> struct type_list<H, T...>;

        template <class L> struct __length_of;
        template <>
        struct __length_of<type_list<>> {
            enum : int { value = 0 };
        };
        template <typename H, typename... T>
        struct __length_of<type_list<H, T...>> {
            enum : int { value = __length_of<type_list<T...>>::value + 1 };
        };
        /// Number of types the list holds
        template <class L>
        constexpr int length_of = __length_of<L>::value;

        template <typename L, int index> struct __value_at;
        template <int index>
        struct __value_at<type_list<>, index> {
            /// Run off the end: nulltype
            typedef nulltype value;
        };
        template <typename H, typename... T>
        struct __value_at<type_list<H, T...>, 0> {
            /// 0 names the head
            typedef H value;
        };
        template <typename H, typename... T, int index>
        struct __value_at<type_list<H, T...>, index> {
            /// Otherwise the index counts down as the tail is walked
            typedef typename __value_at<type_list<T...>, index - 1>::value value;
        };
        /**
         * \brief Type named by a 0-based index
         *
         * An index the list does not have -- past the end, or negative -- yields nulltype rather
         * than a diagnostic, so a computed index must be checked before the result is used.
         */
        template <typename L, int index>
        using value_at = typename __value_at<L, index>::value;

        template <typename L, typename U> struct __index_of;
        template <typename U>
        struct __index_of<type_list<>, U> {
            enum : int { value = -1 };
        };
        template <typename H, typename... T>
        struct __index_of<type_list<H, T...>, H> {
            enum : int { value = 0 };
        };
        template <typename H, typename... T, typename U>
        struct __index_of<type_list<H, T...>, U> {
        private:
            enum : int { temp = __index_of<type_list<T...>, U>::value };

        public:
            enum : int { value = temp == -1 ? -1 : temp + 1 };
        };
        /// Position of the first U in the list, or -1 when the list holds no U
        template <typename L, typename U>
        constexpr int index_of = __index_of<L, U>::value;

        template <typename L, typename U> struct __append_on;
        template <typename... Args, typename U>
        struct __append_on<type_list<Args...>, U> {
            /// U as one trailing element
            typedef type_list<Args..., U> value;
        };
        template <typename... Args, typename... U>
        struct __append_on<type_list<Args...>, type_list<U...>> {
            /// U spliced flat: its elements are appended one by one
            typedef type_list<Args..., U...> value;
        };
        /**
         * \brief Append U to the end of the list
         *
         * A U that is itself a type_list is spliced rather than nested: its elements are appended
         * one by one, so the list stays flat.
         */
        template <typename L, typename U>
        using append_on = typename __append_on<L, U>::value;

        template <typename L, typename U> struct __erase_one;
        template <typename U>
        struct __erase_one<type_list<>, U> {
            /// Empty list: nothing to erase
            typedef type_list<> value;
        };
        template <typename H, typename... T>
        struct __erase_one<type_list<H, T...>, H> {
            /// Head matched: dropped, and the tail is left alone (one occurrence only)
            typedef type_list<T...> value;
        };
        template <typename H, typename... T, typename U>
        struct __erase_one<type_list<H, T...>, U> {
            /// Head kept; the one U can only be in the tail
            typedef append_on<
                type_list<H>,
                typename __erase_one<type_list<T...>, U>::value>
                value;
        };
        /// Drop the first occurrence of U; the list comes back as it was when it holds no U
        template <typename L, typename U>
        using erase_one = typename __erase_one<L, U>::value;

        template <typename L, typename U> struct __erase_all;
        template <typename U>
        struct __erase_all<type_list<>, U> {
            /// Empty list: nothing to erase
            typedef type_list<> value;
        };
        template <typename H, typename... T>
        struct __erase_all<type_list<H, T...>, H> {
            /// Head matched: dropped, and the tail is searched for more
            typedef typename __erase_all<type_list<T...>, H>::value value;
        };
        template <typename H, typename... T, typename U>
        struct __erase_all<type_list<H, T...>, U> {
            /// Head kept, and the tail searched for more
            typedef append_on<
                type_list<H>,
                typename __erase_all<type_list<T...>, U>::value>
                value;
        };
        /// Drop every occurrence of U
        template <typename L, typename U>
        using erase_all = typename __erase_all<L, U>::value;

        template <typename L> struct __remove_dup;
        template <>
        struct __remove_dup<type_list<>> {
            /// Empty list: nothing to collapse
            typedef type_list<> value;
        };
        template <typename H, typename... T>
        struct __remove_dup<type_list<H, T...>> {
            /// Keep the head, drop its later copies, then collapse what is left
            typedef append_on<
                type_list<H>,
                typename __remove_dup<erase_all<type_list<T...>, H>>::value>
                value;
        };
        /// Drop repeated types, keeping the first occurrence of each
        template <typename L>
        using remove_dup = typename __remove_dup<L>::value;

        template <typename L, typename S, typename U> struct __replace_one;
        template <typename S, typename U>
        struct __replace_one<type_list<>, S, U> {
            /// Empty list: nothing to replace
            typedef type_list<> value;
        };
        template <typename H, typename... T, typename U>
        struct __replace_one<type_list<H, T...>, H, U> {
            /// First occurrence: replaced, and the search stops there
            typedef type_list<U, T...> value;
        };
        template <typename H, typename... T, typename S, typename U>
        struct __replace_one<type_list<H, T...>, S, U> {
            /// Head kept; the one S can only be in the tail
            typedef append_on<
                type_list<H>,
                typename __replace_one<type_list<T...>, S, U>::value>
                value;
        };
        /// Replace the first occurrence of S with U, if the list holds one
        template <typename L, typename S, typename U>
        using replace_one = typename __replace_one<L, S, U>::value;

        template <typename L, typename S, typename U> struct __replace_all;
        template <typename S, typename U>
        struct __replace_all<type_list<>, S, U> {
            /// Empty list: nothing to replace
            typedef type_list<> value;
        };
        template <typename H, typename... T, typename U>
        struct __replace_all<type_list<H, T...>, H, U> {
            /// Head replaced, and the tail searched for more
            typedef append_on<
                type_list<U>,
                typename __replace_all<type_list<T...>, H, U>::value>
                value;
        };
        template <typename H, typename... T, typename S, typename U>
        struct __replace_all<type_list<H, T...>, S, U> {
            /// Head kept, and the tail searched for more
            typedef append_on<
                type_list<H>,
                typename __replace_all<type_list<T...>, S, U>::value>
                value;
        };
        /// Replace every occurrence of S with U
        template <typename L, typename S, typename U>
        using replace_all = typename __replace_all<L, S, U>::value;

        template <typename L> struct __derived_most;
        template <>
        struct __derived_most<type_list<>> {
            /// Empty list: nulltype
            typedef nulltype value;
        };
        template <typename H>
        struct __derived_most<type_list<H>> {
            /// A single type is its own most-derived one
            typedef H value;
        };
        template <typename H, typename... T>
        struct __derived_most<type_list<H, T...>> {
        private:
            typedef typename __derived_most<type_list<T...>>::value candidate;

        public:
            /// H wins when the tail's candidate is one of its bases
            typedef std::conditional_t<std::is_base_of_v<candidate, H>, H, candidate> value;
        };
        /**
         * \brief Most-derived type of the list, under std::is_base_of
         *
         * Types unrelated to one another are not ordered by that test, and the last of them wins.
         * An empty list yields nulltype.
         */
        template <typename L>
        using derived_most = typename __derived_most<L>::value;

        template <typename L> struct __derived_sort;
        template <>
        struct __derived_sort<type_list<>> {
            /// Empty list: nothing to order
            typedef type_list<> value;
        };
        template <typename H, typename... T>
        struct __derived_sort<type_list<H, T...>> {
        private:
            typedef derived_most<type_list<H, T...>> min;
            typedef replace_one<type_list<T...>, min, H> temp;

        public:
            /// Emit min, then order the rest with H standing in min's slot
            typedef append_on<
                type_list<min>,
                typename __derived_sort<temp>::value>
                value;
        };
        /**
         * \brief Order the list so that a type comes before the types it derives from
         *
         * Selection sort using the same comparison as derived_most(), so only inheritance pairs
         * are ordered; unrelated types keep no meaningful position among themselves.
         */
        template <typename L>
        using derived_sort = typename __derived_sort<L>::value;

        template <class L, int FROM, int SIZE> struct __subseq_of;
        template <int FROM, int SIZE>
        struct __subseq_of<type_list<>, FROM, SIZE> {
            /// The list ran out: nothing left to take
            typedef type_list<> value;
        };
        template <typename H, typename... T, int FROM, int SIZE>
        struct __subseq_of<type_list<H, T...>, FROM, SIZE> {
            /// FROM at or below 0 takes the head and shortens SIZE; above 0 skips the head
            typedef std::conditional_t<
                FROM <= 0,
                std::conditional_t<0 == SIZE, type_list<>, append_on<type_list<H>, typename __subseq_of<type_list<T...>, FROM, SIZE - 1>::value>>,
                typename __subseq_of<type_list<T...>, FROM - 1, SIZE>::value>
                value;
        };
        /**
         * \brief SIZE types of the list, starting at FROM
         *
         * A FROM at or below 0 starts at the head. Running out of types is not an error: a SIZE
         * larger than what is left is truncated, and a FROM past the end gives an empty list.
         */
        template <typename L, int FROM, int SIZE>
        using subseq_of = typename __subseq_of<L, FROM, SIZE>::value;

        template <class L> struct __maxsize_of;
        template <>
        struct __maxsize_of<type_list<>> {
            enum : size_t { value = 0 };
        };
        template <typename H, typename... T>
        struct __maxsize_of<type_list<H, T...>> {
            enum : size_t { value = alx::max_value<size_t>(sizeof(H), __maxsize_of<type_list<T...>>::value) };
        };
        /// Largest sizeof among the types of the list; 0 when the list is empty
        template <class L>
        constexpr size_t maxsize_of = __maxsize_of<L>::value;

        template <class L> struct __sumsize_of;
        template <>
        struct __sumsize_of<type_list<>> {
            enum : size_t { value = 0 };
        };
        template <typename H, typename... T>
        struct __sumsize_of<type_list<H, T...>> {
            enum : size_t { value = sizeof(H) + __sumsize_of<type_list<T...>>::value };
        };
        /// Sum of sizeof over the types of the list; 0 when the list is empty
        template <class L>
        constexpr size_t sumsize_of = __sumsize_of<L>::value;
    }
}
#endif