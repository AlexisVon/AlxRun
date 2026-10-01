/*****************************************************************/ /**
 * \file   aimage.h
 * \brief  Image processing (based on libjpeg-turbo)
 *
 * \author alexis
 *
 * Copyright (c) 2026 AlexisVon
 * SPDX-License-Identifier: MIT
 *********************************************************************/

#ifndef _ALEXIS_IMAGE_H_
#define _ALEXIS_IMAGE_H_

#include "abytes.h"
#include "afile.h"
#include "aplatform.h"

namespace alx {
    /**
     * \brief One pixel of an image, at a given depth
     *
     * Depth 1, 4 and 8 are proxies: each holds a reference to the byte inside the image buffer
     * and reads or writes the bits of one pixel in place, so an assignment through one modifies
     * the image. Depth 16, 24 and 32 are plain POD values read out of the buffer.
     *
     * \tparam DEPTH Bits per pixel; only 1, 4, 8, 16, 24 and 32 are defined
     */
    template <uint_8 DEPTH> struct pixel;
    /// Eight pixels to a byte, most significant bit first
    template <> struct pixel<1> {
        /**
         * \brief Bind to one bit of a buffer
         *
         * \param _data Image buffer; the pixel writes through to it
         * \param _rft Row offset in bytes, i.e. row * image_base::line_bytes()
         * \param _cft Column index, counted in pixels from the start of the row
         */
        pixel(uint_8* _data, uint_64 _rft, uint_64 _cft) : src(*(_data + _rft + (_cft >> 3))), oft((uint_8) (_cft & 0X07U)) {}
        /// True when the bit is set
        operator bool() { return src & (0X80U >> oft); }
        /// The bit as 1 or 0
        operator uint_8() { return src & (0X80U >> oft) ? 0X01U : 0X00U; }
        /// Write the bit; returns _v
        bool operator=(bool _v) {
            src = _v ? src | (0X80U >> oft) : (src & ~(0X80U >> oft));
            return _v;
        }
        /// Write the bit set when _v is not zero; returns _v
        uint_8 operator=(uint_8 _v) {
            src = _v != 0 ? src | (0X80U >> oft) : (src & ~(0X80U >> oft));
            return _v;
        }

    private:
        uint_8& src;
        uint_8 oft;
    };
    /// Two pixels to a byte, high nibble first
    template <> struct pixel<4> {
        /**
         * \brief Bind to one nibble of a buffer
         *
         * \param _data Image buffer; the pixel writes through to it
         * \param _rft Row offset in bytes, i.e. row * image_base::line_bytes()
         * \param _cft Column index; an even column is the high nibble of the byte
         */
        pixel(uint_8* _data, uint_64 _rft, uint_64 _cft) : src(*(_data + _rft + (_cft >> 1))), oft((uint_8) (_cft & 0X01U)) {}
        /// The nibble as 0..15
        operator uint_8() { return (oft ? src : src >> 4) & 0X0FU; }
        /// Write the nibble, clamped to 0..15; returns the value written
        uint_8 operator=(uint_8 _v) {
            _v = alx::min_value<uint_8>(_v, 0X0FU);
            src = oft ? (src & 0XF0U) | _v : (src & 0X0FU) | (_v << 4);
            return _v;
        }

    private:
        uint_8& src;
        bool oft;
    };
    /// One byte to a pixel
    template <> struct pixel<8> {
        /**
         * \brief Bind to one byte of a buffer
         *
         * \param _data Image buffer; the pixel writes through to it
         * \param _rft Row offset in bytes, i.e. row * image_base::line_bytes()
         * \param _cft Column index, one byte per pixel
         */
        pixel(uint_8* _data, uint_64 _rft, uint_64 _cft) : src(*(_data + _rft + _cft)) {}
        /// The stored byte
        operator uint_8() { return src; }
        /// Write the byte; returns _v
        uint_8 operator=(uint_8 _v) { return src = _v; }

    private:
        uint_8& src;
    };
    /// RGB565: 16 bits to a pixel, five for blue, six for green and five for red
    template <> struct pixel<16> {
        /// Build from a packed RGB565 value; zero, that is black, by default
        pixel(uint_16 _v = 0) : value(_v) {}
        /// Build from 8-bit channels, each clamped to the width of its own field
        pixel(uint_8 _b, uint_8 _g, uint_8 _r) : b(alx::min_value<uint_8>(_b, 0X1FU)), g(alx::min_value<uint_8>(_g, 0X3FU)), r(alx::min_value<uint_8>(_r, 0X1FU)) {}
        /// The packed RGB565 value
        operator uint_16() { return value; }
        union {
            struct {
                /// Blue channel, 5 bits
                uint_16 b : 5;
                /// Green channel, 6 bits
                uint_16 g : 6;
                /// Red channel, 5 bits
                uint_16 r : 5;
            };
            /// The three channels as one 16-bit value
            uint_16 value;
        };
    };
    /// 24-bit BGR: the byte order BMP rows and libjpeg-turbo's 3-channel output use
    template <> struct pixel<24> {
        /// Build from a packed 0x00RRGGBB value
        pixel(uint_32 _v) : b(_v & 0XFFU), g((_v >> 8) & 0XFF), r((_v >> 16) & 0XFF) {}
        /// Build from 8-bit channels
        pixel(uint_8 _b, uint_8 _g, uint_8 _r) : b(_b), g(_g), r(_r) {}
        /// The pixel as 0xFFRRGGBB; no alpha is stored, so it always reads as opaque
        operator uint_32() { return 0XFF000000 | r << 16 | g << 8 | b; }
        struct {
            /// Blue, green and red, in the byte order they occupy in the buffer
            uint_8 b, g, r;
        };
    };
    /// 32-bit BGRA: the byte order libjpeg-turbo's 4-channel output uses
    template <> struct pixel<32> {
        /// Build from a packed BGRA value; zero, that is transparent black, by default
        pixel(uint_32 _v = 0) : value(_v) {}
        /// Build from 8-bit channels
        pixel(uint_8 _b, uint_8 _g, uint_8 _r, uint_8 _a) : b(_b), g(_g), r(_r), a(_a) {}
        /// The packed BGRA value
        operator uint_32() { return value; }
        union {
            struct {
                /// Blue, green, red and alpha, in the byte order they occupy in the buffer
                uint_8 b, g, r, a;
            };
            /// The same four bytes as one 32-bit value
            uint_32 value;
        };
    };

