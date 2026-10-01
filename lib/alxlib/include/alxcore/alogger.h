/*****************************************************************/ /**
 * \file   alogger.h
 * \brief  Logging system (supports multiple log levels)
 *
 * \author alexis
 *
 * Copyright (c) 2026 AlexisVon
 * SPDX-License-Identifier: MIT
 *********************************************************************/

#ifndef _ALEXIS_LOGGER_H_
#define _ALEXIS_LOGGER_H_

#include "ajson.h"
#include "autility.h"

#include <atomic>

namespace alx {
    /**
     * \brief Severity of a record, and the threshold an appender filters on
     *
     * The order is the filter order: an appender takes a record when the record's level is not
     * below its own. info is 0, the lowest, and the level an appender falls back on when its config
     * names none, so every appender takes it; an appender set to debug already drops it.
     */
    enum class log_level : uint_8 { info = 0,
                                    /// Detail useful while tracking a problem down
                                    debug = 1,
                                    /// Something unexpected that the code decided to carry on from
                                    warn = 2,
                                    /// An operation failed
                                    error = 3,
                                    /// The failure the process does not survive
                                    crash = 4,
                                      };
    /// Settings of one logger, parsed from its configuration; opaque here, owned by that logger
    class log_property;
    /// One output sink of a logger, built from a configuration entry; opaque here, owned by it
    class log_appender;

    /**
     * \brief One logger: the global options plus an appender per entry of its configuration
     *
     * log() formats a record and writes it to every appender whose level the record passes. The
     * appenders and the parsed options are owned, and the destructor deletes them, so a logger must
     * not be copied. A record reaches its sink on the calling thread, except through a file
     * appender built with is_async, which queues it -- blocking the caller while the queue is full
     * -- to a worker of its own; the destructor drains that queue and joins the worker.
     */
    class ALXCORE_API logger {
    public:

        /**
         * \brief Build a logger from a JSON configuration
         *
         * The top-level object carries the global options: prt_time, prt_thrd and prt_level prefix
         * the record with a timestamp, the low 16 bits of the thread id and the level name, and
         * prt_head puts a newline in front of it -- each a bool, false when absent. Every other
         * entry whose value is an object is one appender: its "type" picks the implementation and
         * the rest of the object is handed to it. Registered are cout (std::cout, reads is_flush:
         * flush the sink after every record), file (a local file: file_path, is_append -- append
         * instead of truncating -- is_flush, is_async and queue_limit, the queue depth, 1024 for 0
         * or absent) and, under Windows, win32 (OutputDebugString). Each also reads "level", the
         * lowest record it takes; a name that is absent or unrecognized falls back to info.
         *
         * Keys outside these, entries whose type nothing registered, and a file_path that cannot be
         * opened are ignored without a word -- the last one turns every record into a no-op.
         *
         * \param _config Configuration; nothing of it is referenced after the call
         */
        logger(const json_object& _config);
        /// As the json_object overload, from the JSON text of a configuration; text that does not
        /// parse yields a logger with no appenders, which silently discards every record
        logger(const std::string& _config) : logger(json_doc::from_json(_config)) {}
        /// Delete the appenders and the parsed settings
        ~logger();

    public:
        /**
         * \brief The appender implementations this build has registered
         *
         * \return The registration keys, the demangled class names -- log_appender_cout,
         *         log_appender_file, and log_appender_win32 under Windows -- rather than the type
         *         values a configuration writes
         */
        static std::vector<std::string> list();
        /**
         * \brief Format one record and write it to every appender that takes it
         *
         * The record is the prefix [timestamp][level][thread id] -- of which only the fields whose
         * prt_* option is on appear, in that order -- then _mesg and a newline; prt_head adds a
         * newline in front of _mesg, so the message lands on a line of its own. The level name
         * printed is padded to five characters, and a level outside the enum reads "nolvl". An
         * appender takes the record when _level is not below its own.
         *
         * May be called from any thread. Nothing is locked around the appender list, so records
         * written concurrently can interleave; the exception is the queue of an appender built with
         * is_async, which serializes them and may block the caller while it is full.
         *
         * \param _mesg Text of the record; it is copied and a newline is appended
         * \param _level Severity the appenders filter on
         */
        void log(const std::string& _mesg, log_level _level);

