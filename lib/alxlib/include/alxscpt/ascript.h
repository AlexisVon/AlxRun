/*****************************************************************/ /**
 * \file   ascript.h
 * \brief  Script engine — C-style embedded scripting language
 *
 * \author alexis
 *
 * Copyright (c) 2026 AlexisVon
 * SPDX-License-Identifier: MIT
 ******************************************************************************/
#ifndef _ALEXIS_SCRIPT_H_
#define _ALEXIS_SCRIPT_H_

#include "abase.h"
#include "autility.h"
#include "avariant.h"
#include <initializer_list>
#include <list>

namespace alx {
    namespace script {

        /**
         * \brief One compile-time diagnostic, as delivered through engine::on_cmpl
         *
         * The lexer and the parser both report through it, and the parser recovers per statement
         * so a single compile may deliver many of these; the compile only fails as a whole when
         * the resulting AST comes back empty.
         */
        struct compile_error {
            /// Position of the offending token
            struct {
                /// Source path, empty when the input had none
                std::string path;
                /// Line, 1-based
                uint_64 row{0};
                /// Column in characters, 1-based
                uint_64 col{0};
                /// Byte offset into the source; uint_64_npos when the position is unknown
                uint_64 index{uint_64_npos};
            } loc;
            /// Diagnostic text
            std::string msg;
        };

        /**
         * \brief How a run failed, as reported by engine::result::error
         *
         * The values of UnknownError up to DivZeroError are pinned because a host maps one of
         * them straight onto a process exit code (example/Scpt does exactly that); a new name
         * goes after them. The tail carries no values on purpose.
         */
        enum class error_type : int_32 {
            /// No classification: a failure the engine could not name any better
            UnknownError = -1,
            /// The run finished
            NoError = 0,
            /// set_interrupt(), or a hook that returned false. Owns id 1 on its own
            InterruptedError = 1,

            /// An allocation failed (bad_alloc / length_error)
            MemoryError = 2,
            /// A configured limit refused the operation: a vec/lst fill over max_vecfill
            ResourceError = 3,

            /// A C++ exception crossed a native boundary
            NativeError = 4,
            /// A compiled product did not pass the etype/vtype gate
            VersionError = 5,
            /// The module world failed: unresolved, unreadable or corrupt import
            ImportError = 6,
            /// A native call arrived outside any walk, i.e. from a load/unload entry point
            LinkError = 7,

            /// A value could not be converted to the type asked for
            ConvError = 8,
            /// Undefined name, or a name that is already taken
            NameError = 9,
            /// The value's type does not fit the operation
            TypeError = 10,
            /// The source does not parse (eval and indirect call throw this too)
            ParseError = 11,
            /// An index is out of range: sequence, slice, or native argument list
            IndexError = 12,
            /// A map key is absent
            KeyError = 13,
            /// Wrong argument count for a builtin
            ArgError = 14,
            /// Division or modulo by zero
            DivZeroError = 15,

            /// The generic runtime failure; what fwrap::raise() throws by default
            RuntimeError,
            /// Integer division overflow (INT64_MIN / -1)
            DivOverflowError,
            /// Arithmetic overflow, raised only with engine_config::overflow_check on
            OverflowError,
            /// Shift count negative, or 64 and beyond
            ShiftError,
            /// Entity navigation left the entity tree
            NavError,
            /// Frames exceeded engine_config::max_stack
            StackError,
        };

        /// Display name of _e; "UnknownError" for anything the table does not carry
        inline const char* error_type_name(error_type _e) {
            switch (_e) {

            case error_type::NoError: return "NoError";
            case error_type::InterruptedError: return "InterruptedError";
            case error_type::MemoryError: return "MemoryError";
            case error_type::ResourceError: return "ResourceError";
            case error_type::NativeError: return "NativeError";
            case error_type::VersionError: return "VersionError";
            case error_type::ImportError: return "ImportError";
            case error_type::LinkError: return "LinkError";
            case error_type::ConvError: return "ConvError";
            case error_type::NameError: return "NameError";
            case error_type::TypeError: return "TypeError";
            case error_type::ParseError: return "ParseError";
            case error_type::IndexError: return "IndexError";
            case error_type::KeyError: return "KeyError";
            case error_type::ArgError: return "ArgError";
            case error_type::DivZeroError: return "DivZeroError";
            case error_type::RuntimeError: return "RuntimeError";
            case error_type::DivOverflowError: return "DivOverflowError";
            case error_type::OverflowError: return "OverflowError";
            case error_type::ShiftError: return "ShiftError";
            case error_type::NavError: return "NavError";
            case error_type::StackError: return "StackError";
            default: return "UnknownError";
            }
        }

