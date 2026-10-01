/*****************************************************************/ /**
 * \file   avariant.h
 * \brief  Type-erased variant and container types (varmap, varvec, varlst)
 *
 * \author alexis
 *
 * Copyright (c) 2026 AlexisVon
 * SPDX-License-Identifier: MIT
 *********************************************************************/

#ifndef _ALEXIS_VARIANT_H_
#define _ALEXIS_VARIANT_H_

#include "aanyptr.h"
#include "abase.h"
#include "abytes.h"
#include "atypelist.h"
#include "autility.h"

#include <algorithm>
#include <limits>
#include <list>
#include <map>
#include <string>
#include <vector>

namespace alx {

    typedef void (*__all_free_fptr)(void* _ptr);
    typedef void* (*__new_init_fptr)(const void* _ptr);
    typedef void (*__stk_init_fptr)(void* _mem, const void* _ptr);
    typedef bool (*__opt_eqal_fptr)(const void* _lhs, const void* _rhs);
    typedef const char* (*__idx_type_fptr)();
    typedef void* (*__def_meta_fptr)();

    template <typename T> void __new_free(void* _ptr) { delete reinterpret_cast<T*>(_ptr); }
    template <typename T> void __stk_free(void* _ptr) { reinterpret_cast<T*>(_ptr)->~T(); }
    template <typename T> void* __new_init(const void* _ptr) { return new T(*reinterpret_cast<const T*>(_ptr)); }
    template <typename T> void __stk_init(void* _mem, const void* _ptr) { new (_mem) T(*reinterpret_cast<const T*>(_ptr)); }
    template <typename T> bool __opt_eqal(const void* _lhs, const void* _rhs) { return *reinterpret_cast<const T*>(_lhs) == *reinterpret_cast<const T*>(_rhs); }
    template <typename T> void* __new_move(const void* _ptr) { return new T(std::move(*reinterpret_cast<const T*>(_ptr))); }
    template <typename T> void __stk_move(void* _mem, const void* _ptr) { new (_mem) T(std::move(*reinterpret_cast<const T*>(_ptr))); }
    template <typename T> const char* __idx_type() { return typeid(T).name(); }

    template <typename T> void* __def_meta() {
        static T def_val;
        return &def_val;
    }

    /**
     * \brief Function-pointer tables, one entry per type in Args..., indexed by id<T>()
     *
     * Declared here, defined out of line below: entry i is the helper instantiated with the
     * i-th type of the list, so an operation on the stored value is a table lookup.
     */
    template <typename... Args>
    struct __resource_ctrl {
        /// Delete a heap-allocated T
        static const __all_free_fptr new_free[];
        /// Heap copy-construct: return a new T copied from the pointed-to value
        static const __new_init_fptr new_init[];
        /// Heap move-construct: return a new T moved from the pointed-to value
        static const __new_init_fptr new_move[];
        /// Destroy an in-place T
        static const __all_free_fptr stk_free[];
        /// In-place copy-construct a T at _mem
        static const __stk_init_fptr stk_init[];
        /// In-place move-construct a T at _mem
        static const __stk_init_fptr stk_move[];
        /// Compare two T values
        static const __opt_eqal_fptr opt_eqal[];
        /// RTTI name of T
        static const __idx_type_fptr idx_type[];
        /// Address of a function-local static T, default-constructed on first use
        static const __def_meta_fptr def_meta[];
    };
    /// Body of the table above: entry i is __new_free instantiated with the i-th type
    template <typename... Args> const __all_free_fptr __resource_ctrl<Args...>::new_free[] = {__new_free<Args>...};
    /// Body of the table above: entry i is __new_init instantiated with the i-th type
    template <typename... Args> const __new_init_fptr __resource_ctrl<Args...>::new_init[] = {__new_init<Args>...};
    /// Body of the table above: entry i is __new_move instantiated with the i-th type
    template <typename... Args> const __new_init_fptr __resource_ctrl<Args...>::new_move[] = {__new_move<Args>...};
    /// Body of the table above: entry i is __stk_free instantiated with the i-th type
    template <typename... Args> const __all_free_fptr __resource_ctrl<Args...>::stk_free[] = {__stk_free<Args>...};
    /// Body of the table above: entry i is __stk_init instantiated with the i-th type
    template <typename... Args> const __stk_init_fptr __resource_ctrl<Args...>::stk_init[] = {__stk_init<Args>...};
    /// Body of the table above: entry i is __stk_move instantiated with the i-th type
    template <typename... Args> const __stk_init_fptr __resource_ctrl<Args...>::stk_move[] = {__stk_move<Args>...};
    /// Body of the table above: entry i is __opt_eqal instantiated with the i-th type
    template <typename... Args> const __opt_eqal_fptr __resource_ctrl<Args...>::opt_eqal[] = {__opt_eqal<Args>...};
    /// Body of the table above: entry i is __idx_type instantiated with the i-th type
    template <typename... Args> const __idx_type_fptr __resource_ctrl<Args...>::idx_type[] = {__idx_type<Args>...};
    /// Body of the table above: entry i is __def_meta instantiated with the i-th type
    template <typename... Args> const __def_meta_fptr __resource_ctrl<Args...>::def_meta[] = {__def_meta<Args>...};