    private:
        const log_property* m_property{nullptr};
        std::vector<log_appender*> m_appenders;
    };

    /**
     * \brief The process-wide sink the log_wapper temporaries hand their records to
     *
     * Reachable through instance(), which is thread-safe and answers the same object every time. A
     * record goes to the installed handle when there is one, and otherwise to a default logger the
     * first call builds: a logger on the default configuration, the timestamp and thread id
     * prefixes over a flushing cout appender. set_log_handle() is the only way to redirect that
     * path.
     */
    class global_logger
        : public single<global_logger> {
    public:
        /// Delete the default logger this object built; single<> never destroys its instance, so a
        /// program that goes through instance() never runs this
        ~global_logger() { delete __deflog__.load(); }

    public:
        /// Signature of a sink that replaces the default logger: the record text and its level
        typedef void (*log_handle)(const std::string&, log_level);
        /**
         * \brief Submit a record to the installed handle, or to the default logger
         *
         * A handle, when one is installed, takes the record in place of the default logger, and the
         * text it receives is the caller's own -- the fields the builder collected, with none of
         * the logger prefixes and no trailing newline. Safe to call from any thread: the handle is
         * swapped atomically, and a call already inside the previous one runs to its end there. The
         * default logger is built by the first call that needs it; a race leaves exactly one alive.
         *
         * \param _mesg Record text, as the caller built it
         * \param _level Level of the record
         */
        inline void log(const std::string& _mesg, log_level _level) {
            if (log_handle handle = __handle__.load(std::memory_order_acquire)) return handle(_mesg, _level);
            logger* deflog = __deflog__.load(std::memory_order_acquire);
            if (nullptr == deflog) deflog = make_default();
            deflog->log(_mesg, _level);
        }
        /**
         * \brief Install a sink for the global path, or clear the one installed
         *
         * Takes effect for the next call to log(); a call already inside the previous handle is not
         * disturbed. An installed handle stays until it is replaced, so a caller that only wants to
         * observe for a while puts the returned handle back when it is done.
         *
         * \param _handle Sink to install; nullptr restores the default logger
         * \return The handle that was installed before, or nullptr when there was none
         */
        inline log_handle set_log_handle(log_handle _handle) {
            return __handle__.exchange(_handle, std::memory_order_acq_rel);
        }

    private:

        inline logger* make_default() {
            logger* fresh = new logger(__defcfg__);
            logger* expected = nullptr;
            if (!__deflog__.compare_exchange_strong(expected, fresh, std::memory_order_acq_rel)) delete fresh;
            else expected = fresh;
            return expected;
        }

    private:
        static constexpr char __defcfg__[] = R"({"prt_time":true,"prt_thrd":true,"prt_level":false,"stdout":{"type":"cout","level":"info","is_flush":true}})";
        std::atomic<logger*> __deflog__{nullptr};
        std::atomic<log_handle> __handle__{nullptr};
    };

    /**
     * \brief Record builder: fields are appended to it, and the record is submitted when it dies
     *
     * Meant to be used as a temporary -- log_info() << "started" << 42 -- so the record reaches the
     * global logger at the end of the full expression; a named one submits when its scope ends, and
     * every copy submits again. Each operator<< separates its value from what comes before it with
     * a space; type() is the exception, appending [name] as it stands.
     *
     * \tparam LEVEL Level the record is submitted at
     */
    template <log_level LEVEL>
    class log_wapper {
    public:
        /// Build an empty record, with room reserved for the first fields
        inline log_wapper() { m_buf.reserve(128); }
        /// Submit what has been built to the global logger, at LEVEL
        inline ~log_wapper() { global_logger::instance()->log(m_buf, LEVEL); }

