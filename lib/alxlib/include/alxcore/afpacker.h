/*****************************************************************/ /**
 * \file   afpacker.h
 * \brief  File system packing and unpacking tool (supports optional encryption and compression)
 *
 * \author alexis
 *
 * Copyright (c) 2026 AlexisVon
 * SPDX-License-Identifier: MIT
 *********************************************************************/

#ifndef _ALEXIS_FPACKER_H_
#define _ALEXIS_FPACKER_H_

#include "aaes.h"
#include "acompress.h"
#include "afile.h"
#include "astream.h"
#include "avariant.h"

namespace alx {
    namespace fpacker {

        /**
         * \brief Progress and error callback, shared by encoder and decoder
         *
         * _v1 == (uint_64) -1 reports a failure: _v2 is the uint_8 code that the call is about to
         * return. Any other _v1 is progress: _v1 is the total size of the entry being processed
         * and _v2 how much of it is done. _path is the source path when packing; when unpacking
         * it is the name of the output stream -- the target file for save(), empty for a memory
         * buffer.
         */
        typedef std::function<void(const std::string&, uint_64, uint_64)> MesgFunc;
        /**
         * \brief Write a packed archive: the file data in blocks, then an index of the packed tree
         *
         * Layout, every field written raw in host byte order:
         *   [0]   uint_32 magic 0x5F415A4C
         *   [4]   uint_32 0x5F535043 when the blocks are LZ4-compressed, 0 otherwise
         *   [8]   uint_32 0x5F534541 when the blocks are AES-CTR encrypted, 0 otherwise
         *   [12]  the file data, in blocks, in append() order
         *   [..]  the index: the packed tree serialized with varsolid, compressed and encrypted
         *         under those same two flags
         *   [-20] uint_64 index size before compression
         *   [-12] uint_64 index size as stored
         *   [-4]  uint_32 magic 0x5F455942
         *
         * There is no version field: the two flag words are the whole format marker. The encoder
         * owns the stream given to start() and deletes it.
         */
        class ALXCORE_API encoder : public noncopyable {
        public:

            /**
             * \brief Configure the archive to write
             *
             * The password is hashed once with SHA-256 into the AES-256-CTR key, so the same
             * password always yields the same key: use a fresh one for each archive. Salting and
             * key stretching are the caller's business. An empty password turns encryption off.
             *
             * \param _cmps Compress the data blocks and the index with LZ4
             * \param _pswd Password; empty means no encryption
             * \param _blck_size Plain bytes of one file per block, default 64 MiB
             */
            encoder(bool _cmps, const std::string& _pswd, uint_64 _blck_size = 0X04000000U);
            /// Finish an open archive as finish() does, then delete the stream
            ~encoder();

        public:

            /**
             * \brief Open a new archive on a stream, taking ownership of it
             *
             * Writes the 12-byte header and clears the index. An archive already open is finished
             * first; the new stream is only taken when that succeeds.
             *
             * \param _ostm Output stream; deleted by finish() or by the destructor
             * \return false only when a previously opened archive could not be finished
             */
            bool start(ostream* _ostm);

            /**
             * \brief Write the index and the trailer, then close the archive
             *
             * A no-op returning true when no archive is open, so a second call is harmless. On
             * success the stream is deleted; on failure it stays with the encoder and the
             * destructor deletes it.
             *
             * \return false when the index cannot be serialized or compressed, or cannot be
             *         written and flushed
             */
            bool finish();
            /// Result codes of append(); error_message() turns one into text
            enum : uint_8 {
                /// The call did what it was asked to
                success = 0,
                /// The source path does not exist
                notexist,
                /// The source path is a symbolic link; links are never packed
                linkfile,
                /// A name is already taken at that level of the index
                duppath,
                /// The source could not be opened, or its size changed while it was read
                readfail,
                /// LZ4 returned nothing for a block
                cmpsfail,
                /// The output stream refused a write
                flushfail,
                /// The abort flag was raised; the archive is left incomplete
                forceabort,
            };

            /**
             * \brief Add one file or one directory tree to the archive
             *
             * Valid only between start() and finish(). A file is cut into blocks of at most
             * _blck_size plain bytes, compressed and encrypted as configured, and the CRC-32C of
             * every block's plain bytes goes into the index. A directory is walked recursively into
             * a nested index entry; the first failing child drops the whole subtree, while the
             * blocks already written stay in the stream, unreferenced.
             *
             * \param _finf Source file or directory
             * \param _name Name the entry takes in the index, i.e. the root name used by get()
             * \return success, or the first failure encountered; a missing source or a symbolic
             *         link still returns success when the matching ignore flag is set, though the
             *         message callback reports it either way
             */
            uint_8 append(const file_info& _finf, const std::string& _name);

