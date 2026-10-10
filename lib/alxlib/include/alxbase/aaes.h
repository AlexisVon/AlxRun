/*****************************************************************/ /**
 * \file   aaes.h
 * \brief  AES encryption and decryption algorithm
 *
 * \author alexis
 *
 * Copyright (c) 2026 AlexisVon
 * SPDX-License-Identifier: MIT
 *********************************************************************/

#ifndef _ALEXIS_AES_H_
#define _ALEXIS_AES_H_

/// Compile-time switch for the hardware paths: when off, hardcal() always answers false, every
/// _hard entry point compiles away as an empty body, and round_key loses its 16-byte alignment
#define TRY_AES_HARD true

#include "abytes.h"

namespace alx {
    /**
     * \brief AES round core: key expansion, the round functions, GHASH and the padding helpers
     *
     * Stateless: every member is a pure function over caller-owned 16-byte blocks, so the class
     * holds no data of its own and its members exist for the mode classes below to build on. The
     * _hard and _soft variants of a primitive are picked at runtime by hardcal(); with
     * TRY_AES_HARD off the probe can only answer false and only the software code is called.
     */
    class ALXBASE_API aes_base : public noncopyable {
    public:
        /// Side of the 4x4 state matrix; a round key is block_num 4-byte words long
        static constexpr uint_8 block_num{4};
        /// Bytes in one block
        static constexpr uint_8 block_len{16};
        /// block_len - 1: masks the offset inside a block out of a byte count
        static constexpr uint_8 block_len_msk{15};
        /// log2(block_len): shifts a byte count into a block count
        static constexpr uint_8 block_len_shift{4};
        /// Bits in one block, the unit GHASH measures its inputs in
        static constexpr uint_8 block_bits_len{block_len * 8};

        /**
         * \brief Padding schemes understood by buf_padding() and buf_unpadding()
         *
         * NONE (0) means "leave the buffer alone": the caller aligns the data itself. Every other
         * mode pads up to the next 16-byte boundary, and when the buffer is already aligned
         * PKCS7, ANSIX923 and ISO10126 append a whole extra block while ZEROS appends nothing.
         * PKCS7 is the default argument of both helpers.
         */
        enum PADDING { NONE = 0,
                       /// Each pad byte holds the pad length, so the length read back is 1..16
                       PKCS7,
                       /// Zero fill, and an aligned buffer is not grown
                       ZEROS,
                       /// Zero fill with the pad length in the last byte
                       ANSIX923,
                       /// 0xff fill with the pad length in the last byte; the standard asks for
                       /// random filler, which no decoder reads back, so this is equivalent
                       ISO10126 };

    protected:
        /// For a class deriving from aes<>: true when this machine runs the AES-NI round
        /// functions; the CPU probe decides, and the answer is false whenever TRY_AES_HARD is off
        static bool hardcal();
        /// Encrypt one 16-byte block in place with the expanded _round_key, over _round_num rounds
        static void cipher(uint_8* _state, const uint_8* _round_key, const uint_8 _round_num);
        /// Inverse of cipher(): decrypt one 16-byte block in place with the expanded _round_key
        static void inv_cipher(uint_8* _state, const uint_8* _round_key, const uint_8 _round_num);
        /// XOR one 16-byte block with a 16-byte _iv, in place: the CBC chaining step
        static void xor_with_iv(uint_8* _state, const uint_8* _iv);
        /**
         * \brief Expand a key into its round keys
         *
         * _round_key must hold 16 * (_round_num + 1) bytes, which is the key_exp_size of the
         * matching aes<> instantiation; _key_num is the key length in 32-bit words: 4, 6 or 8.
         */
        static void key_expansion(uint_8* _round_key, const uint_8* _key, const uint_8 _key_num, const uint_8 _round_num);
        /**
         * \brief Grow a buffer to a whole number of blocks by appending padding
         *
         * Resizes _buf in place: the caller pads before encrypt() and strips after decrypt().
         * NONE and an unrecognized _mode change nothing; for ZEROS an aligned buffer is left as
         * it is, while every other mode appends a whole block in that case.
         */
        static void buf_padding(bytes& _buf, PADDING _mode = PADDING::PKCS7);
        /**
         * \brief Cut the padding block off the tail of a buffer
         *
         * No-op when _buf is empty or its size is not a multiple of 16. PKCS7 and ANSIX923 check
         * their filler before cutting, ISO10126 cannot (its filler is arbitrary), and ZEROS drops
         * up to 15 trailing zero bytes that may be data of their own.
         */
        static void buf_unpadding(bytes& _buf, PADDING _mode = PADDING::PKCS7);

