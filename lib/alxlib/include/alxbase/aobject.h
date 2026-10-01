/*****************************************************************/ /**
 * \file   aobject.h
 * \brief  Base class for object management system
 *
 * \author alexis
 *
 * Copyright (c) 2026 AlexisVon
 * SPDX-License-Identifier: MIT
 *********************************************************************/

#ifndef _ALEXIS_OBJECT_H_
#define _ALEXIS_OBJECT_H_

#include "abase.h"
#include "autility.h"

namespace alx {
    /// Opaque per-object state; forward-declared only, never defined by the library
    struct object_data;

    /**
     * \brief Base class for objects linked under a parent object
     *
     * A derived object names its parent at construction and can relink with set_parent(); nullptr,
     * the default of both, means no parent. Only the declarations are shipped -- nothing in the
     * library defines the constructor, the destructor or set_parent(), so constructing, copying or
     * destroying an object fails to link.
     */
    class ALXBASE_API object {
    public:
        /**
         * \brief Construct an object, linked under _parent when one is given
         *
         * \param _parent Parent to link under; nullptr -- the default -- leaves it unlinked
         */
        explicit object(object* _parent = nullptr);
        /// Virtual: deleting through an object* reaches the derived destructor
        virtual ~object();

    public:
        /// Copy as object(nullptr): the copy starts with fresh state, not _obj's
        inline object(const object& _obj) : object() {}
        /// No-op: the target keeps its state, _obj is left alone
        inline object& operator=(const object& _obj) { return *this; }
        /// Take over _obj's state; _obj is left unlinked
        inline object(object&& _obj) noexcept : object() { std::swap(m_ptr, _obj.m_ptr); }
        /// Exchange state with _obj: what the target held passes to _obj
        inline object& operator=(object&& _obj) noexcept {
            std::swap(m_ptr, _obj.m_ptr);
            return *this;
        }

    protected:
        /**
         * \brief Relink the object to another parent
         *
         * Protected: only a derived class can relink an object. The argument has the same meaning
         * as the constructor's, so the default nullptr drops the parent link.
         */
        void set_parent(object* _parent = nullptr);

    private:
        object_data* m_ptr{nullptr};
    };
}

#endif