    /// Type list and function tables for a variant over Args... only, with nothing boxed
    template <typename... Args>
    struct __variant_utils_base {
    public:
        /// Per-type function-pointer table, one entry per accepted type
        typedef __resource_ctrl<Args...> Ctrl;
        /// The accepted type list, in declaration order
        typedef typelist::type_list<Args...> Type;
    };

    /// Type list and function tables for a variant that also accepts std::vector<T> and
    /// std::list<T> alongside every type in Args...
    template <typename... Args>
    struct __variant_utils_full {
        /// Per-type function-pointer table, covering the boxed types as well
        typedef __resource_ctrl<Args..., std::vector<Args>..., std::list<Args>...> Ctrl;
        /// The accepted type list: Args..., then one std::vector<T> and one std::list<T> per T
        typedef typelist::type_list<Args..., std::vector<Args>..., std::list<Args>...> Type;
    };

    /**
     * \brief Type-erased union: one value of any type in the list, or nothing
     *
     * A trivially-copyable type of at most STACK_SIZE bytes lives inline in the object; anything
     * else is heap-allocated and owned. m_type is the whole state -- -1 means null, and the
     * payload is then neither read nor owned. Copying runs the stored type's copy constructor;
     * moving leaves the source null whatever its storage was.
     */
    template <template <typename...> typename Utils, typename... Args>
    class __variant_impl {
    public:
        /// Construct a null variant
        __variant_impl() : m_type(-1) {}
        /// Release the stored value
        ~__variant_impl() { destroy(); }

        /// Copy-construct through the stored type's copy constructor; a null source stays null
        __variant_impl(const __variant_impl& _value) : m_type(_value.m_type) {
            if (m_type == -1) return;
            if ((m_heap = _value.m_heap)) m_dptr = CTRL::new_init[m_type](_value.m_dptr);
            else m_dval = _value.m_dval;
        }
        /// Copy-assign; self-assignment is a no-op and a null source clears this one
        __variant_impl& operator=(const __variant_impl& _value) {
            if (this == &_value) return *this;
            else destroy();
            if (_value.m_type == -1) return *this;

            if (_value.m_heap) m_dptr = CTRL::new_init[_value.m_type](_value.m_dptr);
            else m_dval = _value.m_dval;
            m_heap = _value.m_heap;
            m_type = _value.m_type;
            return *this;
        }
        /// Move-construct; the source is left null whatever its storage was
        __variant_impl(__variant_impl&& _value) noexcept : m_type(_value.m_type) {
            if (m_type == -1) return;

            if ((m_heap = _value.m_heap)) {
                m_type = _value.m_type;
                _value.m_type = -1;
                m_dptr = _value.m_dptr;
                _value.m_dptr = nullptr;
            } else {
                m_type = _value.m_type;
                m_dval = _value.m_dval;
                _value.m_type = -1;
            }
        }
        /// Move-assign; self-assignment is a no-op and the source is left null
        __variant_impl& operator=(__variant_impl&& _value) noexcept {
            if (this == &_value) return *this;
            else destroy();
            if (_value.m_type == -1) return *this;

            if ((m_heap = _value.m_heap)) {
                m_type = _value.m_type;
                _value.m_type = -1;
                m_dptr = _value.m_dptr;
                _value.m_dptr = nullptr;
            } else {
                m_type = _value.m_type;
                m_dval = _value.m_dval;
                _value.m_type = -1;
            }
            return *this;
        }

        /**
         * \brief Construct holding a value of a supported type
         *
         * T must be in the type list, which a static_assert enforces at compile time. The value
         * is moved into a heap object when T is heap-mode, and copied into the inline slot
         * otherwise.
         */
        template <typename T, typename = std::enable_if_t<!std::is_base_of_v<__variant_impl, std::decay_t<T>> && !std::is_null_pointer_v<std::decay_t<T>>>>
        __variant_impl(T&& _value) noexcept(!heap_mode<std::decay_t<T>>()) {
            using ORG_T = std::decay_t<T>;
            static_assert(typelist::index_of<TYPE, ORG_T> != -1, "__variant_impl(T&&) unknown type!");
            if (heap_mode<ORG_T>()) m_dptr = new ORG_T(std::forward<T>(_value));
            else r_interpret<ORG_T>(m_dstk) = _value;
            m_heap = heap_mode<ORG_T>();
            m_type = typelist::index_of<TYPE, ORG_T>;
        }
        /// Assign a value of a supported type, releasing whatever was held first
        template <typename T, typename = std::enable_if_t<!std::is_base_of_v<__variant_impl, std::decay_t<T>> && !std::is_null_pointer_v<std::decay_t<T>>>>
        __variant_impl& operator=(T&& _value) noexcept(!heap_mode<std::decay_t<T>>()) {
            using ORG_T = std::decay_t<T>;
            static_assert(typelist::index_of<TYPE, ORG_T> != -1, "__variant_impl::=(T&&) unknown type!");
            destroy();
            if (heap_mode<ORG_T>()) m_dptr = new ORG_T(std::forward<T>(_value));
            else r_interpret<ORG_T>(m_dstk) = _value;
            m_heap = heap_mode<ORG_T>();
            m_type = typelist::index_of<TYPE, ORG_T>;
            return *this;
        }

