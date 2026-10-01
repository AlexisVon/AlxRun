/*****************************************************************/ /**
 * \file   arefcount.h
 * \brief  Atomic reference counter with static and unshareable states
 *
 * \author alexis
 *
 * Copyright (c) 2026 AlexisVon
 * SPDX-License-Identifier: MIT
 *********************************************************************/

#ifndef _ALEXIS_REFCOUNT_H_
#define _ALEXIS_REFCOUNT_H_

#include "abase.h"
#include <atomic>

namespace alx {

    /**
     * \brief Reference counter shared by every COW block
     *
     * It only reports ownership and never deallocates anything: the false return of deref() is
     * what hands the block over to the caller. static_ref (-1) is the immortal state, left
     * alone by both; unsharable (0) is a single owner that refuses ref() and so forces a COW
     * copy to deep-clone; a positive value counts the owners. Counting is atomic, so ref() and
     * deref() may be called from different threads; the counter itself is neither copyable nor
     * movable.
     */
    class ALXBASE_API ref_count {
    public:
        enum : int_32 { static_ref = -1,
                        /// Not shared: one owner, ref() refuses and a COW copy deep-clones
                        unsharable = 0,
                        /// One owner that may be shared; is_shared() is above this
                        shareable_ = 1 };

        /// Construct in any state; unsharable by default, so ref() refuses until init_owned()
        ref_count(int_32 _count = unsharable) : m_count(_count) {}

    public:

        /**
         * \brief Take an additional reference to the same block
         *
         * \return false when the counter is unsharable -- the block must not be shared and the
         *         caller has to deep-copy instead; true otherwise, including for an immortal
         *         static block, whose counter is left untouched
         */
        inline bool ref() {
            int_32 count = m_count.load();
            if (unsharable == count) return false;
            if (static_ref != count) ref_impl();
            return true;
        }

        /**
         * \brief Drop one reference to the same block
         *
         * \return true while the block still has an owner; false when this was the last one, or
         *         when the counter is unsharable. Either false case leaves the block with the
         *         caller, which must then deallocate it. An immortal static block always gives
         *         true.
         */
        inline bool deref() {
            int_32 count = m_count.load();
            if (unsharable == count) return false;
            if (static_ref == count) return true;
            return deref_impl();
        }

        /**
         * \brief Switch the counter between unsharable and a single shareable owner
         *
         * A single CAS, which only fires from the exact opposite state: a counter that has moved
         * on -- immortal, already shareable when _able is true, more than one owner when it is
         * false -- is left alone and the call returns false.
         *
         * \param _able true to accept further ref() calls, false to refuse them
         * \return true when the counter was in the opposite state and was switched
         */
        inline bool set_sharable(bool _able) {
            return _able ? cas_impl(unsharable, shareable_) : cas_impl(shareable_, unsharable);
        }
        /// True when ref() is accepted: unsharable is the only state that refuses
        inline bool is_sharable() const {
            return m_count.load() != unsharable;
        }
        /// True when the counter is immortal (static_ref), so the block is never deallocated
        inline bool is_static() const {
            return m_count.load() == static_ref;
        }

        /// True when more than one owner holds the block; a write in place would be seen by them
        inline bool is_shared() const {
            return m_count.load() > shareable_;
        }

        /// Reset to shareable with a single owner; a plain store, so nobody may hold it yet
        inline void init_owned() { m_count.store(shareable_); }

        /// Reset to unsharable: ref() refuses, deref() hands the block back to its single owner
        inline void init_unsharable() { m_count.store(unsharable); }
        /// Raw stored value, sentinels included: -1 immortal, 0 not shareable, otherwise owners
        inline int_32 value() const { return m_count.load(); }

        /**
         * \brief Compare the counter with _exp and store _new when the two match
         *
         * Sequentially consistent and strong. The value is replaced as a whole, sentinels
         * included, so an immortal or a single-owner state can be installed outright; a failed
         * swap reports nothing about the value that was actually there.
         *
         * \param _exp Value the counter must hold for the store to happen
         * \param _new Value to store
         * \return true when the counter was switched to _new
         */
        inline bool cas(int_32 _exp, int_32 _new) { return cas_impl(_exp, _new); }

    private:
        void ref_impl() { ++m_count; }
        bool deref_impl() { return --m_count != 0; }
        bool cas_impl(int_32 _exp, int_32 _new, int_32* _cur = nullptr,
                      std::memory_order _success = std::memory_order_seq_cst,
                      std::memory_order _failure = std::memory_order_seq_cst) {
            bool temp = m_count.compare_exchange_strong(_exp, _new, _success, _failure);
            if (nullptr != _cur) *_cur = _exp;
            return temp;
        }

    private:
        std::atomic<int_32> m_count;
    };
}

#endif