    private:
        static void cipher_impl_soft(uint_8* _state, const uint_8* _round_key, const uint_8 _round_num);
        static void cipher_impl_hard(uint_8* _state, const uint_8* _round_key, const uint_8 _round_num);
        static void inv_cipher_impl_soft(uint_8* _state, const uint_8* _round_key, const uint_8 _round_num);
        static void inv_cipher_impl_hard(uint_8* _state, const uint_8* _round_key, const uint_8 _round_num);
        static void add_round_key(uint_8* _state, uint_8 _round, const uint_8* _key);
        static void sub_bytes(uint_8* _state);
        static void inv_sub_bytes(uint_8* _state);
        static void shift_rows(uint_8* _state);
        static void inv_shift_rows(uint_8* _state);
        static void mix_columns(uint_8* _state);
        static void inv_mix_columns(uint_8* _state);
        static inline uint_8 xtime(uint_8 _x);
        static inline uint_8 multiply(uint_8 _x, uint_8 _y);

    protected:
        /// One GHASH step: _s ^= _d, then _s is multiplied by the subkey _h in GF(2^128)
        static void ghash(uint_8* _s, const uint_8* _h, const uint_8* _d);
        /// Add 1 to a 16-byte counter, big-endian: the carry walks from the last byte backwards
        static void increment_counter(uint_8* _counter);

    public:
        /// One GHASH step in portable code; _s is the running state, _h the subkey, _d the block
        static void ghash_soft(uint_8* _s, const uint_8* _h, const uint_8* _d);
        /// Multiply _a by _b in GF(2^128); the product replaces _a
        static void ghash_mul_soft(uint_8* _a, const uint_8* _b);

    private:
        /// The hardware-path GHASH step; same contract as ghash_soft(), and the same assumption
        /// as the cipher_impl_hard pair: the CPU runs PCLMULQDQ, which only ghash() checks
        static void ghash_hard(uint_8* _s, const uint_8* _h, const uint_8* _d);
        /// The PCLMULQDQ multiply; same contract as ghash_mul_soft(), and it maps both operands
        /// into the domain the intrinsic works in on the way through
        static void ghash_mul_hard(uint_8* _a, const uint_8* _b);
    };

    /**
     * \brief AES core for one key size, holding the expanded key
     *
     * AES_SIZE is the key size in bits and is fixed at compile time: 128, 192 or 256, anything
     * else fails the static_assert. The expanded key lives in the object -- no heap, no lifetime
     * tie to the caller's array, which is only read in the constructor. The class is abstract:
     * encrypt(), decrypt() and reset() come from the mode built on top of it.
     */
    template <uint_64 AES_SIZE>
    class aes : public aes_base {
    public:
        static_assert(AES_SIZE == 128LLU || AES_SIZE == 192LLU || AES_SIZE == 256LLU, "unsupported AES_SIZE");
        /// Key length in 32-bit words: 4, 6 or 8
        static constexpr uint_8 key_num{alx::cond_value(AES_SIZE == 128LLU, 4U, alx::cond_value(AES_SIZE == 192LLU, 6U, 8U))};
        /// Rounds per block: 10, 12 or 14
        static constexpr uint_8 round_num{alx::cond_value(AES_SIZE == 128LLU, 10U, alx::cond_value(AES_SIZE == 192LLU, 12U, 14U))};
        /// Bytes of expanded key: 16 * (round_num + 1), i.e. 176, 208 or 240
        static constexpr uint_64 key_exp_size{alx::cond_value(AES_SIZE == 128LLU, 176LLU, alx::cond_value(AES_SIZE == 192LLU, 208LLU, 240LLU))};
        /// Key size in bytes: 16, 24 or 32
        static constexpr uint_64 key_len{key_num << 2};
        /// Both padding helpers are public here, so the caller can pad and strip on its own
        using aes_base::buf_padding;
        using aes_base::buf_unpadding;