        /**
         * \brief What fired a hook callback; hook_info::info carries the payload
         *
         * Every event but exec is delivered regardless of the checkpoint interval. A callback
         * that returns false interrupts the run -- for import and link the statement simply does
         * not happen and the script gets no chance to catch that, it only stops.
         */
        enum class hook_event {
            /// Instruction checkpoint every hook_interval instructions; info = checkpoints so far
            exec,
            /// A module load was resolved and is about to happen; info = resolved path
            import,
            /// A dynamic library load was resolved and is about to happen; info = resolved path
            link,
            /// A script called trap(); info = {here, args?}
            trap,
            /// A statement boundary, for position tracking; info is empty, read wkdt_pos()
            debug,
        };

        /**
         * \brief Host IO channels, passed through to natives
         *
         * The engine never calls them: it only stores what set_pipe() was given and hands it out
         * through fwrap::config(), so what a channel means is entirely the host's convention --
         * the usual one is stdio when the pointer is null.
         */
        using pipe_out = void (*)(const std::string* _out, void* _ud);
        /// Host input channel; a host native calls it to obtain whatever the script asked for
        using pipe_in = std::string (*)(void* _ud);

        struct hook_info;
        /// Hook entry point; returning false interrupts the execution that fired the event
        using hook_fn = bool (*)(hook_info& _info);

        /// Host type-naming callback for a value the script layer cannot name; null/empty = the host does not know it either
        using type_ex = const char* (*)(const variant& _v, void* _ud);

        /**
         * \brief Everything an engine is configured with
         *
         * engine::create() copies it, engine::config() shows the live copy back, and the engine's
         * set_*() methods write into it. The runtime knobs (max_stack, max_vecfill,
         * overflow_check, search_paths) are read as the script runs, the parse-time ones
         * (parse_depth, debug_enable) apply to the next compile.
         */
        struct engine_config {

            /// Raise OverflowError instead of wrapping, on integer overflow
            bool overflow_check = false;
            /// Parse-time: insert position markers. Exec-time: record them and fire debug events
            bool debug_enable = false;
            /// Live frames allowed; 0 = unlimited
            size_t max_stack = 1024;
            /// Elements one vec/lst fill may produce; 0 = unlimited
            size_t max_vecfill = 0;
            /// Syntax nesting depth of one parse; 0 = unlimited
            size_t parse_depth = 1024;

            /// Directories an import/link target is looked up in
            std::list<std::string> search_paths;

            /// Hook to install; null = no events at all
            hook_fn hook_fn_ptr = nullptr;
            /// Cookie handed to the hook as hook_info::hkdt
            void* hook_ud = nullptr;
            /// Instructions between two hook_event::exec checkpoints; 0 = no exec events
            uint_64 hook_interval = 0;

            /// Host input channel; see engine::set_pipe()
            pipe_in pipe_in_ptr = nullptr;
            /// Cookie passed back to pipe_in_ptr
            void* pipe_in_ud = nullptr;
            /// Host output channel
            pipe_out pipe_out_ptr = nullptr;
            /// Cookie passed back to pipe_out_ptr
            void* pipe_out_ud = nullptr;

            /// Host type-naming callback; see engine::set_type_ex()
            type_ex type_ex_ptr = nullptr;
            /// Cookie passed back to type_ex_ptr
            void* type_ex_ud = nullptr;
        };

        /**
         * \brief One hook callback: what fired, its payload, and a read-only view of the run
         *
         * The engine builds one per event and passes it to hook_fn; the reference, the payload
         * and everything the wk*() accessors return are valid for the duration of that callback
         * and no longer. Their arguments are never checked: a stale handle, an out-of-range
         * index or an unknown key is a wild read, so hand back only what an accessor gave you.
         */
        struct ALXSCPT_API hook_info {
            /// Event being delivered
            hook_event type = hook_event::exec;
            /// Payload: exec = checkpoints fired, import/link = resolved path, trap = {here, args?}
            variant info;
            /// Reject reason: write it only when returning false; the last write is the one kept
            std::string* desc;