        /// Hold a C string as std::string; char* is not itself a supported type
        __variant_impl(char* _value) : __variant_impl(std::string(_value)) {}
        /// Hold a C string as std::string; const char* is not itself a supported type
        __variant_impl(const char* _value) : __variant_impl(std::string(_value)) {}
        /// Assign a C string as std::string
        __variant_impl& operator=(char* _value) { return this->operator=(std::string(_value)); }
        /// Assign a C string as std::string
        __variant_impl& operator=(const char* _value) { return this->operator=(std::string(_value)); }

    public:

        /**
         * \brief Read the stored value when its type is exactly T, otherwise _default
         *
         * \param _default Fallback, by default the shared static def_val<T>()
         * \return a reference to the stored value, or to _default when T is not the stored type;
         *         the stored-value case is invalidated by any modification of this variant
         */
        template <typename T>
        inline const T& to(const T& _default = def_val<T>()) const {
            static_assert(typelist::index_of<TYPE, T> != -1, "__variant_impl::to(const T&) unknown type!");
            return typelist::index_of<TYPE, T> == m_type ? r_interpret<T>(m_heap ? m_dptr : m_dstk) : _default;
        }

        /**
         * \brief Mutable read of the stored value when its type is exactly T, otherwise _default
         *
         * \param _default Returned by reference when T is not the stored type, so a write through
         *                 the result lands in _default; there is no defaulted form on purpose,
         *                 the caller owns this object
         */
        template <typename T>
        inline T& to(T& _default) {
            static_assert(typelist::index_of<TYPE, T> != -1, "__variant_impl::to(T&) unknown type!");
            return typelist::index_of<TYPE, T> == m_type ? r_interpret<T>(m_heap ? m_dptr : m_dstk) : _default;
        }

        /// Like to<T>() but on a mismatch the variant is overwritten with a default-constructed T
        template <typename T>
        inline T& as() {
            static_assert(typelist::index_of<TYPE, T> != -1, "__variant_impl::as() unknown type!");
            if (typelist::index_of<TYPE, T> == m_type) return r_interpret<T>(m_heap ? m_dptr : m_dstk);
            else return operator=(T{}), r_interpret<T>(m_heap ? m_dptr : m_dstk);
        }

        /// Like as<T>() with an explicit fallback: on a mismatch _ifnot is stored, and returned
        template <typename T>
        inline T& as(T&& _ifnot) {
            using ORG_T = std::decay_t<T>;
            static_assert(typelist::index_of<TYPE, ORG_T> != -1, "__variant_impl::as(T&&) unknown type!");
            if (typelist::index_of<TYPE, ORG_T> == m_type) return r_interpret<ORG_T>(m_heap ? m_dptr : m_dstk);
            else return operator=(std::forward<T>(_ifnot)), r_interpret<ORG_T>(m_heap ? m_dptr : m_dstk);
        }

        /// True when the stored type is exactly T; false while null
        template <typename T>
        inline bool is() const noexcept { return m_type != -1 && m_type == typelist::index_of<TYPE, T>; }

    public:
        /// Equal when both hold the same type and equal values; two nulls are equal
        inline bool operator==(const __variant_impl& _value) const noexcept {
            if (m_type != _value.m_type) return false;
            if (m_type == -1) return true;
            return m_heap ? CTRL::opt_eqal[m_type](m_dptr, _value.m_dptr) : CTRL::opt_eqal[m_type](m_dstk, &_value.m_dval);
        }
        /// Equal to a C string only when a std::string with exactly that text is stored
        inline bool operator==(const char* _value) const noexcept {
            static_assert(typelist::index_of<TYPE, std::string> != -1, "__variant_impl::operator==(const char*) unknown type!");
            return is<std::string>() && (to<std::string>() == _value);
        }
        /**
         * \brief Compare against a value of the stored type
         *
         * The stored type's operator== does the comparison, so _value is reinterpreted as that
         * type: passing a value of any other type compares raw bytes, not values.
         *
         * \return false when null
         */
        template <typename T, typename = std::enable_if_t<!std::is_base_of_v<__variant_impl, T> && !std::is_same_v<char*, T>>>
        inline bool operator==(const T& _value) const noexcept {
            static_assert(typelist::index_of<TYPE, T> != -1, "__variant_impl::operator==(const T&) unknown type!");
            return m_type == -1 ? false : m_heap ? CTRL::opt_eqal[m_type](m_dptr, &_value)
                                                 : CTRL::opt_eqal[m_type](m_dstk, &_value);
        }
        /// Negation of operator==
        template <typename T>
        inline bool operator!=(const T& _value) const noexcept { return !this->operator==(_value); }

    public:

        /// Type index of the stored value, or -1 when null
        inline int type() const noexcept { return m_type; }
        /// True when nothing is stored; a moved-from variant is null
        inline bool null() const noexcept { return m_type == -1; }

