/*****************************************************************/ /**
 * \file   afile.h
 * \brief  File system operations (file info retrieval, directory management, file operations)
 *
 * \author alexis
 *
 * Copyright (c) 2026 AlexisVon
 * SPDX-License-Identifier: MIT
 *********************************************************************/

#ifndef _ALEXIS_FILE_H_
#define _ALEXIS_FILE_H_

#include "abytes.h"
#include "adatetime.h"
#include "astring.h"
#include "autility.h"

namespace alx {
    /// Open handle over one file; the contract is on the class body below
    class file;

    /**
     * \brief One stat() snapshot of a path, split into path, name and suffix
     *
     * The file system is touched once, by the constructor: the times and the size are the
     * values read then. A path that does not exist still gives a usable object -- is_valid()
     * only says a path string was given, is_exist() carries the stat() result. The path is
     * stored absolute, so a relative argument becomes absolute and, where it exists, is
     * resolved through symlinks.
     */
    class ALXCORE_API file_info {
    public:
        /// Empty info: is_valid() false, every query false or 0
        file_info() {}
        /// Same as the std::string constructor, for a C string
        explicit file_info(const char* _path) : file_info(std::string(_path)) {}
        /**
         * \brief Stat a path and split it into name and suffix
         *
         * Never fails. A path with no suffix gets an empty suffix(), and a directory's trailing
         * separator is dropped.
         *
         * \param _path Path to inspect; empty gives what the default constructor gives
         */
        explicit file_info(const std::string& _path);

    public:
        /// Absolute path as stored; empty for the empty input and for the POSIX root
        inline const std::string& path() const { return path_; }
        /// Last component of the path; empty for the POSIX root
        inline const std::string& name() const { return name_; }
        /// Extension of name(), dot included; empty when there is none
        inline const std::string& suffix() const { return suffix_; }
        /// Size in bytes from the stat(); for a directory, the directory itself, not its contents
        inline uint_64 size() const { return size_; }
        /// Raw creation time on Windows, status-change time on POSIX; see conver_file_time()
        inline uint_64 time_ct() const { return time_ct_; }
        /// Raw last access time; same unit as time_ct()
        inline uint_64 time_la() const { return time_la_; }
        /// Raw last write time; same unit as time_ct()
        inline uint_64 time_lw() const { return time_lw_; }
        /// True when the path resolves to a directory
        inline bool is_dir() const { return dir_; }
        /// True when the path exists; a broken symlink reports false
        inline bool is_exist() const { return exist_; }
        /// True for a Windows reparse point; on POSIX the stat() follows the link, so it stays false
        inline bool is_link() const { return link_; }
        /// True when a path string was given; a missing file is still valid
        inline bool is_valid() const { return !path_.empty(); }

        /// True for an absolute path: a leading '/' on POSIX, drive or UNC form on Windows
        inline static bool is_absolute(const std::string& _p) {
            if (_p.empty()) return false;
#ifdef _WIN32
            if (_p.size() >= 3 && _p[1] == ':' && _p[2] == '\\') return true;
            if (_p.size() >= 2 && _p[0] == '\\' && _p[1] == '\\') return true;
            return false;
#else
            return _p[0] == '/';
#endif
        }

        /// Info of the containing directory, stat()-ed afresh; empty when there is no parent
        file_info get_parent() const;
        /**
         * \brief List this directory's entries
         *
         * Every entry is stat()-ed into a fresh file_info; "." and ".." are skipped. Empty when
         * the object is not an existing directory, or the directory cannot be read.
         *
         * \param _filter Entry-name pattern; on POSIX an empty pattern matches every entry
         */
        std::vector<file_info> get_child(const std::string& _filter = "*") const;
        /**
         * \brief Create this path as a directory
         *
         * One level only: the parent must already exist. A no-op returning true when the path
         * already is a directory; false when it exists as something else or the creation
         * failed. Nothing is re-stat()-ed, the directory flag is simply set.
         *
         * \return true when the path is a directory afterwards
         */
        bool mkdir();