            /// Cookie from set_hook(), handed back on every event
            void* hkdt = nullptr;
            /// The engine's own state, which every wk*() accessor reads; opaque
            const void* wkdt = nullptr;
            /// Instructions to the next exec checkpoint; a callback may write it to re-arm
            uint_64 freq = 0;

            /// Configuration of the engine being observed
            const engine_config& wkconfig() const;

            /// Frames alive, 0 = at the top level of the script
            uint_64 wkfm_size() const;
            /// Name of frame _i: the def it runs, "eval" for an eval frame, "?" for a block or loop
            std::string wkfm_func(uint_64 _i) const;
            /// Entity the frame _i runs in
            const void* wkfm_eptr(uint_64 _i) const;
            /// Locals of frame _i (slots already released are not listed)
            std::vector<std::string> wkfm_data_keys(uint_64 _i);
            /// Value of local _key in frame _i; null when the frame has no such local
            const variant* wkfm_data_cptr(uint_64 _i, const char* _key) const;

            /// Entity behind _v, i.e. an imported module; null when _v is not one
            const void* wken_of(const variant* _v) const;
            /// The root entity, the scope the executed script itself runs in
            const void* wken_root() const;
            /// Entity-level names; frame locals are excluded
            std::vector<std::string> wken_data_keys(const void* _e);
            /// Value of entity-level name _key; null when it is not one
            const variant* wken_data_cptr(const void* _e, const char* _key) const;
            /// Path of the file the entity was loaded from; "::" for the root
            std::string wken_file(const void* _e) const;
            /// Alias the entity is bound to in its parent
            std::string wken_name(const void* _e) const;
            /// Entity that imported it; null for the root
            const void* wken_pptr(const void* _e) const;

            /// Link instance behind _v; null when _v is not one
            const void* wklk_of(const variant* _v) const;
            /// Every name stored in the link instance's data store
            std::vector<std::string> wklk_data_keys(const void* _l);
            /// Value of _key in the link store; null when it is not one
            const variant* wklk_data_cptr(const void* _l, const char* _key) const;
            /// Natives bound to area _area of the link; empty when the area is unknown
            std::vector<std::string> wklk_area_funs(const void* _l, const char* _area);
            /// Entity the link was declared in; null for a root-level link
            const void* wklk_pptr(const void* _l) const;

            /// True when _v holds a module entity
            bool wkis_ent(const variant* _v) const;
            /// True when _v holds a link instance
            bool wkis_link(const variant* _v) const;
            /// True when _v holds something callable: a script def or a native
            bool wkis_func(const variant* _v) const;
            /// True when _v holds a link area
            bool wkis_area(const variant* _v) const;

            /// Statement being executed: {row, col, ofst, file}; row 0 = the position is unknown
            varmap wkdt_pos() const;
            /// Statement chain, innermost first: {row, col, ofst, file, func}, the top level last
            std::list<varmap> wkdt_fpos() const;
        };

        /**
         * \brief The error the engine and natives throw, catchable from the script
         *
         * A native throws one with fwrap::raise(); the walker unwinds to the script's nearest try,
         * and an uncaught one ends exec() with result::error set to type and the message in
         * result::value. Only a walk catches it: thrown from a link's load entry point it
         * surfaces as that import's failure, thrown from release/unload it has nowhere to go.
         */
        struct script_exception {
            /// How the failure is classified
            error_type type;
            /// Message, always a plain string; a non-string is converted on the way in
            std::string info;

            /// _t becomes result::error, _v the message the traceback shows
            script_exception(error_type _t, const std::string& _v)
                : type(_t), info(_v) {}
        };

        /**
         * \brief The interface a native function gets: arguments in, result out
         *
         * One exists per native call and only for the duration of it, so the reference a native
         * receives -- and every pointer it takes from one -- is dead once the native returns.
         * The engine does not track any of them.
         *
         * A native runs on the thread that is executing the script. Registering the same handler
         * in several engines means it can be entered concurrently, and nothing in the engine
         * serializes that, so a handler on two engines has to be thread-safe by itself.
         */
        class fwrap {
        public:
            /// Empty: the engine builds the call object and destroys it with the call
            virtual ~fwrap() = default;