    public:
        /**
         * \brief Expand _key into the round keys this object runs on
         *
         * \param _key Key to use, read for exactly key_len bytes and not kept
         */
        aes(const uint_8 _key[key_len]) { aes_base::key_expansion(round_key, _key, key_num, round_num); }
        /// Nothing to release: the expanded key is a member array
        ~aes() {}

    public:
        /// Encrypt _buf in place; shorthand for encrypt(_buf.data(), _buf.size())
        inline bool encrypt(bytes& _buf) { return encrypt(_buf.data(), _buf.size()); }
        /// Decrypt _buf in place; shorthand for decrypt(_buf.data(), _buf.size())
        inline bool decrypt(bytes& _buf) { return decrypt(_buf.data(), _buf.size()); }
        /**
         * \brief Encrypt a buffer in place
         *
         * \param _buf Buffer to transform; untouched when the call returns false
         * \param _len Byte count; each mode accepts what its construction allows -- the block
         *             modes reject 0 and anything that is not a multiple of 16, CTR and GCM take
         *             any length
         * \return false when _len is not usable for this mode
         */
        virtual bool encrypt(uint_8* _buf, uint_64 _len) = 0;
        /// Inverse of encrypt(); accepts exactly the same lengths
        virtual bool decrypt(uint_8* _buf, uint_64 _len) = 0;
        /**
         * \brief Return the mode to its freshly constructed state, ready for another message
         *
         * Restores whatever the mode carries between calls: the IV (CBC, CTR), the counter and the
         * tag state (GCM). ECB has nothing to restore and does nothing.
         */
        virtual void reset() = 0;

    protected:
        /// Encrypt one 16-byte block with this object's round key
        inline void cipher(uint_8* _state) { aes_base::cipher(_state, round_key, round_num); }
        /// Inverse of cipher(): decrypt one 16-byte block with this object's round key
        inline void inv_cipher(uint_8* _state) { aes_base::inv_cipher(_state, round_key, round_num); }

    protected:
#if TRY_AES_HARD
        /// Expanded round keys, key_exp_size bytes, aligned because the AES-NI path loads them
        /// 16 bytes at a time
        alignas(16) uint_8 round_key[key_exp_size];
#else
        uint_8 round_key[key_exp_size];
#endif
    };

    /**
     * \brief ECB mode: every block is enciphered on its own, with no feedback
     *
     * Equal plaintext blocks give equal ciphertext blocks, so the mode leaks structure; the
     * other modes exist because of that. The object is otherwise stateless.
     */
    template <uint_64 AES_SIZE>
    class aes_ecb : public aes<AES_SIZE> {
    public:
        /// aes<AES_SIZE>: the core this mode builds on
        typedef aes<AES_SIZE> base;
        using base::aes;
        using base::decrypt;
        using base::encrypt;
        /**
         * \brief Encrypt every block of a buffer in place
         *
         * \param _len Byte count; a non-zero multiple of 16 is required, anything else leaves the
         *             buffer untouched
         * \return false when _len is 0 or not a whole number of blocks
         */
        virtual bool encrypt(uint_8* _buf, uint_64 _len) override {
            if (0 == _len || _len & base::block_len_msk) return false;

            uint_64 end = _len - base::block_len;
            for (uint_64 i = 0; i <= end; i += base::block_len)
                base::cipher(_buf + i);
            return true;
        }
        /// Inverse of encrypt(): in place, and under the same _len requirement
        virtual bool decrypt(uint_8* _buf, uint_64 _len) override {
            if (0 == _len || _len & base::block_len_msk) return false;

            uint_64 end = _len - base::block_len;
            for (uint_64 i = 0; i <= end; i += base::block_len)
                base::inv_cipher(_buf + i);
            return true;
        }
        /// No-op: ECB carries nothing from one block or one call to the next
        virtual void reset() override {}
    };

    /**
     * \brief AES core plus an IV, with a backup of it for reset()
     *
     * The modes below use the 16 bytes both as chaining value and as counter block, updating them
     * as data flows; the backup is never touched again after construction, which is what makes
     * reset() a real rewind. Still abstract -- encrypt()/decrypt() belong to the mode.
     */
    template <uint_64 AES_SIZE>
    class aes_iv : public aes<AES_SIZE> {
    public:
        /// aes<AES_SIZE>: the core this mode builds on
        typedef aes<AES_SIZE> base;
        /**
         * \brief Take the key and the starting IV
         *
         * \param _iv IV, exactly 16 bytes: the values this object starts every message from. It
         *            is copied, so the caller's array can go away
         */
        aes_iv(const uint_8 _key[base::key_len], const uint_8 _iv[base::block_len])
            : base(_key) {
            memcpy(iv, _iv, base::block_len);
            memcpy(iv_backup, _iv, base::block_len);
        }
        /// Put iv back to the constructor's value
        virtual void reset() override { memcpy(iv, iv_backup, base::block_len); }