    /// One 24-bit BGR pixel
    typedef pixel<24> bgr24;
    /// One 32-bit BGRA pixel; also the element type of a palette
    typedef pixel<32> bgra32;

    /**
     * \brief A raster image: a pixel buffer plus the geometry that describes it
     *
     * The buffer holds height() rows of line_bytes() bytes, row 0 being the top line of the
     * image; every row starts with the leftmost pixel of that line and ends in padding. Derived
     * classes fix the depth and, for a paletted image, the colour table. An image handed out by
     * create_image() or by one of the load functions is owned by the caller, which deletes it.
     */
    class ALXCORE_API image_base {
    public:
        /**
         * \brief Allocate an image of the given depth
         *
         * \param _w Width in pixels
         * \param _h Height in pixels
         * \param _dp Bits per pixel: 1, 4, 8, 16, 24 or 32
         * \param _lb Row stride in bytes; 0 = align_line_bytes(_w, _dp), the smallest stride that
         *            holds a whole row
         * \return A new image owned by the caller; null when _dp is none of those six depths, or
         *         when the buffer, _lb * _h bytes, would take more than 1 GiB
         */
        static image_base* create_image(uint_32 _w, uint_32 _h, uint_8 _dp, uint_64 _lb = 0);
        /**
         * \brief Decode an uncompressed BMP image
         *
         * Takes the depths 1, 4 and 8, reading the colour table the file stores, and also 16, 24
         * and 32. The header must describe the buffer exactly -- the declared pixel data must
         * fill it, and a declared colour count must match the bytes before the pixels -- so
         * trailing slack is refused rather than trimmed; the two magic bytes are not checked.
         * Rows are stored bottom-up and come back with row 0 on top.
         *
         * \param _data Complete BMP file; read only while the call runs
         * \return A new image owned by the caller; null when the data is shorter than a header or
         *         the header does not match the buffer, and when the depth is not one of the six
         *         create_image() takes
         */
        static image_base* load_image_bmp(const bytes_view& _data);
        /**
         * \brief Decode a JPEG image
         *
         * The component count of the file picks the depth: one component gives an 8-bit grayscale
         * image and three a 24-bit one. Four components ask for a 32-bit image, which libjpeg
         * cannot produce out of CMYK. The picture is decoded at full size, and the palette of a
         * paletted image plays no part here.
         *
         * \param _data Complete JPEG file; read only while the call runs
         * \return A new image owned by the caller; null when the input is empty, when the header
         *         declares a zero-sided picture, when it is too big for create_image(), or when a
         *         scanline comes back short. A fatal libjpeg error does not land here and ends
         *         the process instead: no error handler is installed, so libjpeg's default one
         *         reports and calls exit(). Data that is not a JPEG, and a 4-component CMYK file,
         *         take that path
         */
        static image_base* load_image_jpg(const bytes_view& _data);
#ifdef _WIN32
        /**
         * \brief Copy the pixels of a Windows bitmap handle
         *
         * \param _hbm Bitmap to read
         * \param _bcmps Compression to ask GetDIBits for, one of the BI_* values
         * \return A new image owned by the caller; null when _hbm is null, when its geometry is
         *         unusable, when the depth is not supported, or when GetDIBits fails
         */
        static image_base* load_image_hbm(HBITMAP _hbm, DWORD _bcmps = BI_RGB);
#endif
        /**
         * \brief Bytes of one row of _w pixels of depth _dp, padding included
         *
         * Rounds the tight row size up to a 4-byte boundary, the alignment a BMP row needs.
         *
         * \param _w Width in pixels
         * \param _dp Bits per pixel
         */
        static inline uint_64 align_line_bytes(uint_32 _w, uint_8 _dp) { return alx::bit_align(uint_64(uint_64(_w) * uint_64(_dp) + 7) >> 3, 2); }