            /// Argument count of the call
            virtual size_t size() const = 0;
            /**
             * \brief Argument _i
             *
             * Valid during the call only. An argument written as a bare variable name aliases
             * the script's variable, so writing through the reference is seen by the script; any
             * other argument is a temporary. Throws IndexError when _i is past the last argument.
             */
            virtual variant& operator[](size_t _i) const = 0;

            /// Set the return value
            virtual void freturn(const variant& _v) = 0;
            /// Set the return value, moving _v in
            virtual void freturn(variant&& _v) = 0;
            /// Leave the call without a value; the script sees an empty variant
            void freturn() { freturn(variant()); }

            /// Slot of _key in the calling scope; null when the name or path resolves to nothing
            virtual variant* nload(const std::string& _key) = 0;

            /**
             * \brief Call a script function and get its value back
             *
             * \param _func Name or path resolved in the calling scope, or a callable from a value
             * \param _args Arguments, passed as plain values
             * \throws script_exception NameError when nothing callable is there; LinkError from a
             *         load/unload entry point, where there is no walk to call into
             */
            virtual variant call(const variant& _func, const varvec& _args) = 0;

            /**
             * \brief Throw a script exception
             *
             * \param _info Message; a non-string is converted on the way in
             * \param _what Classification the script and result::error will see
             */
            virtual void raise(const variant& _info,
                               error_type _what = error_type::RuntimeError) = 0;

            /**
             * \brief Register a native function under _name
             *
             * With an empty _area it goes into the call's data scope, which for a link entry point
             * is the instance's own store; a non-empty _area creates the area object under that
             * name or extends the one already there. Throws NameError when _name is taken.
             *
             * \param _func Handler, of the same type as an extension function
             * \param _area Area to bind into, or empty for the plain scope
             */
            virtual void bind(const std::string& _name,
                              void (*_func)(fwrap&),
                              const std::string& _area = std::string()) = 0;

            /**
             * \brief Object bound to the area the call went through
             *
             * Only an area-bound native has one: a plain extension function called as $name()
             * carries no area, and the engine does not check before dereferencing.
             */
            virtual anyptr* object() = 0;

            /// Value of _name in the call's data scope, or null when there is none
            virtual variant* load(const std::string& _name) = 0;
            /// Set _name in the call's data scope; throws NameError when the name is taken
            virtual variant* store(const std::string& _name, const variant& _val) = 0;
            /// Same, moving _val in
            virtual variant* store(const std::string& _name, variant&& _val) = 0;
            /// Drop _name; false when it was not there
            virtual bool remove(const std::string& _name) = 0;

            /// The area's object as T*, or null when the area holds another type
            template <typename T>
            T* unwrap() {
                anyptr* obj = object();
                return obj ? anyptr_ex<T>::as(*obj) : nullptr;
            }

            /**
             * \brief Bind _obj as a new area and register its methods in one step
             *
             * The object goes into the scope under _area and its handle owns it from then on, so
             * the caller must not delete it afterwards; each method is bound as a native of that
             * area. Throws NameError when _area is already taken -- remove() it first to replace
             * an instance.
             *
             * \param _obj Object to hand over
             * \param _area Name the script reaches it under
             * \param _methods Pair of method name and handler
             */
            template <typename T>
            void wrap(T* _obj, const std::string& _area,
                      std::initializer_list<std::pair<const std::string, void (*)(fwrap&)>> _methods) {
                store(_area, variant(anyptr_ex<T>::make(_obj)));
                for (const auto& m : _methods)
                    bind(m.first, m.second, _area);
            }

            /// Configuration of the engine, with the pipe pointers in it
            virtual const engine_config& config() const = 0;
        };

        /// Signature of an extension function and of a native bound with fwrap::bind()
        using native_func = void (*)(fwrap&);

        /// Entry point a link library exports for its library-level load, run once per engine
        static constexpr const char* LINK_LOAD = "alexis_script_load";
        /// Entry point run for each link instance the engine creates
        static constexpr const char* LINK_CREATE = "alexis_script_create";
        /// Entry point run when a link instance is destroyed
        static constexpr const char* LINK_RELEASE = "alexis_script_release";
        /// Library-level counterpart of LINK_LOAD, run when the engine drops its last link
        static constexpr const char* LINK_UNLOAD = "alexis_script_unload";

