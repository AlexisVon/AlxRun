/*****************************************************************/ /**
 * \file   afactory.h
 * \brief  Factory pattern template, supports runtime/compile-time object creation
 *
 * \author alexis
 *
 * Copyright (c) 2026 AlexisVon
 * SPDX-License-Identifier: MIT
 *********************************************************************/

#ifndef _ALEXIS_FACTORY_H_
#define _ALEXIS_FACTORY_H_

#include <functional>
#include <string>
#include <unordered_map>

#ifdef _WIN32
#else
#    include <cxxabi.h>
#endif

namespace alx {
    /**
     * \brief Registry of creators for one product type, keyed by demangled name
     *
     * One table per instantiation: factory<TYPE> and factory<TYPE, Args...> share nothing. There
     * is no locking -- regist() belongs to static initialization, before main(), and create(),
     * enable() and type_map() only read the table afterwards, so they may be called from any
     * thread.
     */
    template <typename TYPE, typename... Args>
    class factory {
    public:
        /// What create() looks up: the factory's arguments in, a TYPE the caller owns out
        typedef std::function<TYPE*(Args...)> CREATE_FUNC;
        /**
         * \brief Add or replace the creator registered under a name
         *
         * _type_name is taken in the form typeid() hands it out: mangled on GCC/Clang, demangled
         * here, and "class X" / "struct X" on MSVC, where everything up to the first space is cut
         * off. A string that is not a mangled name -- a plain "circle", say -- is stored verbatim,
         * so hand-made names work.
         *
         * \param _type_name Name to register under; copied
         * \param _create_func Called by create() with the Args of this factory
         */
        static inline void regist(const char* _type_name, CREATE_FUNC _create_func) {
#ifdef _WIN32
            std::string type_name = std::string(_type_name);
            size_t index = type_name.find(' ');
            regist_map()[index == std::string::npos ? type_name : type_name.substr(index + 1)] = _create_func;
#else
            int status;
            char* name = abi::__cxa_demangle(_type_name, 0, 0, &status);
            regist_map()[status == 0 ? std::string(name) : std::string(_type_name)] = _create_func;
            free(name);
#endif
        }
        /**
         * \brief Build the named type
         *
         * \param _type_name Registered name, as product registered it or as regist() stored it
         * \param _args Handed to the creator unchanged; declare the factory's Args as references
         *              to keep them from being copied on the way in
         * \return A new instance the caller owns and deletes, or nullptr when the name is not
         *         registered -- an implementation the linker never pulled in is not registered
         */
        static inline TYPE* create(const std::string& _type_name, Args... _args) {
            auto iter = regist_map().find(_type_name);
            return iter == regist_map().cend() ? nullptr : iter->second(_args...);
        }
        /// True when create() would find a creator for _type_name
        static bool enable(const std::string& _type_name) {
            auto iter = regist_map().find(_type_name);
            return iter != regist_map().cend();
        }
        /**
         * \brief The whole name -> creator table
         *
         * Not a copy but the factory's own function-local static, so it stays valid for the rest
         * of the process. Read-only; enumerate it to learn the names create() answers to.
         */
        static inline const std::unordered_map<std::string, CREATE_FUNC>& type_map() { return regist_map(); }

    private:
        static std::unordered_map<std::string, CREATE_FUNC>& regist_map() {
            static std::unordered_map<std::string, CREATE_FUNC> regist_map_;
            return regist_map_;
        }
    };

    /**
     * \brief Implementation base that puts IMPL into the factory's table
     *
     * Deriving publicly is the whole registration: the static member below adds IMPL to
     * factory<TYPE, Args...> under the demangled name of IMPL during static initialization, so no
     * call to regist() is needed and the name is there for the first create(). Args... must fit
     * TYPE's constructor as well as IMPL's -- the creator builds IMPL with the same list.
     */
    template <typename IMPL, typename TYPE, typename... Args>
    class product : public TYPE {
    public:
        /**
         * \brief Build the base TYPE from the factory's arguments
         *
         * Touching _register is what has every translation unit that instantiates this
         * constructor emit the registration.
         *
         * \param _args Passed straight to TYPE
         */
        product(Args... _args) : TYPE(_args...) { (void) _register; }
        /// Virtual, so the TYPE* that create() returned is enough to delete a product
        virtual ~product() {}

    public:
        /// Registers IMPL with the factory the moment the static member below is constructed
        struct __register {
            __register() {
                factory<TYPE, Args...>::regist(typeid(IMPL).name(), [](Args... _args) -> TYPE* { return new IMPL(_args...); });
            }
        };
        /**
         * \brief The registration itself: constructing this member adds IMPL to the factory
         *
         * Built during static initialization, before main(). It is emitted only where this
         * product's constructor is instantiated, so an implementation that nothing ever constructs
         * and that defines no constructor of its own never reaches the table.
         */
        static const __register _register;
    };
    /// The definition every instantiation needs; the linker keeps one copy of it
    template <typename IMPL, typename TYPE, typename... Args>
    const typename product<IMPL, TYPE, Args...>::__register product<IMPL, TYPE, Args...>::_register;
}

#endif