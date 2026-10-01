/*****************************************************************/ /**
 * \file   asqlite.h
 * \brief  SQLite3 database wrapper
 *
 * \author alexis
 *
 * Copyright (c) 2026 AlexisVon
 * SPDX-License-Identifier: MIT
 *********************************************************************/

#ifndef _ALEXIS_SQLITE_H_
#define _ALEXIS_SQLITE_H_

#include "abase.h"
#include "ajson.h"

namespace alx {
    /**
     * \brief SQLite3 connection: open, exec, prepare, transactions
     *
     * Owns one driver handle and closes it in the destructor. Move-only. A statement prepared
     * from the connection borrows it, so the connection must outlive every stmt it hands out.
     *
     * Threading is the caller's: the wrapper adds no lock of its own, except the transaction
     * bracket, which holds the connection mutex from start_transaction() to the matching commit
     * or rollback, so that another thread's bracket cannot interleave with it.
     */
    class ALXCORE_API sqlite {
    public:
        /// Create a closed connection; open() comes first
        sqlite() {}
        /// Close the connection, if it is still open
        ~sqlite() { close(); }
        /// Move the handle out of _v, which is left closed
        sqlite(sqlite&& _v) noexcept : db(_v.db) { _v.db = nullptr; }
        /// Replace the handle with _v's, which is left closed
        sqlite& operator=(sqlite&& _v) noexcept {
            if (this == &_v) return *this;
            close();
            db = _v.db;
            _v.db = nullptr;
            return *this;
        }
        /// No copy: the same handle would be closed twice
        sqlite(const sqlite&) = delete;
        /// No copy assignment, same reason
        sqlite& operator=(const sqlite&) = delete;

    public:
        /**
         * \brief Open a database file
         *
         * A failed open closes the handle again before returning, so last_error() afterwards
         * carries sqlite's null-handle message ("out of memory") and not the real reason.
         *
         * \param _db Path of the database file; ":memory:" opens a private in-memory database
         * \param _readonly Open with SQLITE_OPEN_READONLY -- writes refused, missing file not
         *                  created -- instead of the READWRITE|CREATE default
         * \return false when the driver refuses the file
         */
        bool open(const std::string& _db, bool _readonly = false);
        /**
         * \brief Close the connection
         *
         * No-op when not open. A statement prepared from it may outlive this call: the handle
         * goes to sqlite3_close_v2, which frees the connection once the last of those statements
         * is finalized -- the file itself is released at once.
         */
        void close();
        /// The connection's last error as text; the null-handle message when not open
        const char* last_error();
        /// True while a handle is held, that is from a successful open() until close()
        bool is_open() const { return nullptr != db; }

    public:
        /// True while the connection is inside an explicit transaction, however it was begun
        bool isin_transaction();
        /**
         * \brief Begin a transaction and take the connection mutex
         *
         * The mutex is held until commit_transaction() or rollback_transaction(), whatever
         * happens in between -- a failed BEGIN, a nested one for instance, does not release it.
         */
        bool start_transaction();
        /**
         * \brief Commit the transaction and release the connection mutex
         *
         * \return false when the COMMIT failed -- the transaction then stays open, but the mutex
         *         has been released all the same
         */
        bool commit_transaction();
        /// Roll back the open transaction and release the connection mutex; false on failure
        bool rollback_transaction();

    public:
        class token;
        /// Rows of a query, one vector per row: a token per column, its name and its value
        typedef std::list<std::vector<token>> result;
        /**
         * \brief Run _sql and throw the rows away
         *
         * Statements separated by semicolons are run in sequence.
         *
         * \return false on the first driver error, which last_error() then carries; the
         *         statements before it stay applied
         */
        bool exec(const std::string& _sql);
        /**
         * \brief Run _sql and append the rows it produces to _rst
         *
         * Every value arrives as text whatever its storage class, and a NULL column arrives as a
         * token whose null() is true. Rows are appended, so _rst is not cleared first, and a
         * statement that produces no rows leaves it untouched.
         *
         * \param _rst Receives the rows; the ones already in it are kept
         * \return false on the first driver error; rows produced before it are kept as well
         */
        bool exec(const std::string& _sql, result& _rst);