        /// Signature of the LINK_LOAD entry point
        using script_load = void (*)(fwrap&);
        /// Signature of the LINK_CREATE entry point
        using script_create = void (*)(fwrap&);
        /// Signature of the LINK_RELEASE entry point
        using script_release = void (*)(fwrap&);
        /// Signature of the LINK_UNLOAD entry point
        using script_unload = void (*)(fwrap&);

        /**
         * \brief The script engine: a compilation environment, an interpreter, and variables
         *
         * create() hands back an engine the caller owns and deletes. An engine is single-threaded
         * and not reentrant: exec(), call(), compile(), load() and the set_*() methods must never
         * be entered from two threads at once, while running() and set_interrupt() are meant to be
         * called from another thread. A native registered here runs on whichever thread executes
         * the script, so one handler registered in several engines must be thread-safe itself.
         *
         * What a run leaves behind stays: root variables, defs and loaded modules survive into the
         * next exec(), which is what lets a REPL be an exec() in a loop. reset() takes that away.
         */
        class ALXSCPT_API engine {
        public:
            /// Delete the engine, dropping the root store and every module still loaded
            virtual ~engine() = default;

            /**
             * \brief Create an engine
             *
             * \param _cfg Configuration to copy; config() shows the live copy back
             */
            static engine* create(const engine_config& _cfg = engine_config());

            /**
             * \brief Take a compiled product apart into readable pieces
             *
             * \param _axp Product of compile()
             * \param _out Receives "ast" (text dump) and "info" (metadata), plus "modules"
             *             (key -> dump) and "hint" (the host payload in clear) when present
             * \return false when _axp is not a well-formed product, or its payload does not undo
             */
            static bool unpack(const bytes_view& _axp, varmap& _out);
            /**
             * \brief Dump the AST of a compiled product, or of source text
             *
             * \return The dump, or a line starting with "Error:" when _data is neither a product
             *         nor source that stands on its own
             */
            static std::string prtast(const bytes_view& _data);
            /// Reformat source: a statement per line, indented by brace; a lexical pass, no AST
            static bytes prtfmt(const bytes_view& _data);

            /**
             * \brief Metadata of a compiled product, one ";; key: value" line per field
             *
             * \return The lines, or "Error: not a compiled binary" when _data is not a product
             */
            static std::string prtinf(const bytes_view& _data);

            /**
             * \brief Drop everything the engine has accumulated
             *
             * Root variables, module instances and whatever a failed run left behind go; the
             * configuration stays. Every module still loaded is released, so a link library's
             * unload entry point runs.
             */
            virtual void reset() = 0;

            /**
             * \brief Register an extension function $name
             *
             * The $ prefix keeps host names out of the script's own namespace. The parser checks
             * the name while compiling and the walker looks the handler up again per call, so a
             * compiled product that used $name needs it registered in the engine that runs it.
             *
             * \return false when _name is a language keyword, or a static definition already holds
             *         it; true otherwise, the handler replacing whatever was there before
             */
            virtual bool set_extend(const std::string& _name, native_func _handler) = 0;
            /// Unregister $name; no-op when it was never registered
            virtual void del_extend(const std::string& _name) = 0;
            /// Handler bound to $name, or null when there is none
            virtual native_func get_extend(const std::string& _name) const = 0;
            /// True when $name is a registered extension
            virtual bool fid_extend(const std::string& _name) const = 0;

            /**
             * \brief Register a host constant $name
             *
             * Shares the $ namespace with the extension functions, so the two cannot collide. The
             * value is folded into the AST while compiling, which makes a product independent of
             * the definition it was built with -- and a misspelling a compile error rather than a
             * silently empty read.
             *
             * \return false when _name is a language keyword, or an extension function already
             *         holds it
             */
            virtual bool set_define(const std::string& _name, const variant& _value) = 0;
            /// Unregister $name; no-op when it was never registered
            virtual void del_define(const std::string& _name) = 0;
            /// Value of $name, or an empty variant when there is none
            virtual variant get_define(const std::string& _name) const = 0;
            /// True when $name is a registered definition
            virtual bool fid_define(const std::string& _name) const = 0;