    protected:
        /// Working IV: the previous ciphertext block (CBC) or the counter (CTR), 16 bytes
        uint_8 iv[base::block_len];
        /// The constructor's IV, restored by reset() and never written after construction
        uint_8 iv_backup[base::block_len];
    };

    /**
     * \brief CBC mode: every block is XORed with the previous ciphertext block before encryption
     *
     * The chain lives in the base's iv, so consecutive calls continue one another and the IV when
     * a call returns is the last ciphertext block, in either direction. reset() is therefore
     * needed before a new message on the same object, and before switching from encrypting to
     * decrypting with the same key.
     */
    template <uint_64 AES_SIZE>
    class aes_cbc : public aes_iv<AES_SIZE> {
    private:
        typedef aes_iv<AES_SIZE> base;
        using base::iv;
        using base::iv_backup;

    public:
        using base::aes_iv;
        using base::decrypt;
        using base::encrypt;
        /**
         * \brief Encrypt every block of a buffer in place, chaining as it goes
         *
         * \param _len Byte count; a non-zero multiple of 16 is required, anything else leaves the
         *             buffer and the IV untouched
         * \return false when _len is 0 or not a whole number of blocks
         */
        virtual bool encrypt(uint_8* _buf, uint_64 _len) override {
            if (0 == _len || _len & base::block_len_msk) return false;

            uint_8* ptr = iv;
            uint_64 end = _len - base::block_len;
            for (uint_64 i = 0; i <= end; i += base::block_len) {
                base::xor_with_iv(_buf, ptr);
                base::cipher(_buf);
                ptr = _buf;
                _buf += base::block_len;
            }
            memcpy(iv, ptr, base::block_len);
            return true;
        }
        /// Inverse of encrypt(): in place, under the same _len requirement, and it also leaves
        /// the IV on the last ciphertext block seen
        virtual bool decrypt(uint_8* _buf, uint_64 _len) override {
            if (0 == _len || _len & base::block_len_msk) return false;

            uint_8 ptr[base::block_len];
            uint_64 end = _len - base::block_len;
            for (uint_64 i = 0; i <= end; i += base::block_len) {
                memcpy(ptr, _buf, base::block_len);
                base::inv_cipher(_buf);
                base::xor_with_iv(_buf, iv);
                memcpy(iv, ptr, base::block_len);
                _buf += base::block_len;
            }
            return true;
        }
    };

    /**
     * \brief CTR mode: AES of a counter block, XORed with the data
     *
     * A stream cipher, so encrypt() and decrypt() are the same operation and _len is
     * unconstrained -- block aligned or not. The position inside the current key-stream block is
     * kept in the object, so a message may be fed in pieces and the stream simply continues;
     * reset() rewinds the counter to the constructor's IV.
     */
    template <uint_64 AES_SIZE>
    class aes_ctr : public aes_iv<AES_SIZE> {
    private:
        typedef aes_iv<AES_SIZE> base;
        using base::iv;
        using base::iv_backup;

    public:
        using base::aes_iv;
        using base::decrypt;
        using base::encrypt;
        /// XOR _len bytes with the key stream; the primitive that encrypt() and decrypt() call
        inline void xcrypt(uint_8* _buf, uint_64 _len) {
            for (uint_64 i = 0; i < _len; ++i, ++iv_index) {
                if (iv_index == base::block_len) {
                    memcpy(iv_buffer, iv, base::block_len);
                    base::cipher(iv_buffer);
                    for (; iv_index != 0; --iv_index) {
                        if (iv[iv_index - 1] == 255) {
                            iv[iv_index - 1] = 0;
                            continue;
                        }
                        iv[iv_index - 1] += 1;
                        break;
                    }
                    iv_index = 0;
                }
                _buf[i] = (_buf[i] ^ iv_buffer[iv_index]);
            }
        }
        /// Same as xcrypt(), and always true: CTR takes any _len, including 0
        virtual bool encrypt(uint_8* _buf, uint_64 _len) override {
            xcrypt(_buf, _len);
            return true;
        }
        /// Identical to encrypt(): the key stream is symmetric, so one pass undoes the other
        virtual bool decrypt(uint_8* _buf, uint_64 _len) override {
            xcrypt(_buf, _len);
            return true;
        }
        /// Rewind the counter to the constructor's IV and drop the buffered key-stream block, so
        /// the next byte starts where a freshly constructed object would
        virtual void reset() override {
            base::reset();
            iv_index = base::block_len;
        }