        /// RTTI name of the stored type, or an empty string when null (compiler-dependent)
        inline const char* type_name() const noexcept { return m_type == -1 ? "" : CTRL::idx_type[m_type](); }
        /// Release the stored value and become null; a second call is a no-op
        inline void destroy() noexcept {
            if (m_type != -1 && m_heap) CTRL::new_free[m_type](m_dptr);
            m_type = -1;
        }

    public:

        /// Compile-time index of T in the type list, or -1 when T is not in it
        template <typename T>
        static constexpr int id() { return typelist::index_of<TYPE, T>; }

        /// Compile-time test whether T is one of the supported types
        template <typename T>
        static constexpr bool enable() { return typelist::index_of<TYPE, T> != -1; }

        /// True when T is stored on the heap: not trivially copyable, or larger than STACK_SIZE
        template <typename T>
        static constexpr bool heap_mode() { return !std::is_trivially_copyable_v<T> || sizeof(T) > STACK_SIZE; }

        /// A shared static default-constructed T, the default argument of the to<T>() accessors
        template <typename T>
        inline static const T& def_val() {
            static_assert(typelist::index_of<TYPE, T> != -1, "__variant_impl::def_val unknown type!");
            return r_interpret<const T>(CTRL::def_meta[typelist::index_of<TYPE, T>]());
        }

    protected:

        /**
         * \brief Convert the stored FROM to TO when the conversion is lossless, otherwise _def
         *
         * The caller must have checked type() == id<FROM>(); the storage is read as a FROM as-is.
         *
         * \param _def Fallback returned when is_lossless<FROM, TO> is false
         */
        template <typename FROM, typename TO>
        inline TO to_number_impl(const TO& _def) const noexcept {
            return alx::is_lossless<FROM, TO> ? (TO) r_interpret<FROM>(m_heap ? m_dptr : m_dstk) : _def;
        }

    protected:
        /// The type list and function tables this instantiation was built from
        typedef Utils<Args...> utils;
        /// The accepted type list
        using TYPE = typename utils::Type;
        /// The function-pointer tables, indexed the same way as TYPE
        using CTRL = typename utils::Ctrl;
        /// Inline capacity in bytes; a larger type goes to the heap, and must match sizeof(m_dstk)
        static constexpr uint_64 STACK_SIZE{8};

        union {
            /// Heap storage: the owned object, live when m_heap is true
            void* m_dptr;
            /// Inline storage for the value's bytes, live when m_heap is false
            uint_8 m_dstk[8];
            /// Whole-slot copy path for inline values: copies go through this member, never m_dstk
            uint_64 m_dval;
        };
        /// Type index of the stored value; -1 means null, and the payload is then not read at all
        int m_type;
        /// True when the value lives in m_dptr; must agree with heap_mode of the stored type
        bool m_heap;
    };