    public:
        /**
         * \brief Bits for the _flag argument of prepare(), one per SQLITE_PREPARE_* flag
         *
         * The first bit, persistent (0X01U), declares a long-lived statement the driver should
         * not keep in its lookaside memory; the default _flag of 0 sets no bit at all. Bits
         * outside this set are ignored.
         */
        enum prepare_flag : uint_8 { persistent = 0X01U,
                                     /// Hint that the statement is compiled once and run many times
                                     normalize = 0X02U,
                                     /// Accepted for compatibility: a no-op in SQLite
                                     novtable = 0X04U,
                                     /// Make a statement that uses a virtual table fail to compile
                                     dontlog = 0X08U };
        class stmt;
        /**
         * \brief Compile the first statement of _sql
         *
         * Text after that first statement is ignored. The statement keeps a pointer to this
         * connection, so the connection must outlive it.
         *
         * \param _sql SQL text; only its first statement is compiled
         * \param _flag OR of prepare_flag bits; 0 for none
         * \return An invalid statement -- valid() false -- when the SQL does not compile, with
         *         last_error() carrying the reason
         */
        stmt prepare(const std::string& _sql, uint_8 _flag = 0X00U);

    private:
        static int exec_callback(void* _rst, int _cols, char** _content, char** _header);

    private:
        void* db{nullptr};
    };

    /**
     * \brief One column of one result row: its name and its value as text
     *
     * Move-only. The value is owned by the token; a token that carries none reports null() true.
     */
    class ALXCORE_API sqlite::token {
    public:
        /// Copy _key and _val; a null _val makes a value-less token
        token(const char* _key, const char* _val = nullptr)
            : key_(_key), val_(nullptr == _val ? nullptr : new std::string(_val)) {
        }
        /**
         * \brief Take over _key and the string _val points to
         *
         * \param _key Column name, copied
         * \param _val Value the token owns from here on, deleted by its destructor; a null
         *             pointer makes a value-less token
         */
        token(const std::string& _key, std::string* _val = nullptr) : key_(_key), val_(_val) {}
        /// Move the value out of _v, which is left value-less
        token(token&& _v) noexcept : key_(_v.key_), val_(_v.val_) { _v.val_ = nullptr; }
        /// Replace key and value with _v's, which is left value-less
        token& operator=(token&& _v) noexcept {
            if (this == &_v) return *this;
            delete val_;
            key_ = _v.key_;
            val_ = _v.val_;
            _v.val_ = nullptr;
            return *this;
        }
        /// No copy: the owned value would be deleted twice
        token(const token&) = delete;
        /// No copy assignment, same reason
        token& operator=(const token&) = delete;
        /// Delete the owned value, if there is one
        ~token() { delete val_; }

    public:
        /// True when the token carries no value -- value() must not be called then
        inline bool null() const { return nullptr == val_; }
        /// Column name
        inline const std::string& key() const { return key_; }
        /// The value; only valid while null() is false
        inline const std::string& value() const { return *val_; }

    private:
        std::string key_;
        std::string* val_{nullptr};
    };

    /**
     * \brief A prepared statement, finalized by its destructor
     *
     * Move-only, and bound to the connection it came from, which must outlive it -- last_error()
     * dereferences that connection. A run is bind, exec, read the columns while exec() returns
     * data, then reset() before binding or stepping again; the column accessors are only
     * meaningful for the row the last step produced.
     */
    class ALXCORE_API sqlite::stmt {
    public:
        /**
         * \brief Wrap an already prepared driver handle and take it over
         *
         * \param _dbp Connection the handle belongs to; it must outlive this statement
         * \param _ptr Driver handle, finalized by the destructor; null gives an invalid statement
         */
        stmt(sqlite* _dbp, void* _ptr) : db_(_dbp), stmt_(_ptr) {}
        /// Finalize the statement; the connection it came from is left alone
        ~stmt();
        /// Move the handle out of _v, which is left invalid
        stmt(stmt&& _v) noexcept : db_(_v.db_), stmt_(_v.stmt_) { _v.stmt_ = nullptr; }
        /// Replace the handle with _v's, which is left invalid
        stmt& operator=(stmt&& _v) noexcept {
            if (this == &_v) return *this;
            finalize();
            db_ = _v.db_;
            stmt_ = _v.stmt_;
            _v.stmt_ = nullptr;
            return *this;
        }
        /// No copy: the same handle would be finalized twice
        stmt(const stmt&) = delete;
        /// No copy assignment, same reason
        stmt& operator=(const stmt&) = delete;

    public:
        /// True when a driver handle is held and the connection it came from is known
        bool valid() const { return nullptr != stmt_ && nullptr != db_; }
        /// The owning connection's last error as text
        const char* last_error() const { return db_->last_error(); }