    private:
        uint_8 iv_index{base::block_len};
        uint_8 iv_buffer[base::block_len];
    };

    /**
     * \brief GCM authenticated encryption: a counter key stream plus a GHASH tag
     *
     * The core is inherited protected, so the raw block functions stay out of the interface and
     * only encrypt()/decrypt() are re-exposed. The IV is fixed at 12 bytes: J0 is the IV with
     * 0x00000001 appended. The object is driven start(aad) -> encrypt()/decrypt() -> finish(tag),
     * and finish() resets it for the next message.
     */
    template <uint_64 AES_SIZE>
    class aes_gcm : protected aes<AES_SIZE> {
    private:
        typedef aes<AES_SIZE> base;
        static constexpr uint_8 iv_len{12};
        void process_block(uint_8 _block[base::block_len]) {
            uint_8 counter[base::block_len];
            memcpy(counter, gcm_cnt, base::block_len);
            base::cipher(counter);
            for (int i = 0; i < base::block_len; ++i) _block[i] ^= counter[i];
        }

    public:
        using base::decrypt;
        using base::encrypt;
        /**
         * \brief Take the key and the IV
         *
         * The IV is copied, and the GHASH subkey H, J0 and the counter are derived right here, so
         * the object is ready for start().
         *
         * \param _iv IV, exactly 12 bytes; a longer or shorter one is not supported
         */
        aes_gcm(const uint_8 _key[base::key_len], const uint_8 _iv[iv_len])
            : base(_key) {
            base::cipher(gcm_h);
            memcpy(gcm_j0, _iv, iv_len);
            gcm_j0[15] = 0X01;
            memcpy(gcm_y0, gcm_j0, base::block_len);
            memcpy(gcm_cnt, gcm_y0, base::block_len);
        }
        /// Start a message with a bytes_view of AAD; see the pointer overload for the contract
        bool start(const bytes_view& _aad) { return start(_aad.data(), _aad.size()); }
        /**
         * \brief Start a message, feeding the additional authenticated data
         *
         * Required once per message, before finish(); the AAD may be empty (nullptr, 0) but the
         * call itself is not optional, and it carries the whole AAD at once -- the length that
         * goes into the tag is the one given here.
         *
         * \return false when start() was already called for this message; the object is then
         *         untouched, and finish() or reset() has to come first
         */
        bool start(const uint_8* _aad_data, uint_64 _aad_len) {
            if (max_uint_64 != gcm_aad_len) return false;
            uint_64 remaining = _aad_len;
            const uint_8* ptr = _aad_data;

            while (remaining > 0) {
                uint_8 block[base::block_len]{0};
                uint_64 copy_size = (remaining >= base::block_len) ? base::block_len : remaining;
                memcpy(block, ptr, copy_size);
                aes_base::ghash(gcm_s, gcm_h, block);
                ptr += copy_size;
                remaining -= copy_size;
            }
            gcm_aad_len = _aad_len;
            return true;
        }
        /// Write the tag into _auth_tag, resized to 16 bytes; see the pointer overload
        bool finish(bytes& _auth_tag) { return _auth_tag.resize(base::block_len), finish(_auth_tag.data()); }
        /**
         * \brief Close the message and write its 16-byte authentication tag
         *
         * The tag covers the AAD plus every byte fed through encrypt()/decrypt() since start().
         * Nothing is checked here: after a decrypt the caller must compare this tag with the one
         * the sender produced, and throw the plaintext away when they differ. The object resets
         * itself on the way out, so it is ready for another message.
         *
         * \return false when the message was not started -- start() never called, or reset()
         *         called since; _auth_tag is then untouched
         */
        bool finish(uint_8 _auth_tag[base::block_len]) {
            if (max_uint_64 == gcm_aad_len) return false;
            uint_8 len_block[base::block_len]{0};
            uint_64 aad_bits = gcm_aad_len << 3;
            uint_64 ct_bits = gcm_proc_len << 3;
            for (int i = 0; i < 8; ++i) len_block[i] = (aad_bits >> ((7 - i) << 3)) & 0XFFU;
            for (int i = 0; i < 8; ++i) len_block[i + 8] = (ct_bits >> ((7 - i) << 3)) & 0XFFU;
            aes_base::ghash(gcm_s, gcm_h, len_block);
            uint_8 J0_encrypted[base::block_len];
            memcpy(J0_encrypted, gcm_j0, base::block_len);
            base::cipher(J0_encrypted);
            for (int i = 0; i < base::block_len; ++i) gcm_s[i] ^= J0_encrypted[i];
            memcpy(_auth_tag, gcm_s, base::block_len);
            return reset(), true;
        }
        /**
         * \brief Encrypt in place, folding the ciphertext into the tag
         *
         * Any _len is accepted and the output has the same length as the input; the counter and
         * the tag state carry over, so a message may be fed in several pieces.
         */
        virtual bool encrypt(uint_8* _buf, uint_64 _len) override {
            const uint_64 block_num = _len >> base::block_len_shift;
            const uint_64 block_rem = _len & base::block_len_msk;

            for (uint_64 i = 0; i < block_num; i++) {
                base::increment_counter(gcm_cnt);
                uint_8* current_block = _buf + (i * base::block_len);
                process_block(current_block);
                aes_base::ghash(gcm_s, gcm_h, current_block);
                gcm_proc_len += base::block_len;
            }
            if (block_rem != 0) {
                base::increment_counter(gcm_cnt);
                uint_8 temp_block[base::block_len]{0};
                memcpy(temp_block, _buf + _len - block_rem, block_rem);
                process_block(temp_block);
                memcpy(_buf + _len - block_rem, temp_block, block_rem);
                memset(temp_block + block_rem, 0, base::block_len - block_rem);
                aes_base::ghash(gcm_s, gcm_h, temp_block);
                gcm_proc_len += block_rem;
            }

            return true;
        }
        /// Inverse of encrypt(), with the same free _len; the tag is computed but not checked
        virtual bool decrypt(uint_8* _buf, uint_64 _len) override {
            const uint_64 block_num = _len >> base::block_len_shift;
            const uint_64 block_rem = _len & base::block_len_msk;

            for (uint_64 i = 0; i < block_num; i++) {
                base::increment_counter(gcm_cnt);
                uint_8* current_block = _buf + (i * base::block_len);
                aes_base::ghash(gcm_s, gcm_h, current_block);
                process_block(current_block);
                gcm_proc_len += base::block_len;
            }
            if (block_rem != 0) {
                base::increment_counter(gcm_cnt);
                uint_8 temp_block[base::block_len]{0};
                memcpy(temp_block, _buf + _len - block_rem, block_rem);
                aes_base::ghash(gcm_s, gcm_h, temp_block);
                process_block(temp_block);
                memcpy(_buf + _len - block_rem, temp_block, block_rem);
                gcm_proc_len += block_rem;
            }

            return true;
        }
        /// Back to a freshly constructed object: the counter from J0, the tag state zeroed and
        /// both lengths cleared -- finish() calls this on its way out
        virtual void reset() override {
            memcpy(gcm_cnt, gcm_y0, base::block_len);
            memset(gcm_s, 0, base::block_len);
            gcm_aad_len = max_uint_64;
            gcm_proc_len = 0;
        }

    private:
#if TRY_AES_HARD
        alignas(16) uint_8 gcm_h[base::block_len]{0};
        alignas(16) uint_8 gcm_j0[base::block_len]{0};
        alignas(16) uint_8 gcm_y0[base::block_len]{0};
        alignas(16) uint_8 gcm_cnt[base::block_len]{0};
        alignas(16) uint_8 gcm_s[base::block_len]{0};
#else
        uint_8 gcm_h[base::block_len]{0};
        uint_8 gcm_j0[base::block_len]{0};
        uint_8 gcm_y0[base::block_len]{0};
        uint_8 gcm_cnt[base::block_len]{0};
        uint_8 gcm_s[base::block_len]{0};
#endif
        uint_64 gcm_aad_len{max_uint_64};
        uint_64 gcm_proc_len{0};
    };
}

#endif