    /**
     * \brief Ordered string to Type map that owns its values
     *
     * Values are held as pointers but owned: erase, clear and destruction delete them, and a copy
     * duplicates them, so the map behaves as a value type. An insert never overwrites a key that
     * is already there. Iteration follows the key order of the underlying std::map.
     */
    template <template <typename, typename> typename MapType, typename Type>
    class __varmap_impl
        : protected MapType<std::string, Type*> {
    public:
        /// The underlying std::map<std::string, Type*> the storage is taken from
        using parent = MapType<std::string, Type*>;
        /// Mutable iterator of the parent map
        using parent_iterator = typename parent::iterator;
        /// Const iterator of the parent map
        using parent_const_iterator = typename parent::const_iterator;
        /// True while the map holds no entry, inherited from the parent map
        using parent::empty;
        /// Number of entries, inherited from the parent map
        using parent::size;
        /// Construct an empty map
        __varmap_impl() : parent() {}
        /// Deep copy: every value is duplicated into a fresh object
        __varmap_impl(const __varmap_impl& _value) : parent() {
            for (const_iterator it = _value.cbegin(); it != _value.cend(); it++) {
                insert(it.key(), it.value());
            }
        }
        /// Take over the contents of _value, which is left empty
        __varmap_impl(__varmap_impl&& _value) noexcept {
            this->parent::swap(_value);
        }
        /// Deep copy; self-assignment is a no-op, and the previous contents are deleted first
        __varmap_impl& operator=(const __varmap_impl& _value) {
            if (this == &_value) return *this;
            this->clear();
            for (const_iterator it = _value.cbegin(); it != _value.cend(); it++) {
                insert(it.key(), it.value());
            }
            return *this;
        }
        /// Take over the contents of _value, which is left empty; self-assignment is a no-op
        __varmap_impl& operator=(__varmap_impl&& _value) noexcept {
            if (this == &_value) return *this;
            this->parent::swap(_value);
            return *this;
        }
        /// Build from {key, value} pairs; a key repeated in the list keeps its first value
        __varmap_impl(std::initializer_list<std::pair<std::string, Type>> _list) {
            for (auto& it : _list) insert(it.first, it.second);
        }
        /// Delete every stored value
        ~__varmap_impl() {
            clear();
        }
        /**
         * \brief Mutable iterator over the key/value pairs, in key order
         *
         * Wraps the parent map iterator: key() and value() are the intended accessors, while
         * operator* and operator-> expose the underlying pair, whose second member is the raw
         * stored pointer. Inserting never invalidates an iterator; erasing an entry invalidates
         * the iterators pointing at that entry, and clear() invalidates all of them.
         */
        class iterator {
        private:
            parent_iterator m_it;

        public:
            /// Wrap a parent map iterator
            iterator(const parent_iterator& _it) : m_it(_it) {}
            /// Advance to the next entry, returning this iterator
            iterator& operator++() {
                ++m_it;
                return *this;
            }
            /// Advance, returning a copy of the previous position
            iterator operator++(int) { return iterator(m_it++); }
            /// Compare two positions
            inline bool operator!=(const iterator& _it) const { return m_it != _it.m_it; }
            /// Compare two positions
            inline bool operator==(const iterator& _it) const { return m_it == _it.m_it; }
            /// The current key, valid while its entry lives
            inline const std::string& key() const { return m_it->first; }
            /// Mutable reference to the current value, valid while its entry lives
            inline Type& value() { return r_interpret<Type>(m_it->second); }
            /// The underlying pair, whose second member is the raw stored pointer
            inline const typename parent_iterator::value_type& operator*() const { return *m_it; }
            /// The wrapped parent iterator, for access to the underlying map
            inline const parent_iterator& operator->() const { return m_it; }
        };
        /**
         * \brief Const iterator over the key/value pairs, in key order
         *
         * Wraps the parent map's const iterator; value() yields the value for reading only, and
         * the raw stored pointer is reachable through operator* and operator->. Inserting never
         * invalidates an iterator; erasing an entry invalidates the iterators pointing at that
         * entry, and clear() invalidates all of them.
         */
        class const_iterator {
        private:
            parent_const_iterator m_it;

        public:
            /// Wrap a parent map const iterator
            const_iterator(const parent_const_iterator& _it) : m_it(_it) {}
            /// Advance to the next entry, returning this iterator
            const_iterator& operator++() {
                ++m_it;
                return *this;
            }
            /// Advance, returning a copy of the previous position
            const_iterator operator++(int) { return const_iterator(m_it++); }
            /// Compare two positions
            inline bool operator!=(const const_iterator& _it) const { return m_it != _it.m_it; }
            /// Compare two positions
            inline bool operator==(const const_iterator& _it) const { return m_it == _it.m_it; }
            /// The current key, valid while its entry lives
            inline const std::string& key() const { return m_it->first; }
            /// The current value for reading, valid while its entry lives
            inline const Type& value() const { return r_interpret<const Type>(m_it->second); }
            /// The underlying pair, whose second member is the raw stored pointer
            inline const typename parent_const_iterator::value_type& operator*() const { return *m_it; }
            /// The wrapped parent const iterator, for access to the underlying map
            inline const parent_const_iterator& operator->() const { return m_it; }
        };

    public:

        /**
         * \brief Insert a new entry, handing the stored value over by move
         *
         * \param _value Moved from when the key is new, left untouched when it is already there
         * \return {iterator, true} when the key was new; {iterator, false} when it was already
         *         present, and then the stored value is left as it was
         */
        inline std::pair<iterator, bool> insert(const std::string& _key, Type&& _value) {
            parent_iterator it = parent::find(_key);
            if (it != parent::end()) return {it, false};
            else return parent::insert({_key, new Type(std::move(_value))});
        }
        /// Insert a new entry by copying the value; same {iterator, false} result on a known key
        inline std::pair<iterator, bool> insert(const std::string& _key, const Type& _value) {
            parent_iterator it = parent::find(_key);
            if (it != parent::end()) return {it, false};
            else return parent::insert({_key, new Type(_value)});
        }
        /// As insert(), moving the key too; a known key wins and the key is then not moved
        inline std::pair<iterator, bool> emplace(std::string&& _key, Type&& _value) {
            parent_iterator it = parent::find(_key);
            if (it != parent::end()) return {it, false};
            else return parent::emplace(std::move(_key), new Type(std::move(_value)));
        }

    public:
        /// Equal when both hold the same keys and each key maps to an equal value
        inline bool operator==(const __varmap_impl& _value) const {
            if (size() != _value.size()) return false;
            for (const_iterator it = cbegin(); it != cend(); it++) {
                const_iterator it2 = _value.find(it.key());
                if (it2 == _value.cend() || it.value() != it2.value()) return false;
            }
            return true;
        }
        /// Negation of operator==
        inline bool operator!=(const __varmap_impl& _value) const {
            return !(*this == _value);
        }

    public:
        /// Iterator to the first entry, in key order
        inline iterator begin() { return parent::begin(); }
        /// One past the last entry
        inline iterator end() { return parent::end(); }
        /// Const iterator to the first entry, in key order
        inline const_iterator begin() const { return parent::begin(); }
        /// Const one past the last entry
        inline const_iterator end() const { return parent::end(); }
        /// Const iterator to the first entry, in key order
        inline const_iterator cbegin() const { return parent::cbegin(); }
        /// Const one past the last entry
        inline const_iterator cend() const { return parent::cend(); }

    public:

        /// A shared static default-constructed Type, the fallback of value()
        inline static const Type& def_val() {
            static const Type s_val;
            return s_val;
        }
        /// Look up _key, yielding end() when it is absent
        inline iterator find(const std::string& _key) {
            return parent::find(_key);
        }
        /// Look up _key, yielding cend() when it is absent
        inline const_iterator find(const std::string& _key) const {
            return parent::find(_key);
        }

        /**
         * \brief Read the value stored under _key, without inserting anything
         *
         * \param _default Returned when the key is absent; defaults to the shared static def_val()
         * \return a reference to the stored value or to _default, valid until that entry is
         *         erased, the map is cleared, or the map is destroyed
         */
        inline const Type& value(const std::string& _key, const Type& _default = def_val()) const {
            parent_const_iterator it = parent::find(_key);
            return it == parent::cend() ? _default : r_interpret<Type>(it->second);
        }

        /**
         * \brief Access the value under _key, inserting a default-constructed one when absent
         *
         * For a varmap that new value is a null variant, not a zero.
         *
         * \return a reference to the stored value, valid until that entry is erased, the map is
         *         cleared, or the map is destroyed
         */
        inline Type& operator[](const std::string& _key) {
            parent_iterator it = parent::find(_key);
            if (it != parent::end()) return r_interpret<Type>(it->second);
            Type* value = new Type();
            parent::insert({_key, value});
            return *value;
        }
        /// True when _key is present
        inline bool contain(const std::string& _key) const {
            parent_const_iterator it = parent::find(_key);
            return it != parent::cend();
        }
        /// Delete the value stored under _key and drop the entry; a no-op when it is absent
        inline void erase(const std::string& _key) {
            parent_iterator it = parent::find(_key);
            if (it != parent::end()) {
                delete it->second;
                parent::erase(it);
            }
        }
        /// Delete every stored value and empty the map
        inline void clear() {
            for (const_iterator it = cbegin(); it != cend(); it++) {
                delete it->second;
            }
            parent::clear();
        }
    };

