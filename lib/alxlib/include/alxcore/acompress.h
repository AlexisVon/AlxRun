/*****************************************************************/ /**
 * \file   acompress.h
 * \brief  Data compression (integrated LZ4 algorithm)
 *
 * \author alexis
 *
 * Copyright (c) 2026 AlexisVon
 * SPDX-License-Identifier: MIT
 *********************************************************************/

#ifndef _ALEXIS_COMPRESS_H_
#define _ALEXIS_COMPRESS_H_

#include "abytes.h"

namespace alx {
    namespace compress {
        /**
         * \brief LZ4 block encoder, one-shot or chained across exec() calls
         *
         * The sexec() forms compress one buffer and keep no state; the result is a bare LZ4 block,
         * with no frame header and no checksum, so only decoder_lz4 reads it back and it has to be
         * told the original size. The exec() forms keep the tail of the previous block as a 64 KB
         * dictionary, so successive calls compress as one chain: each result is one block of that
         * chain, and the decoder must be given the same blocks, in the same order.
         *
         * A null _src, a _size that is not positive, and an input above LZ4's 2 GB limit all fail
         * every form here, without allocating. One instance owns one stream context, so exec() needs
         * the caller's own serialization; the static sexec() forms share no state.
         */
        class ALXCORE_API encoder_lz4 : public noncopyable {
        public:

            /**
             * \brief Compress one buffer into a standalone block
             *
             * \param _speed LZ4 acceleration: 1 (0 is read as 1) is the default and gives the best
             *               ratio, higher trades ratio for speed
             * \return The compressed block, null bytes on failure
             */
            static bytes sexec(const bytes_view& _src, uint_16 _speed = 1);

            /// Same, for a raw buffer
            static bytes sexec(const uint_8* _src, int_32 _size, uint_16 _speed = 1);

            /**
             * \brief Compress one buffer into a caller-owned block buffer
             *
             * \param _out Destination, resized to the worst-case bound first and down to the exact
             *             block size on success; on failure it is left at the bound size, holding
             *             partial output
             * \return false on the same conditions
             */
            static bool sexec(const bytes_view& _src, bytes& _out, uint_16 _speed = 1);

            /// Same, for a raw buffer
            static bool sexec(const uint_8* _src, int_32 _size, bytes& _out, uint_16 _speed = 1);

        public:
            /// Create the stream context and the 64 KB dictionary
            encoder_lz4();
            /// Free both
            ~encoder_lz4();

        public:

            /**
             * \brief Compress the next block of a chain
             *
             * The result continues the chain built by the earlier calls: its matches may point into
             * the previous blocks, so it can only be decoded by a decoder that has been given them,
             * in order. The tail of this block is saved internally, so the input buffer may be
             * reused or freed as soon as the call returns.
             *
             * \param _speed LZ4 acceleration, as in sexec()
             * \return The compressed block, null bytes on failure
             */
            bytes exec(const bytes_view& _src, uint_16 _speed = 1);

            /// Same, for a raw buffer
            bytes exec(const uint_8* _src, int_32 _size, uint_16 _speed = 1);

            /// Cut the chain, so the next exec() starts an independent block; cheaper than reset()
            void clear();

            /// Cut the chain and zero the whole context; the next exec() starts an independent block
            void reset();

        private:
            void update();

        private:
            void* cmps_{nullptr};
            uint_8* buff_{nullptr};
        };
        /**
         * \brief LZ4 block decoder, one-shot or chained across exec() calls
         *
         * A block carries neither its original size nor its own length, so both the size and the
         * exact block boundaries have to come from outside. The sexec() forms decode one standalone
         * block and keep no state; the exec() forms keep the tail of the decoded block as a 64 KB
         * dictionary for the next one, and must be fed exactly the blocks that encoder_lz4::exec()
         * produced, one call per block. A fresh instance's first exec() decodes a standalone block.
         *
         * A _source_size below the real original size fails the decode; a larger one is harmless,
         * being clamped to the block's worst-case expansion before anything is allocated.
         */
        class ALXCORE_API decoder_lz4 : public noncopyable {
        public:

            /**
             * \brief Decode one standalone block
             *
             * \param _source_size The original size, or any upper bound of it
             * \return The original data, null bytes when the block is malformed or _source_size is
             *         too small for it
             */
            static bytes sexec(const bytes_view& _src, int_32 _source_size);

            /// Same, for a raw buffer
            static bytes sexec(const uint_8* _src, int_32 _size, int_32 _source_size);

            /**
             * \brief Decode one standalone block into a caller-owned buffer
             *
             * \param _out Output buffer, sized by the caller: its size is the only size hint there
             *             is, so it must already hold the whole original data. Shrunk to the exact
             *             size on success, left untouched on failure
             * \return false when the block is malformed or _out is too small
             */
            static bool sexec(const bytes_view& _src, bytes& _out);

            /// Same, for a raw buffer
            static bool sexec(const uint_8* _src, int_32 _size, bytes& _out);

        public:
            /// Create the decode context and the 64 KB dictionary, and start it empty
            decoder_lz4();
            /// Free both
            ~decoder_lz4();

