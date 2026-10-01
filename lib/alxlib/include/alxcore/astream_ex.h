/*****************************************************************/ /**
 * \file   astream_ex.h
 * \brief  File-backed stream implementations (extends astream.h)
 *
 * \author alexis
 *
 * Copyright (c) 2026 AlexisVon
 * SPDX-License-Identifier: MIT
 *********************************************************************/

#ifndef _ALEXIS_STREAM_EX_H_
#define _ALEXIS_STREAM_EX_H_

#include "afile.h"
#include "astream.h"

namespace alx {

    /**
     * \brief ostream writing to a disk file through a write buffer
     *
     * Bytes accumulate in dat_buff_ and reach the file only when the buffer would overflow, on
     * flush(), or at destruction; the constructor opens the file, truncating it without _append.
     * A failed open is not reported: append() keeps succeeding while the block fits the buffer,
     * and the failure surfaces only once the buffer overflows or flush() has bytes pending.
     * name() is the path. Calls are not serialized -- one thread at a time.
     */
    class ALXCORE_API ostream_file
        : public ostream {
    public:
        /**
         * \brief Open _file, allocate the write buffer and install the write path
         *
         * \param _append Append to the existing file; without it the file is truncated here
         * \param _buf_size Write buffer capacity; a block that does not fit goes straight through
         * \param _pre_exec Transform applied to each block before it is written, on the calling
         *                  thread. It is handed either the pending buffer or the caller's oversized
         *                  block, so it must accept an arbitrary split of the stream; its result is
         *                  written as is, so the file length may differ from total()
         */
        ostream_file(const file_info& _file, bool _append, uint_64 _buf_size = 0X8000U,
                     std::function<bytes(const void*, uint_64)> _pre_exec = nullptr);
        /// Write the pending bytes out (a failed write is dropped silently), then close the file
        ~ostream_file();

    public:
        /// The templated append() of ostream stays reachable despite the override below
        using ostream::append;
        /// Buffer the block; when it does not fit, flush what is buffered and write the block
        /// straight through. False only when the file write fails -- buffering never does
        virtual bool append(const void* _ptr, uint_64 _size) override;
        /// Write the buffered bytes through and drop them. True when nothing was pending, so on
        /// a stream whose file never opened it still reports success
        virtual bool flush() override;
        /// Drop the buffered bytes without writing them and reopen the path: truncated without
        /// _append, appended to with it. total() restarts at 0; a failed reopen is not reported
        /// and leaves the stream closed
        inline virtual void reset() override {
            dat_ofst_ = 0;
            dat_total_ = 0;
            file_.open(file_.info(), open_type_);
        }
        /// Bytes append() has accepted since construction or the last reset(); with a _pre_exec
        /// this is the pre-transform count, not the file size
        inline virtual uint_64 total() const override { return dat_total_; }

    protected:
        /// Write buffer capacity, fixed at construction
        const uint_64 buf_size_;
        /// file::APED when the constructor was asked to append, file::WRIT otherwise
        const uint_8 open_type_;
        /// Bytes currently held in dat_buff_
        uint_64 dat_ofst_;
        /// Write buffer, owned by this object
        uint_8* dat_buff_;
        /// Bytes accepted since construction or the last reset()
        uint_64 dat_total_;
        /// The file this stream writes to, opened by the constructor
        file file_;
        /// Writes one block to file_ -- through the constructor's transform when there is one --
        /// and flushes it; false when either step fails
        std::function<bool(const void*, uint_64)> fun_flush_;
    };

    /**
     * \brief istream reading a disk file at caller-given positions
     *
     * Reads are positional: get() takes the offset and returns a view of a freshly read buffer
     * that keeps those bytes alive. total() and the range get() clamps against come from the
     * file_info the stream was opened with, so a file that grows afterwards is not followed.
     * Calls are not serialized -- one thread at a time.
     */
    class ALXCORE_API istream_file
        : public istream {
    public:
        /**
         * \brief Open _file for reading
         *
         * The result of the open is not reported: a missing file or a directory leaves the stream
         * closed, total() 0 and every get() empty.
         *
         * \param _pre_exec Transform applied to each block after it is read, on the calling thread.
         *                  It has to keep the length: get() is random access, so total() and every
         *                  offset stay the file's own. A length-changing transform belongs on the
         *                  write side, which is sequential
         */
        istream_file(const file_info& _file, std::function<void(bytes&)> _pre_exec = nullptr);
        /// Close the file
        ~istream_file();

    public:
        /// Read _size bytes from _ofst, uint_64_npos reading to the end. A read past the end, or
        /// on a stream that never opened, is a shorter or empty view: there is no error signal
        virtual bytes_view get(uint_64 _ofst, uint_64 _size) const override;
        /// Size carried by the file_info the stream was opened with; 0 when the open failed
        virtual uint_64 total() const override;

    private:
        file file_;
        std::function<void(bytes&)> pre_exec_;
    };
}

#endif
