/*****************************************************************/ /**
 * \file   aanyptr.h
 * \brief  Type-erased C++ object wrapper for script link modules
 *
 * \author alexis
 *
 * Copyright (c) 2026 AlexisVon
 * SPDX-License-Identifier: MIT
 ******************************************************************************/
#ifndef _ALEXIS_anyptr_H_
#define _ALEXIS_anyptr_H_

#include "arefcount.h"
#include "autility.h"
#include <atomic>

namespace alx {

    /**
     * \brief Registration record of a type: how to delete it, how to clone it, how it is told apart
     *
     * Filled in once per type by anyptr_register_impl and pointed at -- never copied -- by every
     * handle made from it, so it must outlive them.
     */
    struct anyptr_reg {
        /// Deletes a pointee of the registered type
        typedef void (*del_fun)(void*);
        /// Clones the source object into a fresh one and writes it to the destination pointer
        typedef void (*cpy_fun)(const void*, void*&);
        /// Deleter the handle runs on its pointee when the last owner goes
        del_fun del;
        /// Clone hook; null for a type that cannot be copied, so a copy of such a handle is null
        cpy_fun cpy;
        /// Type identity for anyptr_ex<T>::as(): the same T hashes alike across translation units
        uint_64 rid;
    };
    /**
     * \brief Type-erased owning handle to a C++ object
     *
     * The handle owns the pointee and deletes it through the registered deleter once its last
     * owner is gone; the registration record is only pointed at, so it must outlive the handle.
     * make() gives an exclusive handle: a copy clones through the registered cpy, and a type
     * without one leaves that copy null. make_ref() gives a shared handle: copies point at the
     * same object and the last of them deletes it. Copying and destroying handles of one pointee
     * from different threads is safe -- the counter is atomic -- but the pointee is not protected.
     */
    class anyptr {
    public:
        /// Construct a null handle: nothing owned, and no registration record to reach
        anyptr() = default;
        /**
         * \brief Take ownership of _obj, deleted through _reg
         *
         * \param _obj Object to own; it is deleted through _reg->del, so it must come from new
         * \param _reg Record of the type; stored, not copied, and must not be null -- it is what
         *             deletes the object and what ties the handle to T
         */
        anyptr(void* _obj, const anyptr_reg* _reg) {
            m_hdl.obj_ = _obj;
            m_hdl.reg_ = _reg;
            m_hdl.ref_ = nullptr;
        }
        /**
         * \brief Take ownership of _obj and share it through _ref
         *
         * \param _obj Object to own; it is deleted through _reg->del, so it must come from new
         * \param _reg Record of the type; stored, not copied, and must not be null
         * \param _ref Counter to take over, already counting this handle as its owner; null makes
         *             the handle allocate one of its own, held by this handle alone
         */
        anyptr(void* _obj, const anyptr_reg* _reg, ref_count* _ref) {
            m_hdl.obj_ = _obj;
            m_hdl.reg_ = _reg;
            if (_ref) m_hdl.ref_ = _ref;
            else (m_hdl.ref_ = new ref_count())->init_owned();
        }

        /// Release the handle: the pointee is deleted once no other owner is left
        ~anyptr() { release(); }

        /**
         * \brief Copy a handle: share a ref-counted pointee, clone any other
         *
         * The copy of a make_ref() handle points at the same object. For a make() handle it is a
         * deep clone through the registered cpy; when the type has none -- it is not
         * copy-constructible -- the copy comes out null and the source keeps the object.
         */
        anyptr(const anyptr& _other) { copy_from(_other); }

        /// Copy-assign: the held pointee is released first; self-assignment is a no-op
        anyptr& operator=(const anyptr& _other) {
            if (this == &_other) return *this;
            release();
            copy_from(_other);
            return *this;
        }

        /// Move-construct: the handle is stolen and _other is left null, owning nothing
        anyptr(anyptr&& _other) noexcept : m_hdl(_other.m_hdl) {
            _other.m_hdl.obj_ = nullptr;
            _other.m_hdl.ref_ = nullptr;
        }

        /// Move-assign: release what is held, then steal _other's handle; self-move is a no-op
        anyptr& operator=(anyptr&& _other) noexcept {
            if (this == &_other) return *this;
            release();
            m_hdl = _other.m_hdl;
            _other.m_hdl.obj_ = nullptr;
            _other.m_hdl.ref_ = nullptr;
            return *this;
        }

        /// True when both hold the same pointer; type and counter are not compared
        bool operator==(const anyptr& _other) const { return m_hdl.obj_ == _other.m_hdl.obj_; }
        /// Negation of operator==
        bool operator!=(const anyptr& _other) const { return m_hdl.obj_ != _other.m_hdl.obj_; }

        /**
         * \brief True when no object is held
         *
         * Answered by the default-constructed handle, by a moved-from one, and by a copy whose
         * type has no clone hook to make the copy with.
         */
        bool null() const { return nullptr == m_hdl.obj_; }
        /// True when the handle shares its pointee through a counter, as make_ref() hands it out
        bool is_ref() const { return nullptr != m_hdl.ref_; }
        /// True when more than one handle holds the pointee; a snapshot of the atomic count
        bool shared() const { return nullptr != m_hdl.ref_ && m_hdl.ref_->is_shared(); }

    private:
        template <typename T> friend class anyptr_ex;

        struct ptrpkg {
            void* obj_ = nullptr;
            ref_count* ref_ = nullptr;
            const anyptr_reg* reg_ = nullptr;
        } m_hdl;