        public:

            /**
             * \brief Decode the next block of a chain
             *
             * Returns this block's data only; its tail is kept as the dictionary of the next call.
             *
             * \param _source_size Original size of this block, or any upper bound of it
             * \return This block's data, null bytes on failure
             */
            bytes exec(const bytes_view& _src, int_32 _source_size);

            /// Same, for a raw buffer
            bytes exec(const uint_8* _src, int_32 _size, int_32 _source_size);

            /// Same as reset()
            void clear() { reset(); }

            /// Drop the dictionary: the next exec() starts a new chain, from a standalone block
            void reset();

        private:
            void update(const bytes_view& _data);

        private:
            void* cmps_{nullptr};
            uint_8* buff_{nullptr};
        };
        /**
         * \brief gzip encoder, one shot or one member written across calls
         *
         * The sexec() forms write a complete gzip member -- header, data, CRC trailer -- in one
         * call. The exec() forms write into a single member, flushed after each call: the header
         * goes out with the first call, no trailer is ever written, and clear() or reset() abandon
         * the member where it stands, so the next exec() opens another. The deflate window is kept
         * between calls, so data repeated across them compresses better.
         *
         * A null _src and a _size that is not positive fail every form here, without allocating.
         */
        class ALXCORE_API encoder_gzip : public noncopyable {
        public:

            /**
             * \brief Compress one buffer into a complete gzip member
             *
             * \param _level zlib level: 6 is the default, 1 the fastest, 9 the best, and 0 stores
             *               without compressing; a level above 9 fails the call
             * \return The compressed member, null bytes on failure
             */
            static bytes sexec(const bytes_view& _src, uint_16 _level = 6);

            /// Same, for a raw buffer
            static bytes sexec(const uint_8* _src, int_32 _size, uint_16 _level = 6);

            /**
             * \brief Compress one buffer into a complete gzip member, in a caller-owned buffer
             *
             * \param _out Destination, resized to the bound first and down to the exact member size
             *             on success; left at the bound size, holding partial output, on failure
             * \return false on the same conditions
             */
            static bool sexec(const bytes_view& _src, bytes& _out, uint_16 _level = 6);

            /// Same, for a raw buffer
            static bool sexec(const uint_8* _src, int_32 _size, bytes& _out, uint_16 _level = 6);

        public:
            /// Create the stream, left uninitialized until the first exec()
            encoder_gzip();
            /// End the stream and free it
            ~encoder_gzip();

        public:

            /**
             * \brief Compress the next chunk of the member in progress
             *
             * Returns the bytes this chunk produced -- not the whole member. The first call
             * initializes the stream, and it is the one whose _level counts; the calls after it
             * ignore theirs.
             *
             * \param _level zlib level, as in sexec()
             * \return This chunk's bytes, null bytes when the stream refused the input
             */
            bytes exec(const bytes_view& _src, uint_16 _level = 6);

            /// Same, for a raw buffer
            bytes exec(const uint_8* _src, int_32 _size, uint_16 _level = 6);

            /// Abandon the member in progress, keeping the level; the next exec() opens another
            void clear();

            /// Drop the stream; the next exec() initializes a new one from its own _level
            void reset();

        private:
            void* cmps_{nullptr};
        };
        /**
         * \brief gzip decoder, one member at a time
         *
         * The sexec() forms take a complete member, trailer included; the exec() forms take the
         * chunks of a member as they arrive, return the bytes each chunk yields, and accept a chunk
         * that ends mid-member -- a stream written by encoder_gzip::exec() reads back through exec()
         * only, since no trailer is ever written for it. One member ends the stream: the next one
         * needs clear() first.
         *
         * The output buffer starts at 4x the compressed size and doubles as it fills, up to 1 GiB
         * per call; a call that would need more fails instead of growing.
         */
        class ALXCORE_API decoder_gzip : public noncopyable {
        public:

            /**
             * \brief Decompress a complete gzip member
             *
             * \return The original data, null bytes when the input is not a complete, valid member
             */
            static bytes sexec(const bytes_view& _src);

            /// Same, for a raw buffer
            static bytes sexec(const uint_8* _src, int_32 _size);

            /**
             * \brief Decompress a complete gzip member into a caller-owned buffer
             *
             * \param _out Output buffer; a non-empty one is used as it is, so it must already hold
             *             the whole result, an empty one is sized as the form returning bytes does.
             *             Shrunk to the exact size on success
             * \return false on the same conditions, with _out holding what was decoded into it
             */
            static bool sexec(const bytes_view& _src, bytes& _out);

            /// Same, for a raw buffer
            static bool sexec(const uint_8* _src, int_32 _size, bytes& _out);

        public:
            /// Create the stream, left uninitialized until the first exec()
            decoder_gzip();
            /// End the stream and free it
            ~decoder_gzip();

        public:

            /**
             * \brief Decompress the next chunk of the member in progress
             *
             * Returns the bytes this chunk yielded, not all data decoded so far. A chunk ending
             * mid-member is not an error: the call returns what it has, and the next one continues.
             *
             * \return This chunk's bytes, null bytes on a decode error, which also drops the stream
             */
            bytes exec(const bytes_view& _src);