    /// Forward declaration of the ordered string-to-variant map defined below
    class varmap;
    /// Forward declaration of the std::vector<variant> container defined below
    class varvec;
    /// Forward declaration of the std::list<variant> container defined below
    class varlst;

    /**
     * \brief Type-erased value: one of the supported types, or nothing
     *
     * Supported: the ten numeric types (char, short, int, long long and their unsigned twins,
     * float and double), bool, bytes, std::string, anyptr, the containers varmap, varvec and
     * varlst, and std::vector<T> and std::list<T> for every type in that list. Numeric values
     * convert across numeric types through to<T>(), and only when the conversion is lossless.
     */
    class variant : public __variant_impl<
                        __variant_utils_full,
                        char, short, int, long long,
                        unsigned char, unsigned short, unsigned int, unsigned long long,
                        float, double, bool,
                        bytes, std::string,
                        varmap, varvec, varlst,
                        anyptr> {
    public:
        using __variant_impl::__variant_impl;
        using __variant_impl::operator=;
        using __variant_impl::def_val;
        /// A shared static null variant, the default _def of select()
        inline static const variant& def_val() {
            static const variant v;
            return v;
        }

    public:
        /// Identity: a variant read as a variant is itself
        template <typename T, typename = typename std::enable_if_t<std::is_same_v<T, variant>>>
        inline const variant& to() const { return *this; }

        /**
         * \brief Read the value as a numeric type T, converting when that loses nothing
         *
         * The conversion is taken only when is_lossless<stored type, T> holds, so a narrowing
         * request yields _default instead of a truncated value; so does a non-numeric stored
         * value.
         *
         * \param _default Fallback when the stored value cannot be read as a T
         */
        template <typename T, typename = typename std::enable_if_t<alx::is_number<T>>>
        inline T to(T _default = T()) const {
            if (id<T>() == type()) return __variant_impl::to<T>(_default);
            switch (type()) {
            case id<char>(): return to_number_impl<char, T>(_default);
            case id<short>(): return to_number_impl<short, T>(_default);
            case id<int>(): return to_number_impl<int, T>(_default);
            case id<long long>(): return to_number_impl<long long, T>(_default);
            case id<unsigned char>(): return to_number_impl<unsigned char, T>(_default);
            case id<unsigned short>(): return to_number_impl<unsigned short, T>(_default);
            case id<unsigned int>(): return to_number_impl<unsigned int, T>(_default);
            case id<unsigned long long>(): return to_number_impl<unsigned long long, T>(_default);
            case id<float>(): return to_number_impl<float, T>(_default);
            case id<double>(): return to_number_impl<double, T>(_default);
            default: return _default;
            }
        }
        /// Exact-type read for every supported type that is not numeric: _default on a mismatch
        template <typename T, typename = typename std::enable_if_t<!alx::is_number<T> && !std::is_same_v<T, variant>>>
        inline const T& to(const T& _default = def_val<T>()) const { return __variant_impl::to<T>(_default); }
        /// Identity, mutable: a variant read as a variant is itself
        template <typename T, typename = typename std::enable_if_t<std::is_same_v<T, variant>>>
        inline variant& to() { return *this; }
        /// Mutable read of the stored value, or _default to write into on a mismatch
        template <typename T, typename = typename std::enable_if_t<!std::is_same_v<T, variant>>>
        inline T& to(T& _default) { return __variant_impl::to<T>(_default); }

    public:
        /// True when a std::string is stored
        inline bool is_string() const noexcept { return is<std::string>(); }
        /// The stored std::string, or _def when some other type is stored
        inline const std::string& to_string(const std::string& _def = def_val<std::string>()) const { return to<std::string>(_def); }

    public:

        /// True when a signed integer (char, short, int or long long) is stored
        inline bool is_integer() const noexcept {
            return is<char>() || is<short>() || is<int>() || is<long long>();
        }

        /// The stored signed integer widened to long long, or 0 when no signed integer is stored
        inline long long to_integer() const {
            switch (type()) {
            case id<char>(): return static_cast<long long>(to<char>());
            case id<short>(): return static_cast<long long>(to<short>());
            case id<int>(): return static_cast<long long>(to<int>());
            case id<long long>(): return static_cast<long long>(to<long long>());
            default: return 0;
            }
        }

        /// True when an unsigned integer is stored
        inline bool is_uinteger() const noexcept {
            return is<unsigned char>() || is<unsigned short>() || is<unsigned int>() || is<unsigned long long>();
        }

        /// The stored unsigned integer widened to unsigned long long, or 0 when none is stored
        inline unsigned long long to_uinteger() const {
            switch (type()) {
            case id<unsigned char>(): return static_cast<unsigned long long>(to<unsigned char>());
            case id<unsigned short>(): return static_cast<unsigned long long>(to<unsigned short>());
            case id<unsigned int>(): return static_cast<unsigned long long>(to<unsigned int>());
            case id<unsigned long long>(): return static_cast<unsigned long long>(to<unsigned long long>());
            default: return 0;
            }
        }

        /// True when a numeric value is stored; a bool is not numeric here
        inline bool is_number() const noexcept {
            return is_integer() || is_uinteger() || is<float>() || is<double>();
        }

        /**
         * \brief The stored numeric value, as a double
         *
         * A value that is not numeric, bool included, yields NaN: 0 would pass unnoticed in the
         * arithmetic that follows, NaN does not.
         */
        inline double to_number() const {
            switch (type()) {
            case id<char>(): return static_cast<double>(to<char>());
            case id<short>(): return static_cast<double>(to<short>());
            case id<int>(): return static_cast<double>(to<int>());
            case id<long long>(): return static_cast<double>(to<long long>());
            case id<unsigned char>(): return static_cast<double>(to<unsigned char>());
            case id<unsigned short>(): return static_cast<double>(to<unsigned short>());
            case id<unsigned int>(): return static_cast<double>(to<unsigned int>());
            case id<unsigned long long>(): return static_cast<double>(to<unsigned long long>());
            case id<float>(): return static_cast<double>(to<float>());
            case id<double>(): return static_cast<double>(to<double>());

            default: return std::numeric_limits<double>::quiet_NaN();
            }
        }

    public:
        /// True when a varmap is stored
        inline bool is_map() const noexcept { return is<varmap>(); }
        /// The stored varmap, or _def when another type is stored
        inline const varmap& to_map(const varmap& _def = def_val<varmap>()) const;
        /// Mutable access requiring a stored varmap; on a mismatch _def comes back to write into
        inline varmap& to_map(varmap& _def);
        /// True when a varvec is stored
        inline bool is_vec() const noexcept { return is<varvec>(); }
        /// The stored varvec, or _def when another type is stored
        inline const varvec& to_vec(const varvec& _def = def_val<varvec>()) const;
        /// Mutable access requiring a stored varvec; on a mismatch _def comes back to write into
        inline varvec& to_vec(varvec& _def);
        /// True when a varlst is stored
        inline bool is_lst() const noexcept { return is<varlst>(); }
        /// The stored varlst, or _def when another type is stored
        inline const varlst& to_lst(const varlst& _def = def_val<varlst>()) const;
        /// Mutable access requiring a stored varlst; on a mismatch _def comes back to write into
        inline varlst& to_lst(varlst& _def);

    public:

        /**
         * \brief Walk a path of keys through nested containers
         *
         * A segment is resolved against the variant reached so far: a varmap is read as a string
         * key, a varvec or varlst as a decimal index. A segment that does not resolve -- absent
         * key, non-numeric key into a sequence, index past the end, or a value that is neither
         * map nor sequence -- ends the walk and yields _def.
         *
         * \param _keys Path segments in order; an empty path yields *this
         * \param _def  Returned when the path does not resolve
         * \return the variant the path lands on, or _def. A landed variant is a reference into
         *         this tree, invalidated by any modification of the containers on the way
         */
        inline const variant& select(const std::list<std::string>& _keys, const variant& _def = def_val()) const;
    };