        /// Create one directory level, mode 0755 on POSIX; false when it exists or the parent is missing
        static bool mkdir(const std::string& _path);
        /// Remove an empty directory; false when it is missing or not empty
        static bool rmdir(const std::string& _path);
        /// Delete one file (unlink); false when the path is a directory or cannot be removed
        static bool rmfile(const std::string& _path);
        /// Rename; both paths must share a file system, and on Windows _to must not exist yet
        static bool mvfile(const std::string& _from, const std::string& _to);
        /**
         * \brief Total size in bytes of a file, or of a directory tree
         *
         * Recursive, and each level is stat()-ed again. With _ignore_link, an entry whose
         * is_link() is set counts 0 instead of being descended into.
         *
         * \param _info Root to measure; an invalid info gives 0
         * \param _ignore_link true to skip links rather than measure them
         */
        static uint_64 calsize(const file_info& _info, bool _ignore_link);
        /**
         * \brief Turn a raw timestamp of this struct into a datetime
         *
         * The unit is whatever time_ct()/time_la()/time_lw() store on this platform: seconds
         * since the epoch on POSIX, 100 ns ticks since 1601 on Windows.
         */
        static datetime conver_file_time(uint_64 _time);
        /**
         * \brief Make a path absolute and normalize it
         *
         * Resolves symlinks and "." / ".." as far as the path exists; one that does not exist
         * yet is still made absolute against the current directory. Empty in, empty out.
         */
        static std::string conver_std_path(const std::string& _path);

    private:
        friend class file;
        inline void set_size(const uint_64 _size) { size_ = _size; }

        std::string path_;
        std::string name_;
        std::string suffix_;
        uint_64 size_{0};
        uint_64 time_ct_{0}, time_la_{0}, time_lw_{0};
        bool exist_{false};
        bool dir_{false};
        bool link_{false};
    };

    /**
     * \brief Open handle over one file: a stdio stream plus the file_info it was opened with
     *
     * The object owns the stream, close() releases it and the destructor runs close(). One
     * handle is one stream, so calls on the same object from two threads race on the stream
     * position; the whole-file read_all()/write_all() statics need no handle at all.
     */
    class ALXCORE_API file : public noncopyable {
    public:
        /**
         * \brief Read a whole file into memory
         *
         * Reads to EOF rather than to the stat()-ed size, so a file that grew since _info was
         * built still comes back complete. An empty buffer does not tell success from failure
         * -- an empty file and a failed read both return an empty bytes -- check _ok when it
         * matters.
         *
         * \param _info File to read; a directory or a missing path fails without opening anything
         * \param _ok Optional; false when the open or the read failed
         * \return The file's bytes, owned by the caller
         */
        static bytes read_all(const file_info& _info, bool* _ok = nullptr);
        /// Same, for a path: no existence check first, the open simply fails
        static bytes read_all(const std::string& _path, bool* _ok = nullptr);
        /**
         * \brief Write a buffer to a file, creating it when needed
         *
         * All of the buffer or a false: a short write is retried until the buffer is out, and
         * the file is closed before returning.
         *
         * \param _info Destination; a directory fails without opening anything
         * \param _data Bytes to write; with _size 0 the pointer is not dereferenced
         * \param _size Byte count
         * \param _append true to append, false to truncate an existing file to nothing
         * \return false when the open, the write or the final close failed
         */
        static bool write_all(const file_info& _info, const void* _data, uint_64 _size, bool _append);
        /// Same, for a path
        static bool write_all(const std::string& _path, const void* _data, uint_64 _size, bool _append);
        /// Same, taking the buffer from anything with data() and size() (bytes, std::string, ...)
        template <typename T>
        inline static bool write_all(const file_info& _info, const T& _data, bool _append) { return write_all(_info, _data.data(), _data.size(), _append); }
        /// Same, for a path
        template <typename T>
        inline static bool write_all(const std::string& _path, const T& _data, bool _append) { return write_all(_path, _data.data(), _data.size(), _append); }

