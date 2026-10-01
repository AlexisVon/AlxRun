/*****************************************************************/ /**
 * \file   averify.h
 * \brief  Data validation and verification
 *
 * \author alexis
 *
 * Copyright (c) 2026 AlexisVon
 * SPDX-License-Identifier: MIT
 *********************************************************************/

#ifndef _ALEXIS_VERIFY_H_
#define _ALEXIS_VERIFY_H_

#define TRY_SHA_HARD true
#define TRY_CRC_HARD true

#include "abytes.h"

namespace alx {
    /**
     * \brief Digest and checksum interface: one instance is one computation
     *
     * Feed the message with update() in as many calls as convenient, then take the value with
     * hexdigest() or bytedigest(). Instances come from create(), which hands ownership over, or
     * exec() does the whole job for a single buffer. The value depends only on the bytes fed and
     * their order -- never on whether the CPU's accelerated path was taken -- so a digest written
     * by one build is reproduced by another. An instance is not synchronized: one per thread.
     */
    class ALXCORE_API verify : public noncopyable {
    public:

        /**
         * \brief Fixed ids for the enum overloads of create() and exec(); 0 means no algorithm
         *
         * NO_CHECK is that 0: create() returns null for it, as it does for any value not listed
         * here. The two CRCs are fixed by the code, one line each:
         * - CRC_32 : poly 0x04C11DB7, reflected in and out, init 0xFFFFFFFF, xorout 0xFFFFFFFF
         * - CRC_32C: poly 0x1EDC6F41, reflected in and out, init 0xFFFFFFFF, xorout 0xFFFFFFFF
         * The hashes are SHA-1 and SHA-256 as specified, with no parameter to set.
         */
        enum COMMON_TYPE : uint_8 { NO_CHECK = 0x00,
                                    /// CRC-32/ISO-HDLC, the zlib and gzip CRC
                                    CRC_32 = 0x0F,
                                    /// CRC-32C (Castagnoli); the only CRC with an accelerated path
                                    CRC_32C = 0xF0,
                                    /// SHA-1; 20-byte digest
                                    SHA_1 = 0x33,
                                    /// SHA-256; 32-byte digest
                                    SHA_256 = 0xCC };

        /**
         * \brief Every name the string overloads of create() and exec() accept
         *
         * The names are the product class names, e.g. "sha_256" or "crc_32_c", and cover all the
         * algorithms compiled in -- more than the COMMON_TYPE enumerators. The order is
         * unspecified.
         */
        static std::vector<std::string> list();

        /**
         * \brief Instantiate one of the common algorithms
         *
         * \param _type Which algorithm to instantiate
         * \return New instance owned by the caller, to be deleted; null when _type names no
         *         algorithm -- NO_CHECK, or a value outside COMMON_TYPE
         */
        static verify* create(const COMMON_TYPE _type);
        /// Instance owned by the caller; null when _type is not one of list()
        static verify* create(const std::string& _type);

        /**
         * \brief Digest a whole buffer in one call
         *
         * Equivalent to create() + update() + hexdigest(), with the instance deleted before the
         * call returns.
         *
         * \param _data First byte of the message
         * \param _len Number of bytes to read; the buffer is not retained
         * \return Lowercase hex digest, or an empty string when _type names no algorithm
         */
        static std::string exec(const COMMON_TYPE _type, const uint_8* _data, uint_64 _len);
        /// Digest a byte buffer; empty when _type names no algorithm
        inline static std::string exec(const COMMON_TYPE _type, const bytes_view& _data) { return exec(_type, _data.data(), _data.size()); }
        /// Digest a string's bytes; its terminating null is not part of the message
        inline static std::string exec(const COMMON_TYPE _type, const std::string& _data) { return exec(_type, (const uint_8*) (_data.data()), _data.length()); }

        /**
         * \brief Digest a whole buffer in one call, algorithm chosen by name
         *
         * \param _type One of list()
         * \return Lowercase hex digest, or an empty string when _type is not registered
         */
        static std::string exec(const std::string& _type, const uint_8* _data, uint_64 _len);
        /// Digest a byte buffer; empty when _type is not registered
        inline static std::string exec(const std::string& _type, const bytes_view& _data) { return exec(_type, _data.data(), _data.size()); }
        /// Digest a string's bytes; its terminating null is not part of the message
        inline static std::string exec(const std::string& _type, const std::string& _data) { return exec(_type, (const uint_8*) (_data.data()), _data.length()); }

        /// Feed a byte buffer; the bytes are read during the call
        inline void update(const bytes_view& _data) { return update(_data.data(), _data.size()); }
        /// Feed a string's bytes; its terminating null is not part of the message
        inline void update(const std::string& _data) { return update((const uint_8*) (_data.data()), _data.length()); }
        /// Virtual, so the pointer create() returned is enough to delete a product
        virtual ~verify() = default;

        /**
         * \brief Feed the next bytes of the message
         *
         * Call it as often as convenient before the digest is taken: the value depends on the
         * concatenation, not on where it was split. The bytes are read during the call and the
         * buffer is not retained.
         *
         * \param _data First byte to read
         * \param _len Number of bytes to read
         */
        virtual void update(const uint_8* _data, uint_64 _len) = 0;

        /**
         * \brief Finalize and return the digest as lowercase hex
         *
         * The hashes finalize here -- the call feeds the length and the padding into the state --
         * so a hash digest is taken once: clear() before feeding the next message. A CRC has
         * nothing to finalize, so its digest is repeatable and update() may continue after it.
         *
         * \return Two digits per byte, zero padded to the algorithm's width: 40 or 64 characters
         *         for the hashes, 2 * sizeof(CRC) for the CRCs
         */
        virtual std::string hexdigest() = 0;

        /**
         * \brief The digest as raw bytes rather than as hex
         *
         * The same value as hexdigest(), each digest word in the machine's byte order, so the
         * byte string is only comparable with a peer of the same endianness: cross a machine or
         * process boundary with hexdigest(). Finalizes a hash exactly as hexdigest() does.
         *
         * \return 20 or 32 bytes for the hashes, sizeof(CRC) for the CRCs
         */
        virtual bytes bytedigest() = 0;

        /**
         * \brief Whether this instance can take the CPU's accelerated path
         *
         * True only where the build allows it (TRY_SHA_HARD / TRY_CRC_HARD) and the CPU has the
         * instructions: SHA-NI for sha_1 and sha_256, SSE4.2 CRC32 for crc_32_c. Every other
         * variant is table-driven and always reports false. The digest is the same on either
         * path, so this reports speed only; the acomm_ex packer emits its wire checksum only when
         * it is true.
         */
        virtual bool hardcal() = 0;

        /// Back to the initial state, ready for the next message
        virtual void clear() = 0;
    };
}

#endif