            /**
             * \brief Set the compatibility tag stamped into every compiled product
             *
             * exec() refuses a product whose tag differs from the engine's, so the two ends of a
             * tag have to agree exactly; a product with no tag is refused just the same once the
             * engine has one. Change or drop an extension and the tag has to change with it.
             *
             * \param _type Tag to stamp; empty together with vtype 0 leaves the gate off
             */
            virtual void set_etype(const std::string& _type) = 0;
            /// The tag as stamped; empty on an engine that never set one
            virtual const std::string& etype() const = 0;
            /**
             * \brief Set the numeric counter stamped into every compiled product
             *
             * Checked one way on the way back in: a product whose counter is greater than the
             * engine's is refused as too new, an older one is accepted, so 0 accepts nothing that
             * carries a counter. Bump it when an extension is added; use the tag when one changes
             * meaning.
             *
             * \param _ver Counter to stamp and to accept up to
             */
            virtual void set_vtype(uint_64 _ver) = 0;
            /// The counter as stamped; 0 on an engine that never set one
            virtual uint_64 vtype() const = 0;

            /// The engine's live configuration, valid for as long as the engine is
            virtual const engine_config& config() const = 0;
            /// Frames allowed before StackError; 0 = unlimited
            virtual void set_max_stack(size_t _n) = 0;
            /// Syntax nesting one parse may reach; 0 = unlimited
            virtual void set_parse_depth(size_t _n) = 0;
            /// Raise OverflowError on integer overflow, or wrap around silently (the default)
            virtual void set_overflow_check(bool _on) = 0;
            /// Elements a single vec/lst fill may produce; 0 = unlimited
            virtual void set_max_vecfill(size_t _n) = 0;
            /// Directories import and link targets are looked up in, replacing the previous list
            virtual void set_search_paths(const std::list<std::string>& _paths) = 0;
            /**
             * \brief Install the hook that observes execution and gates module loading
             *
             * A null _fn removes it. import, link, trap and debug events arrive regardless of the
             * interval; _interval only decides how many instructions pass between two
             * hook_event::exec checkpoints, and 0 turns those off while set_interrupt() keeps
             * working. A callback may re-arm the interval through hook_info::freq.
             *
             * \param _ud Cookie handed back as hook_info::hkdt on every event
             * \param _interval Instructions between exec checkpoints; 0 = none
             */
            virtual void set_hook(hook_fn _fn, void* _ud, uint_64 _interval) = 0;
            /**
             * \brief Install the host's IO channels
             *
             * The engine only stores them and hands them out through fwrap::config(); nothing in
             * the library calls them. Null, the default, is for the host to interpret.
             *
             * \param _in_ud Cookie passed back to _in; _out_ud the one passed to _out
             */
            virtual void set_pipe(pipe_in _in, pipe_out _out, void* _in_ud = nullptr, void* _out_ud = nullptr) = 0;
            /**
             * \brief Install the host's type-naming callback
             *
             * type() asks it about a value the script layer has no name of its own for -- an
             * object a host handed over, or a variant type the script layer does not model --
             * and uses the name it returns as-is; null or an empty answer means the host does
             * not know it either, and "unknown" stands. The callback runs on whichever thread
             * executes the script; the pointer it returns is read once, right after the call,
             * never kept -- its storage must stay valid past the return. A C++ exception it
             * throws is reported like a native's: a script try can catch it as NativeError,
             * and without one the run comes back as NativeError.
             *
             * \param _fn Callback; null removes it
             * \param _ud Cookie handed back to _fn
             */
            virtual void set_type_ex(type_ex _fn, void* _ud = nullptr) = 0;

            /// True while an exec() is running; false everywhere else, call() included
            virtual bool running() const = 0;
            /**
             * \brief Ask a running exec() to stop
             *
             * Safe to call from any thread. The request is honoured at the next instruction
             * boundary and the run comes back as InterruptedError; a request made while nothing
             * is running is discarded, because exec() clears the flag as it starts.
             */
            virtual void set_interrupt() = 0;

            /**
             * \brief Track where a running script is, statement by statement
             *
             * Takes effect on the next parse and on every exec. With it on, the parser inserts a
             * marker per statement and the walker records positions and fires hook_event::debug;
             * with it off neither happens, so an already compiled product stays without positions
             * and reports row 0.
             */
            virtual void set_debug_enable(bool _on) = 0;