            /**
             * \brief Abort packing on an external flag
             *
             * The flag is read before every file and every block, so raising it costs at most one
             * block. It must outlive the calls that may read it; null disables the check.
             *
             * \param _abort_flag Caller-owned flag; packing stops once *flag is true
             */
            inline void set_abort_flag(const bool* _abort_flag) { abort_ = _abort_flag; }
            /// The stream passed to start(); null before start() and after a successful finish()
            inline const ostream* get_stream() const { return ostm_; }
            /// Install the progress callback; null, the default, reports nothing
            inline void set_print_rtmsg(MesgFunc _fc) { print_rtmsg_ = _fc; }
            /// True when a missing source is skipped instead of failing the append
            inline bool is_igerr_notexist() const { return igerr_notexist_; }
            /// Skip a missing source: the callback still reports notexist, append() returns success
            inline void set_igerr_notexist(bool _ig) { igerr_notexist_ = _ig; }
            /// True when a symbolic link is skipped instead of failing the append
            inline bool is_igerr_linkfile() const { return igerr_linkfile_; }
            /// Skip a symbolic link: the callback still reports linkfile, append() returns success
            inline void set_igerr_linkfile(bool _ig) { igerr_linkfile_ = _ig; }

        public:
            /**
             * \brief Text for a result code
             *
             * \return A static string, never null; success and every unrecognized code give
             *         "unknown"
             */
            static const char* error_message(uint_8 _err);

        private:
            static uint_8 append_impl(const file_info& _finf, const std::string& _name, encoder* _this, varmap& _root);
            static uint_8 append_impl_dir(const file_info& _finf, const std::string& _name, encoder* _this, varmap& _root);
            static uint_8 append_impl_file(const file_info& _finf, const std::string& _name, encoder* _this, varmap& _root);

        private:
            compress::encoder_lz4* cmps_;
            ostream* ostm_;
            uint_64 size_;
            varmap fsys_;
            bytes pswd_;

            const bool* abort_{nullptr};
            MesgFunc print_rtmsg_;
            bool igerr_notexist_{false};
            bool igerr_linkfile_{false};
        };

        /**
         * \brief Read a packed archive: header and index at construction, file data on demand
         *
         * The constructor reads the 12-byte header for the two flags, then the 20-byte trailer and
         * the whole index, so the stream must support reading at an arbitrary offset -- the trailer
         * is read before anything else. The stream is owned and deleted by the destructor.
         *
         * valid() reports whether the index came out of that read: a truncated trailer, a wrong
         * password, or an index that will not decrypt, decompress or deserialize leaves it false
         * instead of throwing. When the header says the archive is not encrypted, the password is
         * dropped and the index is taken as plain.
         */
        class ALXCORE_API decoder : public noncopyable {
        public:

            /**
             * \brief Whether a stream is marked as encrypted, judged from its header alone
             *
             * Reads the flag word at offset 8, so a caller can pick the password before building a
             * decoder. False when the stream is too short or the word says anything else.
             *
             * \param _istm Stream to inspect; read positionally, so it is left as it was
             */
            static bool is_encrypt(const istream& _istm);

            /**
             * \brief Whether a stream is marked as compressed, judged from its header alone
             *
             * Reads the flag word at offset 4. False when the stream is too short or the word says
             * anything else.
             *
             * \param _istm Stream to inspect; read positionally, so it is left as it was
             */
            static bool is_compress(const istream& _istm);

        public:

            /**
             * \brief Open an archive and read its index
             *
             * The password is hashed once with SHA-256 into the AES-256-CTR key and is used only
             * when the header marks the archive as encrypted; given an empty password, an encrypted
             * archive cannot be read and valid() ends up false.
             *
             * \param _istm Stream to own; deleted by the destructor, and read from at construction
             * \param _pswd Password; empty means no decryption
             */
            decoder(istream* _istm, const std::string& _pswd);
            /// Delete the stream
            ~decoder();

        public:

            /// True when the index was read and holds at least one entry
            inline bool valid() const { return !fsys_.empty(); }

            /**
             * \brief The packed tree as it was indexed
             *
             * Nested varmaps for directories, one varvec of block records per file -- the shape
             * that get() and save() walk with a path. Valid for the life of the decoder.
             */
            inline const varmap& fsystem() const { return fsys_; }