    public:
        /**
         * \brief Open mode of open(), as a bit mask
         *
         * Only six combinations are accepted: READ, WRIT or APED alone, or any of them OR-ed
         * with R__W. Anything else -- NONE, READ | WRIT, R__W on its own -- makes open() fail.
         */
        enum IO_TYPE : uint_8 {
            /// No mode: what io_type() reports while closed, and not a valid argument to open()
            NONE = 0X00U,
            /// Read a file that must exist
            READ = 0X01U,
            /// Write, truncating an existing file
            WRIT = 0X02U,
            /// Write, always at the end of the file
            APED = 0X04U,
            /// OR-ed into READ, WRIT or APED to add the other direction (rb+, wb+, ab+)
            R__W = 0X08U
        };
        /// Closed handle: is_open() false, io_type() NONE
        file() {}
        /// close() the handle
        ~file() { close(); }
        /**
         * \brief Open a file and take the handle
         *
         * An open object is closed first, and a failure leaves the object closed with an empty
         * info(): a failed reopen loses the previous file.
         *
         * \param _info File to open; a directory never opens, and the read modes need it to exist
         * \param _type A mask from IO_TYPE; any value outside the six valid ones fails
         * \return true when the handle is usable
         */
        bool open(const file_info& _info, uint_8 _type);
        /**
         * \brief Read a byte range at a position
         *
         * Valid on a handle opened READ or with R__W; a stream that cannot seek (a pipe, a FIFO)
         * is refused the same way. Reading at or past the size info() records is not an error,
         * it returns an empty buffer.
         *
         * \param _pos Offset to start at
         * \param _size Bytes wanted; uint_64_npos (the default) means to the end of the file
         * \param _ok Optional; false when the handle cannot read, cannot seek, or the read errored
         * \return The bytes read, owned by the caller -- fewer than _size when EOF comes earlier
         */
        bytes read(uint_64 _pos = 0, uint_64 _size = -1, bool* _ok = nullptr) const;
        /**
         * \brief Write a buffer at a position
         *
         * Valid on a handle opened WRIT, APED or with R__W. All of the buffer goes out or false.
         * info().size() is raised to the new end of file, never lowered, so a write inside the
         * file leaves the recorded size alone.
         *
         * \param _data Bytes to write; not dereferenced when _size is 0
         * \param _size Byte count
         * \param _pos Offset to write at; uint_64_npos (the default) writes at the current
         *             position. An append handle takes no notice of it: the data lands at the end
         * \return false when the handle is not write-able, or the seek or the write failed
         */
        bool write(const void* _data, uint_64 _size, uint_64 _pos = -1);
        /// Hand the stream buffer to the OS (fflush, no fsync); true when the handle cannot write
        bool flush();
        /// Close the handle and reset it to NONE; a no-op when it is already closed
        void close();
        /// write() for a bytes_view
        inline bool write(const bytes_view& _data, uint_64 _pos = -1) { return write(_data.data(), _data.size(), _pos); }
        /// write() for a string; the terminating NUL is not part of the payload
        inline bool write(const std::string& _data, uint_64 _pos = -1) { return write(_data.data(), _data.length(), _pos); }
        /**
         * \brief write() for a wide string
         *
         * The payload is _data.length() << 1 bytes taken from the wide buffer: the whole string
         * where wchar_t is 2 bytes, the first half of it where wchar_t is 4.
         */
        inline bool write(const std::wstring& _data, uint_64 _pos = -1) { return write(_data.data(), _data.length() << 1, _pos); }
        /// True between a successful open() and close()
        inline bool is_open() const { return m_type != NONE; }
        /// The mask open() was given; NONE while closed
        inline uint_8 io_type() const { return m_type; }
        /// The info open() was given, its size kept current by write(); a reference into the
        /// object, valid until the next open() or close()
        inline const file_info& info() const { return m_info; }
        /// True when the handle was opened WRIT, APED or with R__W
        inline bool is_write_able() const { return m_type & WRIT || m_type & APED || m_type & R__W; }
        /// True when the handle was opened READ or with R__W
        inline bool is_read_able() const { return m_type & READ || m_type & R__W; }

    private:
        FILE* m_file{nullptr};
        file_info m_info;
        uint_8 m_type{NONE};
    };
}

#endif