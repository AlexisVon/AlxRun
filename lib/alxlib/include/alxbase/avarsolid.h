/*****************************************************************/ /**
 * \file   avarsolid.h
 * \brief  High-level API: varmap ↔ binary bytes conversion and path-based extraction
 *
 * \author alexis
 *
 * Copyright (c) 2026 AlexisVon
 * SPDX-License-Identifier: MIT
 *********************************************************************/

#ifndef _ALEXIS_VARSOLID_H_
#define _ALEXIS_VARSOLID_H_

#include "astream.h"
#include "avariant.h"

namespace alx {
    namespace varsolid {

        /**
         * \brief Serialize a varmap into a binary blob, replacing what _buff held
         *
         * _buff is resized to the exact blob length. An empty varmap is not a failure: it
         * serializes to a header-only blob. On failure _buff holds a partly written blob and must
         * be discarded.
         *
         * \param _buff Blob to overwrite
         * \param _err_obj Out; *_err_obj is set to the top-level entry whose value could not be
         *                 written, which _vmap keeps owning. Untouched on success, and not written
         *                 when the failure belongs to no single entry
         * \return false when a value cannot be carried by the format: a key longer than 65535
         *         bytes, a value of a type the format has no case for (anyptr, a container of a
         *         compound), or a total beyond the format caps
         */
        ALXBASE_API bool to_bytes(const varmap& _vmap, bytes& _buff, variant** _err_obj = nullptr);

        /**
         * \brief Serialize a varmap onto the end of an output stream
         *
         * The blob is appended at the stream's current position, and the stream is not flushed --
         * a buffered sink is flushed by the caller. A failure leaves the partly appended blob in
         * the stream; there is no rollback. The failure conditions are those of the bytes
         * overload.
         *
         * \param _ostm Stream to append the blob to
         * \param _err_obj Out; as in the bytes overload
         * \return false also when the stream did not take exactly the bytes the blob needs
         */
        ALXBASE_API bool to_bytes(const varmap& _vmap, ostream& _ostm, variant** _err_obj = nullptr);

        /**
         * \brief Serialize a varmap and return the blob, without a caller-supplied buffer
         *
         * \param _err_obj Forwarded to the bytes overload
         * \return An empty blob means failure; an empty varmap is not that -- it serializes to a
         *         header-only blob
         */
        inline bytes to_bytes(const varmap& _vmap, variant** _err_obj = nullptr) {
            bytes buff;
            return to_bytes(_vmap, buff, _err_obj) ? buff : bytes();
        }

        /**
         * \brief Decode a blob into a varmap, without an error signal
         *
         * An empty result is always a refused decode: a blob the parser accepts yields at least
         * one entry. Safe to call from any thread; the parser tables are built on first use.
         * _bytes is read only for the duration of the call.
         *
         * \param _bytes Blob produced by to_bytes()
         */
        ALXBASE_API varmap to_varmap(const bytes_view& _bytes);

        /**
         * \brief Decode a blob into a varmap, reporting the failure
         *
         * \param _bytes Blob produced by to_bytes()
         * \param _vmap Replaced only on success; left as it was when the blob is refused
         * \return false when the blob is not one this library wrote -- wrong magic, truncated, or
         *         a corrupt entry header
         */
        ALXBASE_API bool to_varmap(const bytes_view& _bytes, varmap& _vmap);

        /**
         * \brief Tell whether a buffer is a blob to_bytes() could have produced
         *
         * Only the outer header and tail are checked -- nothing is decoded and no entry is walked
         * -- so true does not promise that to_varmap() succeeds. Bytes past the end of the block
         * are ignored. A block holding no entry, which is what an empty varmap serializes to, is
         * refused.
         *
         * \param _bytes Buffer to inspect
         */
        ALXBASE_API bool is_valid(const bytes_view& _bytes);

        /**
         * \brief Extract one value from a blob by path, without decoding all of it
         *
         * _path is walked from the root inwards, one element per level of nesting: at a map an
         * element is a key, at a varvec or a varlst it is a decimal index. A typed container
         * (std::vector<int>, say) is entered by neither -- every element past it misses. An empty
         * _path returns the whole document as a variant holding a varmap, not _def.
         *
         * \param _bytes Blob produced by to_bytes()
         * \param _path Keys and indexes, outermost first
         * \param _def Returned when the blob is not valid, or when a step misses: no such key, an
         *             index out of range, a non-decimal element where an index is required, any
         *             element past a node that has no children. Defaults to a null variant
         * \return A copy of the value found
         */
        ALXBASE_API variant get_value(const bytes_view& _bytes, const std::list<std::string>& _path, const variant& _def = variant::def_val());
    }
}

#endif