    public:
        /// Append [name]: a tag for the record, with no space in front of the bracket
        inline log_wapper& type(const std::string& _t) {
            return m_buf.push_back('['), m_buf.append(_t), m_buf.push_back(']'), *this;
        }

    private:
        template <typename T>
        inline log_wapper& append(T&& _v) {
            return m_buf.push_back(' '), m_buf.append(std::forward<T>(_v)), *this;
        }

    public:
        /// Append _v behind a space
        inline log_wapper& operator<<(const std::string& _v) { return append(_v); }
        /// As the std::string overload, converted from the wide form through the current locale
        inline log_wapper& operator<<(const std::wstring& _v) { return append(strutil::from_wstring(_v)); }
        /// Append " true" or " false"
        inline log_wapper& operator<<(bool _v) { return m_buf.append(_v ? " true" : " false"), *this; }
        /// As the const char* overload, so that a char* is taken as text and not by the fallback
        inline log_wapper& operator<<(char* _v) { return append(_v); }
        /// As the const wchar_t* overload, so that a wchar_t* is taken as text and not by the fallback
        inline log_wapper& operator<<(wchar_t* _v) { return append(strutil::from_wstring(_v)); }
        /// As the std::string overload, for a C string
        inline log_wapper& operator<<(const char* _v) { return append(_v); }
        /// As the std::wstring overload, for a wide C string
        inline log_wapper& operator<<(const wchar_t* _v) { return append(strutil::from_wstring(_v)); }
        /// Append the JSON text of _v, indented over several lines
        inline log_wapper& operator<<(const json_object& _v) { return append(json_doc::to_json(_v)); }
        /// Fallback for arithmetic types, formatted by std::to_string
        template <typename T> inline log_wapper& operator<<(const T& _v) { return operator<<(std::to_string(_v)); }

    private:
        std::string m_buf;
    };

    /**
     * \brief Log the two ends of a scope, at the level the writer LOG names
     *
     * One tracker per scope, as the FUNC_TRACKER macros do: the constructor logs [IN_] and the
     * destructor logs [OUT], each followed by the name the tracker was built with.
     *
     * \tparam LOG Writer class to instantiate, a log_wapper; the level it carries is the level the
     *             two records are submitted at
     */
    template <typename LOG>
    class log_tracker : public noncopyable {
    public:
        /// Log the entry of the scope and remember _func_name
        inline log_tracker(const char* _func_name) : func_name_(_func_name) { LOG().type("IN_") << func_name_; }
        /// Log the exit of the scope, under the name the constructor remembered
        inline ~log_tracker() { LOG().type("OUT") << func_name_; }
        /// Name reported at both ends; the pointer is stored, so the text must outlive the tracker
        const char* func_name_{nullptr};
    };

    /// Record builder that submits at log_level::info
    typedef log_wapper<log_level::info> log_info;
    /// Record builder that submits at log_level::debug
    typedef log_wapper<log_level::debug> log_debug;
    /// Record builder that submits at log_level::warn
    typedef log_wapper<log_level::warn> log_warn;
    /// Record builder that submits at log_level::error
    typedef log_wapper<log_level::error> log_error;
    /// Record builder that submits at log_level::crash
    typedef log_wapper<log_level::crash> log_crash;

/// Declare a scope tracker that logs at LEVEL (a bare log_level enumerator name), named INFO
#define BLOCK_TRACKER(LEVEL, INFO) log_tracker<log_wapper<log_level::LEVEL>> __log_tracker__(INFO)
/// Track the entry and the exit of the enclosing scope, at info, under the function's own name
#define FUNC_TRACKER() BLOCK_TRACKER(info, __FUNCTION__)
/// FUNC_TRACKER with MSG appended to the name; MSG must be a string literal, not an object
#define FUNC_TRACKER_EX(MSG) BLOCK_TRACKER(info, __FUNCTION__ MSG)
}

#endif