        public:
            /// What exec() and call() report: the value, how it went, and how long it took
            struct result {
                /// Last statement's value, or the error message when error is not NoError
                variant value;
                /// Wall time of the run, in microseconds
                int_64 elapsed_us = 0;
                /// NoError when the run finished normally
                error_type error = error_type::NoError;
            };

            /**
             * \brief Root variable by name
             *
             * The root scope is the one a script's top level runs in, so this is how a host seeds
             * a global before exec() and reads it back afterwards; it works outside a run too.
             *
             * \param _name Variable name
             * \param _auto_create Create the variable, empty, when it does not exist yet
             * \return The slot, or null when _name is absent and _auto_create is false; the
             *         pointer is invalidated by anything that adds another root variable
             */
            virtual variant* load(const std::string& _name, bool _auto_create = false) = 0;

            /**
             * \brief Call a def left in the root scope
             *
             * The name is looked up in the root store alone, so the function has to come from an
             * exec() that already ran. running() stays false while it runs, and an interrupt
             * request only cuts the body short instead of being reported as InterruptedError.
             *
             * \param _name def name
             * \param _args Arguments, passed as values
             * \return error NameError when _name is not a def of the root scope; the function's
             *         value otherwise
             */
            virtual result call(const std::string& _name, const varvec& _args) = 0;

            /**
             * \brief Run source text, or a product of compile()
             *
             * A product is recognised by its container, so the one entry point runs either. Root
             * variables and loaded modules are left behind for the next call. The script's own
             * failures are reported rather than thrown: error carries the classification, value
             * the message, and the message also goes to on_cerr with a traceback.
             *
             * \param _data Source text, or a compiled product
             * \param _home_dir Base relative imports resolve against, and the path the root module
             *                  is attributed to
             * \return error ParseError when source does not parse (value stays empty), VersionError
             *         when a product fails the etype/vtype gate, ImportError when its payload is
             *         corrupt, InterruptedError when a hook or set_interrupt() stopped the run;
             *         elapsed_us stays 0 for the failures that precede the walk
             */
            virtual result exec(const bytes_view& _data,
                                const std::string& _home_dir) = 0;

            /**
             * \brief Run a script file
             *
             * The file's directory becomes the home relative imports resolve against, and
             * compile-time diagnostics name the file.
             *
             * \return error RuntimeError with a "cannot read file" message when the file cannot be
             *         read; otherwise the same as the other overload
             */
            result exec(const std::string& _file_path);

            /**
             * \brief Compile source into a self-contained product
             *
             * \param _data Source text
             * \param _home_dir Base relative imports resolve against, and the path the root module
             *                  is attributed to
             * \param _cmps Compress the payload
             * \param _embed Resolve and inline every import; link and env then become compile
             *               errors, a relative import resolves against the importing module's own
             *               directory, and an import that resolves to nothing fails the compile
             * \param _hint Host payload stored as given, compressed when _cmps is set, and never
             *              interpreted by the engine; an empty one leaves the key out
             * \return The product, or empty bytes when the source does not parse (the reason goes
             *         to on_cmpl)
             */
            virtual bytes compile(const bytes_view& _data,
                                  const std::string& _home_dir, bool _cmps = false,
                                  bool _embed = false,
                                  const bytes_view& _hint = bytes_view()) const = 0;

            /**
             * \brief Compile a script file
             *
             * \param _path Script file; its directory becomes the home
             * \param _hint Path of a file whose content becomes the payload, where the other
             *              overload takes the payload itself
             * \return Empty bytes when the file cannot be read
             */
            bytes compile(const std::string& _path, bool _cmps = false, bool _embed = false,
                          const std::string& _hint = std::string()) const;

        public:
            /// Engine diagnostics pushed on the executing thread: errors with their traceback,
            /// plus internal complaints it cannot report as a value
            signal<const std::string&> on_cerr;
            /// Every compile-time diagnostic, one per error; a single compile may deliver several
            signal<const compile_error&> on_cmpl;

            /**
             * \brief The event sink every engine in the process shares
             *
             * Module load and unload and link failures are reported here as (thread id, message)
             * instead of through on_cerr. It belongs to the process-wide resource pool, which the
             * first engine creates and the last one destroys, so a reference kept past that point
             * dangles.
             */
            virtual signal<uint_64, const std::string&>& get_csys() = 0;
        };

    }
}

#endif