    public:
        /**
         * \brief Describe an image of _w x _h pixels with a row stride of _lb bytes
         *
         * Called by the derived constructors only, image_base being abstract. The buffer is
         * _lb * _h bytes, so the default stride of 0 leaves it empty.
         *
         * \param _lb Row stride in bytes; below the size of a whole row the image is invalid()
         *            and every conversion refuses it
         */
        image_base(uint_32 _w, uint_32 _h, uint_64 _lb = 0) : w(_w), h(_h), lb(_lb), d(_lb * _h) {}
        /// Releases the pixel buffer; nothing else is owned
        virtual ~image_base() = default;

    public:
        /// True when the pixel buffer is empty, as it is for a zero-sided image
        inline bool null() const { return this->d.empty(); }
        /// Width in pixels
        inline uint_32 width() const { return this->w; }
        /// Height in pixels
        inline uint_32 height() const { return this->h; }
        /// First byte of the pixel buffer, for writing; null when the image is empty
        inline uint_8* data() { return this->d.data(); }
        /// First byte of the pixel buffer; null when the image is empty
        inline const uint_8* data() const { return this->d.data(); }
        /// Size of the buffer, line_bytes() * height(), row padding included
        inline uint_64 byte_count() const { return this->d.size(); }
        /// Row stride in bytes, row padding included
        inline uint_64 line_bytes() const { return this->lb; }
        /// Bytes one row needs for its pixels alone, row padding excluded
        inline uint_64 usfu_line_bytes() const { return (uint_64(this->w) * depth() + 7) >> 3; }
        /// True when the stride holds a whole row; an invalid image cannot be encoded
        inline bool valid() const { return usfu_line_bytes() <= line_bytes(); }