    /// The std::map the varmap container is built on
    template <typename Key, typename Value>
    using varmap_base_map = std::map<Key, Value>;

    /**
     * \brief Ordered string to variant map that owns its values
     *
     * Keys are unique and iteration is in key order. insert() never overwrites a key that is
     * already there -- use operator[] to access or create one instead -- while erase, clear and
     * destruction delete the values.
     */
    class varmap : public __varmap_impl<varmap_base_map, variant> {
    public:
        using __varmap_impl::__varmap_impl;
        using __varmap_impl::operator=;
#ifdef _MSC_VER
        /// Default-construct an empty map; explicit because MSVC does not inherit this one
        varmap() = default;
#endif
        /// Build from a std::map, converting each mapped value to a variant
        template <typename T>
        inline varmap(const std::map<std::string, T>& _v) {
            for (const auto& v : _v) insert(v.first, v.second);
        }
    };

    /**
     * \brief std::vector<variant> with conversion constructors from std::vector<T> and std::list<T>
     *
     * Storage, indexing and copying are std::vector's, so a copy duplicates every element through
     * variant's copy constructor.
     */
    class varvec : public std::vector<variant> {
    public:
        using std::vector<variant>::vector;
        using std::vector<variant>::operator=;
#ifdef _MSC_VER
        /// Default-construct an empty vector; explicit because MSVC does not inherit this one
        varvec() = default;
#endif
        /// Build from a std::vector, pushing each element in as a variant
        template <typename T>
        varvec(const std::vector<T>& _v) {
            for (const T& v : _v) push_back(v);
        }
        /// Build from a std::list, pushing each element in as a variant
        template <typename T>
        varvec(const std::list<T>& _v) {
            for (const T& v : _v) push_back(v);
        }
    };