        public:
            /// Result codes of get() and save(); error_message() turns one into text
            enum : uint_8 {
                /// The call did what it was asked to
                success = 0,
                /// No such root in the index, or the path resolved to nothing
                notexist,
                /// The path resolved to a directory where a file was wanted
                notfile,
                /// The save target is missing, or is not a directory
                needdir,
                /// The selected entry is neither a directory nor a file
                badfsys,
                /// A block record is malformed, or a block failed its CRC-32C check
                badblock,
                /// A block could not be read in full, or the entry's size does not add up
                readfail,
                /// Never produced by this version; a failed write surfaces as flushfail
                writefail,
                /// A directory below the save target could not be created
                cdirfail,
                /// LZ4 could not decompress a block or the index
                dcmpsfail,
                /// The output stream refused a write or a flush
                flushfail,
                /// The abort flag was raised; the target may hold a partial entry
                forceabort,
            };

            /**
             * \brief Read one file out of the archive into memory
             *
             * Every block named by the index is read, decrypted, decompressed and checked against
             * its stored CRC-32C before its bytes are appended to _odat, so what lands there is
             * plain data. A CRC covers one plain block; the index has no checksum of its own.
             * _odat is cleared first, and keeps what was decoded before a failing block.
             *
             * \param _root Root name handed to encoder::append()
             * \param _path Names below that root; empty selects _root itself
             * \param _odat Filled with the file content
             * \return success, or the first failure among notexist, notfile, badblock, readfail,
             *         dcmpsfail, flushfail and forceabort
             */
            uint_8 get(const std::string& _root, const std::list<std::string>& _path, bytes& _odat) const;

            /**
             * \brief Read one file out of the archive into an output stream
             *
             * Same reads and checks as the bytes form. The stream is only appended to -- never
             * rewound or truncated -- and flushed once the entry is complete.
             *
             * \param _root Root name handed to encoder::append()
             * \param _path Names below that root; empty selects _root itself
             * \param _ostm Destination, written at its current position
             * \return As the bytes form
             */
            uint_8 get(const std::string& _root, const std::list<std::string>& _path, ostream& _ostm) const;

            /**
             * \brief Extract one entry, or the whole archive, onto disk
             *
             * With _root empty or "*" the whole index is written under _dir and _path is ignored.
             * Otherwise _root must be in the index, and the entry its _path selects becomes
             * _dir/_path.back(), or _dir/_root when _path is empty. Names are converted to the
             * local charset on the way out, files are opened for write -- an existing target is
             * truncated -- and the directories below _dir are created as needed; _dir itself must
             * already exist.
             *
             * \param _root Root name, or empty / "*" for the whole archive
             * \param _path Names below that root; the last one names the extracted entry
             * \param _dir Existing directory to extract into
             * \return success, or needdir, notexist, cdirfail, badfsys and the get() codes
             */
            uint_8 save(const std::string& _root, const std::list<std::string>& _path, const file_info& _dir) const;

            /**
             * \brief Abort extraction on an external flag
             *
             * The flag is read before every block, so raising it costs at most one block. It must
             * outlive the calls that may read it; null disables the check.
             *
             * \param _abort_flag Caller-owned flag; extraction stops once *flag is true
             */
            inline void set_abort_flag(const bool* _abort_flag) { abort_ = _abort_flag; }
            /// The stream the constructor was given
            inline const istream* get_stream() const { return istm_; }
            /// Whether this archive is marked as encrypted, read again from its header
            inline bool is_encrypt() const { return is_encrypt(*istm_); }
            /// Whether this archive is marked as compressed, read again from its header
            inline bool is_compress() const { return is_compress(*istm_); }
            /// Install the progress callback; null, the default, reports nothing
            inline void set_print_rtmsg(MesgFunc _fc) { print_rtmsg_ = _fc; }

        public:
            /**
             * \brief Text for a result code
             *
             * \return A static string, never null; success gives "success", every unrecognized
             *         code gives "unknown"
             */
            static const char* error_message(uint_8 _err);

        private:
            bool parse_head();
            bool parse_tail();
            static uint_8 get_impl(const decoder* _this, const variant& _ctrl, bytes& _odat);
            static uint_8 get_impl(const decoder* _this, const variant& _ctrl, ostream& _ostm);
            static uint_8 save_impl(const decoder* _this, const variant& _fsys, const file_info& _dir, const std::string& _key);
            static uint_8 save_impl_dir(const decoder* _this, const variant& _fsys, const file_info& _dir, const std::string& _key);
            static uint_8 save_impl_file(const decoder* _this, const variant& _ctrl, const file_info& _dir, const std::string& _key);

        private:
            compress::decoder_lz4* cmps_{nullptr};
            istream* istm_;
            varmap fsys_;
            bytes pswd_;

            const bool* abort_{nullptr};
            MesgFunc print_rtmsg_;
        };
    }
}

#endif