            /// Same, for a raw buffer
            bytes exec(const uint_8* _src, int_32 _size);

            /// Same as reset()
            void clear() { reset(); }

            /// Drop the stream; the next exec() starts a new member
            void reset();

        private:
            void* cmps_{nullptr};
        };
        /**
         * \brief zstd encoder, one frame per sexec() or one frame written across calls
         *
         * The sexec() forms write a complete frame, content size included, in one call. The exec()
         * forms write into a single frame, flushed after each call, and leave it open -- no end
         * marker is ever written -- so their bytes are readable by decoder_zstd::exec() only. The
         * level belongs to the frame, and is fixed by the call that opens it.
         *
         * A null _src and a _size that is not positive fail every form here, without allocating.
         */
        class ALXCORE_API encoder_zstd : public noncopyable {
        public:

            /**
             * \brief Compress one buffer into a complete frame
             *
             * \param _level zstd level: 3 is the default, 1 the fastest, 22 the best; 0 asks for
             *               the library default, and a level above 22 is clamped down to the best
             * \return The compressed frame, null bytes on failure
             */
            static bytes sexec(const bytes_view& _src, uint_16 _level = 3);

            /// Same, for a raw buffer
            static bytes sexec(const uint_8* _src, int_32 _size, uint_16 _level = 3);

            /**
             * \brief Compress one buffer into a complete frame, in a caller-owned buffer
             *
             * \param _out Destination, resized to the bound first and down to the exact frame size
             *             on success; left at the bound size, holding partial output, on failure
             * \return false on the same conditions
             */
            static bool sexec(const bytes_view& _src, bytes& _out, uint_16 _level = 3);

            /// Same, for a raw buffer
            static bool sexec(const uint_8* _src, int_32 _size, bytes& _out, uint_16 _level = 3);

        public:
            /// Create the compression context
            encoder_zstd();
            /// Free it
            ~encoder_zstd();

        public:

            /**
             * \brief Compress the next chunk of the frame in progress
             *
             * Returns the bytes this chunk produced -- not the whole frame. The level is read when
             * a frame opens, so a value given to a later call does not re-level the frame that is
             * already in progress.
             *
             * \param _level zstd level, as in sexec()
             * \return This chunk's bytes, null bytes on failure
             */
            bytes exec(const bytes_view& _src, uint_16 _level = 3);

            /// Same, for a raw buffer
            bytes exec(const uint_8* _src, int_32 _size, uint_16 _level = 3);

            /// Abandon the frame in progress; the next exec() opens a new one
            void clear();

            /// Free the context; the next exec() creates a fresh one
            void reset();

        private:
            void* cmps_{nullptr};
        };
        /**
         * \brief zstd decoder, one frame at a time
         *
         * The sexec() forms take a complete frame and reject one that is still in progress; the
         * exec() forms take the chunks of a frame as they arrive, return the bytes each chunk
         * yields, and accept a frame that is not finished yet. A frame written by
         * encoder_zstd::exec() carries no content size, so it reads back through exec() only.
         *
         * A frame that declares its content size is allocated once, at exactly that size, but a
         * claim above 1 GiB is refused before anything is allocated; the streamed path doubles its
         * buffer as it fills, up to the same cap.
         */
        class ALXCORE_API decoder_zstd : public noncopyable {
        public:

            /**
             * \brief Decompress one complete frame
             *
             * \return The original data, null bytes when the frame is invalid, declares more than
             *         1 GiB, or is still open -- a flushed-only frame has no end marker yet
             */
            static bytes sexec(const bytes_view& _src);

            /// Same, for a raw buffer
            static bytes sexec(const uint_8* _src, int_32 _size);

            /**
             * \brief Decompress one complete frame into a caller-owned buffer
             *
             * For a frame that declares its content size, _out is resized to that size before the
             * decode, so a failure leaves it at that size, holding the partial output; a frame
             * declaring more than 1 GiB is refused before _out is touched.
             *
             * \return false on the same conditions
             */
            static bool sexec(const bytes_view& _src, bytes& _out);

            /// Same, for a raw buffer
            static bool sexec(const uint_8* _src, int_32 _size, bytes& _out);

        public:
            /// Create the decompression context
            decoder_zstd();
            /// Free it
            ~decoder_zstd();

        public:

            /**
             * \brief Decompress the next chunk of a frame, finished or not
             *
             * Returns the bytes this chunk yielded, not all data decoded so far. The call stops at
             * the end of a frame, and bytes past that point in the same chunk are dropped.
             *
             * \return This chunk's bytes, null bytes on failure, which also frees the context, so
             *         the next call starts from a fresh one
             */
            bytes exec(const bytes_view& _src);

            /// Same, for a raw buffer
            bytes exec(const uint_8* _src, int_32 _size);

            /// Same as reset()
            void clear() { reset(); }

            /// Free the context; the next exec() creates a fresh one
            void reset();

        private:
            void* cmps_{nullptr};
        };
    };
}

#endif