    public:
        /**
         * \brief Encode the image as a BMP file
         *
         * Writes a 16, 24 or 32-bit file for a true-colour image, and a 1, 4 or 8-bit one for a
         * paletted image with its colour table between the header and the pixels; a table of no
         * entries is skipped. Rows are emitted bottom-up and padded out to a 4-byte boundary.
         *
         * \return The whole file, the "BM" magic included; empty when the image is invalid(). An
         *         image with no rows is a contract violation and faults instead of returning: the
         *         row padding is computed by dividing by the height
         */
        bytes to_bmp() const;
        /**
         * \brief Encode the image as a JPEG file
         *
         * Takes the depths 8, 24 and 32 only. A depth-8 image is written as a grayscale JPEG of
         * the bytes it stores, so a palette is not applied -- to_bmp() is the one that keeps the
         * colours. The picture is encoded at full size.
         *
         * \param _quality JPEG quality on the 1..100 scale; libjpeg clamps what falls outside it,
         *                 so 0 acts as 1 and anything above 100 as 100. Higher keeps more detail
         * \return The whole file; empty when the image is invalid() or the depth is one of the
         *         others. A width of 0 is a contract violation: libjpeg refuses an empty picture
         *         and ends the process
         */
        bytes to_jpg(uint_8 _quality = 90) const;

    public:
        /// Bits per pixel, 1, 4, 8, 16, 24 or 32, fixed by the concrete type
        virtual uint_8 depth() const = 0;
        /**
         * \brief Colour table of a paletted image
         *
         * \return Borrowed table of bgra32 entries owned by the image; null when the image has no
         *         table, which under this contract means a depth above 8
         */
        virtual const std::vector<bgra32>* palette() const = 0;
        /// Replace the colour table; no-op for an image that has none
        virtual void set_palette(const std::vector<bgra32>& _pt) = 0;

    protected:
        /// Width and height in pixels, both fixed at construction
        const uint_32 w, h;
        /// Row stride in bytes, fixed at construction
        const uint_64 lb;
        /// Pixel buffer, lb * h bytes
        bytes d;
    };

    /**
     * \brief True-colour image of a fixed depth: the pixels are the colours themselves
     *
     * There is no palette to set or to read back, so set_palette() does nothing and palette() is
     * always null.
     *
     * \tparam DEPTH Bits per pixel; 16, 24 and 32 are the depths this image takes
     */
    template <uint_8 DEPTH>
    class image_impl1
        : public image_base {
    public:
        /// Empty image: 0 x 0, so null() is true
        image_impl1() : image_base(0, 0) {}
        /**
         * \brief Allocate an image of _w x _h pixels
         *
         * \param _lb Row stride in bytes; 0 = align_line_bytes(_w, DEPTH). A larger stride is
         *            honoured as given, a smaller one is rounded up to the aligned size
         */
        image_impl1(uint_32 _w, uint_32 _h, uint_64 _lb = 0)
            : image_base(_w, _h, alx::max_value(align_line_bytes(_w, DEPTH), _lb)) {
            static_assert(DEPTH == 16 || DEPTH == 24 || DEPTH == 32, "image_impl1(_w,_h) unsupport depth");
        }
        /// Releases the pixel buffer; there is no colour table to release
        virtual ~image_impl1() = default;

    public:
        /// Constant: DEPTH
        virtual uint_8 depth() const { return DEPTH; };
        /// Always null: a true-colour image has no colour table
        virtual const std::vector<bgra32>* palette() const { return nullptr; };
        /// No-op: a true-colour image has no colour table to replace
        virtual void set_palette(const std::vector<bgra32>& _pt) {}
        /**
         * \brief Reference to one pixel of the buffer
         *
         * \param _w Column, 0 = the leftmost one; must be inside the image
         * \param _h Row, 0 = the top one; must be inside the image
         * \return Reference into the buffer, valid while the image lives
         */
        pixel<DEPTH>& get_pixel(uint_32 _w, uint_32 _h) {
            return r_interpret<pixel<DEPTH>>(this->d.data() + _h * this->lb + _w * (DEPTH / 8));
        }
    };