    public:
        /// Bind NULL to parameter _idx, 1-based; false when the index is out of range
        bool bind(int _idx);
        /// Bind _val as an integer
        bool bind(int _idx, int _val);
        /// Bind _val as a REAL
        bool bind(int _idx, double _val);
        /// Bind _val as a 64-bit integer
        bool bind(int _idx, long long _val);
        /// Bind _val as an integer, 1 or 0
        bool bind(int _idx, bool _val);
        /// Bind a NUL-terminated string, copied into the statement
        bool bind(int _idx, const char* _val);
        /// Bind a string of _val.length() bytes, copied; embedded NULs are kept
        bool bind(int _idx, const std::string& _val);
        /// Bind _val as a BLOB, copied into the statement
        bool bind(int _idx, const bytes_view& _val);
        /**
         * \brief Bind a BLOB of _len bytes at _ptr, without copying it
         *
         * _del_func is the destructor the driver calls on _ptr. nullptr keeps ownership with the
         * caller, which obliges _ptr to stay valid until the statement is finalized or the
         * parameter is bound again; SQLITE_TRANSIENT has the bytes copied instead.
         */
        bool bind(int _idx, const void* _ptr, const uint_64 _len, void (*_del_func)(void*));
        /// Bind a BLOB of _size zero bytes, which the driver holds without allocating them
        bool bind_zeroblob(int _idx, uint_64 _size);
        /// Set every parameter back to NULL; reset() does not do this
        bool bind_clear();
        /// Number of parameters the statement has, named or not
        int bind_count();
        /// Name of parameter _idx as written in the SQL; null when nameless or out of range
        const char* bind_name(int _idx);
        /// Index of the parameter named _key; 0 when there is no such parameter
        int bind_index(const char* _key);
        /// Index of the parameter named _key; 0 when there is no such parameter
        int bind_index(const std::string& _key) { return bind_index(_key.c_str()); }

    public:
        /**
         * \brief What one step of the statement produced
         *
         * done (0) ends a successful run; fail collects every other driver code -- SQLITE_BUSY,
         * a constraint violation, misuse -- and leaves the reason to last_error().
         */
        enum state : uint_8 { done = 0,
                              /// The step hit an error; the statement stays spent until reset()
                              fail = 1,
                              /// A row is ready; read its columns, then step again for the next
                              data = 2 };
        /**
         * \brief Evaluate the statement once
         *
         * Call it again while it returns data. After done or fail the statement must be reset()
         * before it is bound or stepped again.
         *
         * \return the state of this step
         */
        state exec();
        /**
         * \brief Rewind the statement so it can be bound and stepped again
         *
         * Bindings survive the rewind -- bind_clear() drops those. The statement is rewound in
         * every case; the return value reports the previous evaluation, not the rewind.
         *
         * \return false when the previous exec() failed
         */
        bool reset();

    public:
        /// Number of columns the result set has; 0 for a statement that returns no rows
        int column_count();
        /// Name of column _idx, 0-based, copied out of the driver
        std::string column_name(int _idx);

        /// Number of columns ready in the current row; 0 before the first step and after done
        int data_count();
        /**
         * \brief Storage class of a column in the current row
         *
         * _null_ (0) is also the answer for a column that is not there: before the first step,
         * after done, and for any _idx outside the row.
         */
        enum type : uint_8 { _null_ = 0,
                             /// An integer, up to 64 bits wide
                             _intg_,
                             /// An 8-byte IEEE float
                             _real_,
                             /// A string
                             _text_,
                             /// A byte string
                             _blob_,
        };
        /// Storage class of column _idx of the current row
        type data_type(int _idx);

        /**
         * \brief The column's value as a variant of the matching type
         *
         * _intg_ becomes a long long, _real_ a double, _text_ a std::string, _blob_ bytes; a NULL
         * column becomes an empty variant.
         */
        variant get_variant(int _idx);
        /**
         * \brief The column's value as JSON
         *
         * Integers, reals and strings map as in get_variant(); a BLOB becomes a base64 string,
         * json_value having no binary type; a NULL becomes an empty value.
         */
        json_value get_jsonval(int _idx);

        /// Column _idx as a 64-bit integer, the driver converting text and reals
        long long get_intg(int _idx);
        /// Column _idx as a double, the driver converting text and integers
        double get_real(int _idx);
        /// Column _idx as text, the driver converting; empty when the column is NULL
        std::string get_text(int _idx);
        /// Column _idx as bytes, the driver converting; empty when the column is NULL
        bytes get_blob(int _idx);

    private:
        /// Finalize the held handle, if any, and leave the statement invalid
        void finalize();
        sqlite* db_{nullptr};
        void* stmt_{nullptr};
    };
}

#endif