        void release() {
            if (m_hdl.ref_) {
                if (m_hdl.obj_) {
                    if (!m_hdl.ref_->deref()) {
                        m_hdl.reg_->del(m_hdl.obj_);
                        delete m_hdl.ref_;
                        return;
                    }
                } else {
                    delete m_hdl.ref_;
                }
            } else if (m_hdl.obj_ && m_hdl.reg_) {
                m_hdl.reg_->del(m_hdl.obj_);
            }
        }

        void copy_from(const anyptr& _other) {
            m_hdl.reg_ = _other.m_hdl.reg_;
            m_hdl.ref_ = _other.m_hdl.ref_;
            if (_other.m_hdl.ref_) {
                m_hdl.ref_->ref();
                m_hdl.obj_ = _other.m_hdl.obj_;
            } else if (_other.m_hdl.reg_ && _other.m_hdl.reg_->cpy) {
                _other.m_hdl.reg_->cpy(_other.m_hdl.obj_, m_hdl.obj_);
            } else {
                m_hdl.obj_ = nullptr;
            }
        }
    };

    /**
     * \brief Registration holder: fills a record with the deleter, the clone and the type id
     *
     * The primary carries the clone hook; the specialization below is what a type that cannot be
     * copied instantiates, leaving cpy null. anyptr_register<T> picks between the two on
     * std::is_copy_constructible_v<T>.
     */
    template <typename T, bool COPY_ALBE>
    struct anyptr_register_impl {
        /// The record the constructor fills in; a handle keeps its address, not a copy
        anyptr_reg reg_;
        /// Fill the record in, hashing the compiler's signature for the instantiation into rid
        anyptr_register_impl() {
            reg_.del = del;
            reg_.cpy = cpy;
#ifdef _MSC_VER
            reg_.rid = fnv1a_64(__FUNCSIG__);
#else
            reg_.rid = fnv1a_64(__PRETTY_FUNCTION__);
#endif
        }
        /// Delete a pointee of this instantiation as a T
        static void del(void* p) { delete static_cast<T*>(p); }
        /// Clone a pointee into a raw destination; a copy of a make() handle goes through here
        static void cpy(const void* src, void*& dst) {
            dst = new T(*static_cast<const T*>(src));
        }
    };
    /**
     * \brief Registration holder of a type that cannot be copied
     *
     * Same record as the primary, with the clone hook left null: an anyptr that owns such an
     * object cannot be cloned, so a copy of it holds nothing.
     */
    template <typename T>
    struct anyptr_register_impl<T, false> {
        /// The record the constructor fills in; a handle keeps its address
        anyptr_reg reg_;
        /// Fill the record in, hashing the compiler's signature for the instantiation into rid
        anyptr_register_impl() {
            reg_.del = del;
            reg_.cpy = nullptr;
#ifdef _MSC_VER
            reg_.rid = fnv1a_64(__FUNCSIG__);
#else
            reg_.rid = fnv1a_64(__PRETTY_FUNCTION__);
#endif
        }
        /// Delete a pointee of this instantiation as a T
        static void del(void* p) { delete static_cast<T*>(p); }
    };
    /// Registration holder of T, picked by whether T is copy-constructible
    template <typename T>
    using anyptr_register = anyptr_register_impl<T, std::is_copy_constructible_v<T>>;

    /**
     * \brief Typed front end of anyptr: make handles for T and read them back
     *
     * The _register member is what ties a handle to T: as() reads a handle only when the record
     * it carries is this one, which is why a handle of another type answers null. Every handle
     * made here points at that record, a class static that lives for the whole program.
     */
    template <typename T>
    class anyptr_ex {
    public:
        /// Registration record of T, whose address every handle made here keeps
        static const anyptr_register<T> _register;

    public:
        /**
         * \brief Hand _obj over to a handle that owns it
         *
         * \param obj Object to own, allocated with new; the caller must not delete it afterwards
         * \return An exclusive handle: a copy of it clones the object, so a T that cannot be
         *         copied just makes such a copy null instead
         */
        static anyptr make(T* obj) {
            (void) _register;
            return anyptr(obj, &_register.reg_);
        }

        /**
         * \brief Hand _obj over to a handle that shares it with every copy
         *
         * Copies of the returned handle point at the same object -- no clone, whatever T is --
         * and it is deleted when the last of them goes. is_ref() is true right away, shared()
         * only once the first copy exists.
         *
         * \param obj Object to own, allocated with new; the caller must not delete it afterwards
         */
        static anyptr make_ref(T* obj) {
            (void) _register;
            auto* rc = new ref_count();
            rc->init_owned();
            return anyptr(obj, &_register.reg_, rc);
        }

        /**
         * \brief Read a handle back as a T*
         *
         * \param obj Handle to read; a default-constructed or moved-from anyptr carries no
         *            registration record and answers null
         * \return The pointee when the handle is one of this T, a null one included, which gives
         *         null; null as well for a handle with another type or with no record
         */
        static T* as(const anyptr& obj) {
            return nullptr != obj.m_hdl.reg_ && obj.m_hdl.reg_->rid == _register.reg_.rid ? static_cast<T*>(obj.m_hdl.obj_) : nullptr;
        }
    };
    /// Definition of the record the handles of T point at; one instance per program
    template <typename T>
    const anyptr_register<T> anyptr_ex<T>::_register;

}

#endif
