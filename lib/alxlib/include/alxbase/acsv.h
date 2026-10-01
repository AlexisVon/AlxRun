/*****************************************************************/ /**
 * \file   acsv.h
 * \brief  CSV text matrix (row 0 is the header row)
 *
 * \author alexis
 *
 * Copyright (c) 2026 AlexisVon
 * SPDX-License-Identifier: MIT
 *********************************************************************/

#ifndef _ALEXIS_CSV_H_
#define _ALEXIS_CSV_H_

#include "abytes.h"
#include "astream.h"
#include "avariant.h"

#include <string>

namespace alx {
    namespace csv {

        /**
         * \brief Parse CSV text into a matrix of rows and cells
         *
         * RFC 4180 quoting: a field opened with '"' may hold the delimiter and line breaks, and
         * "" inside it is one literal quote. CRLF, LF and CR all end a row, the last row may omit
         * its break, a blank line is not a record, and nothing is trimmed. The whole parse fails
         * -- empty matrix, *_ok = false -- on an unclosed quote, on junk behind a closing quote,
         * on a quote inside a field that was not opened with one, or on a delimiter outside the
         * range below.
         *
         * \param _data CSV text, valid for _size bytes; null fails. UTF-8 is read as-is, a UTF-8
         *              BOM is stripped -- so a first cell that itself begins with U+FEFF loses it
         *              too -- UTF-16 is converted as a whole and any other detected encoding cell
         *              by cell, a cell the conversion cannot decode failing the whole parse
         * \param _size Text length in bytes; 0 is an empty matrix, not a failure, and so is a
         *              lone BOM
         * \param _delim Field delimiter: an ASCII byte other than '"', CR, LF and NUL
         * \param _strict true: every record must carry as many fields as the first one held, else
         *                the parse fails; false: each row keeps its own field count, so the
         *                matrix may be ragged
         * \param _ok Success flag, written only when non-null; without it an empty matrix cannot
         *            be told from a failed parse
         * \return Rows of cells -- one varvec per row, one std::string per cell -- row 0 first
         */
        ALXBASE_API varvec from_bytes(const char* _data, uint_64 _size,
                                      char _delim = ',', bool _strict = true, bool* _ok = nullptr);

        /// Parse CSV text already held in a string; same contract as the pointer overload
        inline varvec from_bytes(const std::string& _str,
                                 char _delim = ',', bool _strict = true, bool* _ok = nullptr) {
            return from_bytes(_str.data(), (uint_64) _str.size(), _delim, _strict, _ok);
        }

        /**
         * \brief Serialize a matrix to CSV text
         *
         * A cell is quoted only when it needs it: when it holds the delimiter, '"', CR or LF, or
         * when its first or last byte is a space or lower. An empty cell goes out as nothing, its
         * delimiters still in place, except in a row that holds that one cell -- there it becomes
         * "" so that the line is not read back as a blank line, which is no record. A row with no
         * cells at all is written as "" too, and so comes back as one empty cell.
         *
         * \param _rows Rows of cells; every cell must hold a std::string, else null comes back
         *              and nothing is guessed or reformatted
         * \param _delim Field delimiter: an ASCII byte other than '"', CR, LF and NUL
         * \return CSV text, one '\n' per row, UTF-8 without BOM; empty but non-null for an empty
         *         matrix, null on a bad delimiter or on a cell that is not text
         */
        ALXBASE_API bytes to_bytes(const varvec& _rows, char _delim = ',');

        /**
         * \brief Serialize a matrix to CSV text, releasing each row as it is written
         *
         * The peak stays max(matrix, text) instead of matrix + text.
         *
         * \param _rows Rows of cells, emptied row by row as the text is written; a failure
         *              releases the rows already written and leaves the rest in place
         * \param _delim Field delimiter; see the borrowing overload for the accepted range
         * \return CSV text, or null when the delimiter is bad or a cell is not text
         */
        ALXBASE_API bytes to_bytes(varvec&& _rows, char _delim = ',');

        /**
         * \brief Serialize a matrix into a stream
         *
         * \param _rows Rows of cells; every cell must hold a std::string
         * \param _out Sink of any kind (file, socket, memory); the walk stops at the first append
         *             it refuses
         * \param _delim Field delimiter: an ASCII byte other than '"', CR, LF and NUL
         * \return true when every row went out; false on a bad delimiter, on a cell that is not
         *         text, or on the first append the sink refused
         */
        ALXBASE_API bool to_bytes(const varvec& _rows, ostream& _out, char _delim = ',');

        /**
         * \brief Serialize a matrix into a stream, releasing each row as it is written
         *
         * \param _rows Rows of cells, emptied row by row as the text is written
         * \param _out Sink; the walk stops at the first append it refuses
         * \param _delim Field delimiter; see the borrowing overload for the accepted range
         * \return true when every row went out
         */
        ALXBASE_API bool to_bytes(varvec&& _rows, ostream& _out, char _delim = ',');
    };
}

#endif