    /**
     * \brief Paletted image of a fixed depth: what the buffer stores is an index, not a colour
     *
     * palette() hands out the table given to set_palette(), or, while none was given, a grayscale
     * ramp of 2^DEPTH entries built once and then shared by every image of this depth, so it
     * never returns null.
     *
     * \tparam DEPTH Bits per pixel; 1, 4 and 8 are the depths this image takes
     */
    template <uint_8 DEPTH>
    class image_impl2
        : public image_base {
    public:
        /// Empty image: 0 x 0, so null() is true
        image_impl2() : image_base(0, 0) {}
        /**
         * \brief Allocate an image of _w x _h pixels
         *
         * \param _lb Row stride in bytes; 0 = align_line_bytes(_w, DEPTH). A larger stride is
         *            honoured as given, a smaller one is rounded up to the aligned size
         */
        image_impl2(uint_32 _w, uint_32 _h, uint_64 _lb = 0)
            : image_base(_w, _h, alx::max_value(align_line_bytes(_w, DEPTH), _lb)) {
            static_assert(DEPTH == 1 || DEPTH == 4 || DEPTH == 8, "image_impl2(_w,_h) unsupport depth");
        }
        /// Releases the pixel buffer and the colour table
        virtual ~image_impl2() = default;

    public:
        /// Constant: DEPTH
        virtual uint_8 depth() const { return DEPTH; };
        /**
         * \brief Colour table of the image
         *
         * \return Borrowed table, never null: either the copy the image holds, which stays valid
         *         until the next set_palette() or its destruction, or the shared default ramp
         */
        virtual const std::vector<bgra32>* palette() const {
            static default_palette def;
            return pt.empty() ? &def : &pt;
        };
        /// Replace the colour table with a copy of _pt, whatever number of entries it has
        virtual void set_palette(const std::vector<bgra32>& _pt) { pt = _pt; };
        /**
         * \brief Proxy for one pixel of the buffer
         *
         * \param _w Column, 0 = the leftmost one; must be inside the image
         * \param _h Row, 0 = the top one; must be inside the image
         * \return Proxy bound to the byte and bit of that pixel, so writing through it writes
         *         into the image
         */
        pixel<DEPTH> get_pixel(uint_32 _w, uint_32 _h) {
            return pixel<DEPTH>(this->d.data(), _h * this->lb, _w);
        }

    protected:
        /// Colour table set by set_palette(); empty while none was set
        std::vector<bgra32> pt;
        /// Grayscale ramp of 2^DEPTH entries, used while pt is empty
        struct default_palette : public std::vector<bgra32> {
            /// Entry i is the gray level i * 255 / (2^DEPTH - 1) on all three channels, opaque
            default_palette() {
                resize(0X0001U << DEPTH);
                uint_8 step = 255 / ((uint_8) size() - 1);
                for (uint_16 i = 0; i < size(); i++) operator[](i) = bgra32(i * step, i * step, i * step, 255);
            }
        };
    };

    /**
     * \brief The image type a depth maps to: image_impl1 above 8 bits, image_impl2 below
     *
     * Only the six specializations below are defined, and create_image() is the way to get one;
     * any other depth leaves the type incomplete.
     */
    template <uint_8 DEPTH> class image;
    /// 32-bit BGRA true-colour image; inherits the image_impl1 constructors
    template <> class image<32> : public image_impl1<32> {
        using image_impl1::image_impl1;
    };
    /// 24-bit BGR true-colour image; inherits the image_impl1 constructors
    template <> class image<24> : public image_impl1<24> {
        using image_impl1::image_impl1;
    };
    /// 16-bit RGB565 true-colour image; inherits the image_impl1 constructors
    template <> class image<16> : public image_impl1<16> {
        using image_impl1::image_impl1;
    };
    /// 8-bit paletted image; inherits the image_impl2 constructors
    template <> class image<8> : public image_impl2<8> {
        using image_impl2::image_impl2;
    };
    /// 4-bit paletted image; inherits the image_impl2 constructors
    template <> class image<4> : public image_impl2<4> {
        using image_impl2::image_impl2;
    };
    /// 1-bit paletted image; inherits the image_impl2 constructors
    template <> class image<1> : public image_impl2<1> {
        using image_impl2::image_impl2;
    };
}

#endif