    /**
     * \brief std::list<variant> with conversion constructors from std::vector<T> and std::list<T>
     *
     * Storage and copying are std::list's, so a copy duplicates every element through variant's
     * copy constructor.
     */
    class varlst : public std::list<variant> {
    public:
        using std::list<variant>::list;
        using std::list<variant>::operator=;
#ifdef _MSC_VER
        /// Default-construct an empty list; explicit because MSVC does not inherit this one
        varlst() = default;
#endif
        /// Build from a std::vector, pushing each element in as a variant
        template <typename T>
        varlst(const std::vector<T>& _v) {
            for (const T& v : _v) push_back(v);
        }
        /// Build from a std::list, pushing each element in as a variant
        template <typename T>
        varlst(const std::list<T>& _v) {
            for (const T& v : _v) push_back(v);
        }
    };

    /// Out-of-line body of variant::to_map(const varmap&) declared above
    inline const varmap& variant::to_map(const varmap& _def) const { return to<varmap>(_def); }
    /// Out-of-line body of variant::to_map(varmap&) declared above
    inline varmap& variant::to_map(varmap& _def) { return to<varmap>(_def); }
    /// Out-of-line body of variant::to_vec(const varvec&) declared above
    inline const varvec& variant::to_vec(const varvec& _def) const { return to<varvec>(_def); }
    /// Out-of-line body of variant::to_vec(varvec&) declared above
    inline varvec& variant::to_vec(varvec& _def) { return to<varvec>(_def); }
    /// Out-of-line body of variant::to_lst(const varlst&) declared above
    inline const varlst& variant::to_lst(const varlst& _def) const { return to<varlst>(_def); }
    /// Out-of-line body of variant::to_lst(varlst&) declared above
    inline varlst& variant::to_lst(varlst& _def) { return to<varlst>(_def); }

    /// Out-of-line body of variant::select() declared above; the contract is on the declaration
    inline const variant& variant::select(const std::list<std::string>& _path, const variant& _def) const {
        if (_path.empty()) return *this;

        auto key_index = [](const std::string& _key, uint_64& _index) -> bool {
            if (_key.empty()) return false;
            uint_64 value{0};
            for (const char ch : _key) {
                if (!::isdigit((unsigned char) ch)) return false;
                if (value > (max_uint_64 - (uint_64) (ch - '0')) / 10) return false;
                value = value * 10 + (uint_64) (ch - '0');
            }
            return _index = value, true;
        };
        const variant* item = this;
        for (const std::string& key : _path) {
            if (item->is<varmap>()) {
                const varmap& vmap = item->to<varmap>();
                if (vmap.contain(key)) item = &vmap.value(key, _def);
                else return _def;
            } else if (item->is_vec() || item->is_lst()) {
                uint_64 index{0};
                if (!key_index(key, index)) return _def;
                if (item->is_vec()) {
                    const varvec& vvec = item->to_vec();
                    if (index < vvec.size()) item = &vvec[index];
                    else return _def;
                } else {
                    const varlst& vlst = item->to_lst();
                    if (index < vlst.size()) {
                        auto iter = vlst.begin();
                        while (index--) iter++;
                        item = &*iter;
                    } else return _def;
                }
            } else return _def;
        }
        return *item;
    }
}

#endif
