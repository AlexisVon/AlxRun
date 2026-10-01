/*****************************************************************/ /**
 * \file   aserial.h
 * \brief  Binary serialization framework (TLV-style format with type registry)
 *
 * \author alexis
 *
 * Copyright (c) 2026 AlexisVon
 * SPDX-License-Identifier: MIT
 *********************************************************************/

#ifndef _ALEXIS_SERIAL_H_
#define _ALEXIS_SERIAL_H_

#include "abytes.h"
#include "astream.h"
#include "autility.h"
#include "avariant.h"

#include <algorithm>
#include <list>
#include <type_traits>
#include <vector>

namespace alx {
    namespace ser
    {
        /// Unsigned 8-bit integer; the byte is the unit the format is written in
        typedef uint_8 BIT8;
        /// Unsigned 16-bit integer; both block magics are made of these
        typedef uint_16 BIT16;
        /// Unsigned 32-bit integer; a node's type id and a stream's byte count are these
        typedef uint_32 BIT32;
        /// Unsigned 64-bit integer; node and block sizes are counted in these
        typedef uint_64 BIT64;

        /// Format version; it is stamped into both block magics, so a block written by another
        /// version is refused by deserer::valid()
        constexpr BIT8 _VERSION_ = 0X01U;

        /// Block header magic: version in the high byte, 0XA5 in the low byte
        constexpr BIT16 _BLOCK_BEGIN_ = 0X00A5U | ((BIT16) _VERSION_ << 8);
        /// Block tail magic: 0X5A in the high byte, the complement of the version in the low byte
        constexpr BIT16 _BLOCK_END___ = 0X5A00U | (BIT8) (~_VERSION_);
        /// Node header magic: the low byte of serable::HEAD
        constexpr BIT8 _META__BEGIN_ = 0XC3U;
        /// Node tail magic: the only byte of serable::TAIL
        constexpr BIT8 _META__END___ = 0X3CU;

        /// Longest node name the format can carry: the name length field is 16 bits wide
        constexpr BIT64 _MAX_NAME__ = 0X000000000000FFFFU;
        /// Largest node payload: the payload length field is 40 bits wide
        constexpr BIT64 _MAX_META__ = 0X000000FFFFFFFFFFU;
        /// Largest block payload: the block size field is 48 bits wide
        constexpr BIT64 _MAX_DATA__ = 0X00FFFFFFFFFFFFFFU;

        /// Type registry: one BIT16 id per serializable type, and per element type
        ///
        /// Stack-only ids (0X0001 up to 0X0100) are written as sizeof(T) raw bytes, data-size ids
        /// as their data() bytes, container ids as a whole sub-block. A container id is
        /// composite: element id in the high 16 bits, _vector_ or _list_ in the low ones. Host
        /// byte order and host sizes throughout, so a block does not travel between
        /// architectures. The id 0 is reserved as "no type".
        enum TYPE : BIT16 {
            /// No type: what a null payload is written with, and what the type-mapping helpers
            /// answer for a type they do not know
            _void_ = 0X0000U,
            /// Element id of a container of bare serable pointers, i.e. s_varvec and s_varlst
            _any_ = 0XFFFFU,

            __stack_only_begin__ = 0X0000U,
            /// Stack-only: sizeof(bool) raw bytes
            _bool_,
            /// Stack-only: sizeof(int_8) raw bytes
            _int8_,
            /// Stack-only: sizeof(int_16) raw bytes
            _int16_,
            /// Stack-only: sizeof(int_32) raw bytes
            _int32_,
            /// Stack-only: sizeof(int_64) raw bytes
            _int64_,
            /// Stack-only: sizeof(uint_8) raw bytes
            _uint8_,
            /// Stack-only: sizeof(uint_16) raw bytes
            _uint16_,
            /// Stack-only: sizeof(uint_32) raw bytes
            _uint32_,
            /// Stack-only: sizeof(uint_64) raw bytes
            _uint64_,
            /// Stack-only: sizeof(real_32) raw bytes
            _real32_,
            /// Stack-only: sizeof(real_64) raw bytes
            _real64_,
            __stack_only_end__,

            __data_size_begin__ = 0X0100U,
            /// Data-size: the string's bytes, terminator excluded
            _string_,
            /// Data-size: the bytes object's bytes
            _bytes_,
            __data_size_end__,

            __ctrol_enum_begin__ = 0XFF00U,
            /// Container: a sub-block of nodes, whose names are the map keys
            _varmap_,
            /// Container marker in the low half of a composite id: the element id is the high half
            _vector_,
            /// Container marker in the low half of a composite id, as _vector_
            _list_,
            __ctrol_enum_end__,
        };

        /// Id of T among the stack-only types; _void_ when T is not one of them
        template <typename T>
        static constexpr TYPE __stack_only_type{
            std::is_same_v<T, bool> ? _bool_ : std::is_same_v<T, int_8> ? _int8_
                                           : std::is_same_v<T, int_16>  ? _int16_
                                           : std::is_same_v<T, int_32>  ? _int32_
                                           : std::is_same_v<T, int_64>  ? _int64_
                                           : std::is_same_v<T, uint_8>  ? _uint8_
                                           : std::is_same_v<T, uint_16> ? _uint16_
                                           : std::is_same_v<T, uint_32> ? _uint32_
                                           : std::is_same_v<T, uint_64> ? _uint64_
                                           : std::is_same_v<T, real_32> ? _real32_
                                           : std::is_same_v<T, real_64> ? _real64_
                                                                        : _void_};

        /// Id of T among the data+size types; _void_ when T is neither std::string nor bytes
        template <typename T>
        static constexpr TYPE __data_size_type{
            std::is_same_v<T, std::string> ? _string_ : std::is_same_v<T, bytes> ? _bytes_
                                                                                 : _void_};
    }

    namespace ser
    {

        /**
         * \brief One node of a block: a type id, a name, and a payload the derived class writes
         *
         * On the wire a node is HEAD (8 bytes) + type (4) + name + payload + TAIL (1), so
         * EMPTY_SIZE is what a nameless, payload-less node costs. size() must run before read():
         * it computes the payload size once and caches it, and read() puts that cached size into
         * the node's header instead of asking again.
         *
         * A node owns nothing: the caller that allocated it deletes it, and serer and the s_*
         * containers are the ones that delete the nodes they hold.
         */
        class serable {
        public:
            /**
             * \brief Build a node of the given type id and name
             *
             * \param _type Registered type id, written into the node's own type field
             * \param _name Element name, written verbatim; empty for an element of a sequence
             */
            serable(BIT32 _type, std::string _name) : type(_type), name(_name) {}
            /// Copy the node's identity: its type id and its name, not its payload
            serable(const serable& obj) : type(obj.type), name(obj.name) {}
            /// Virtual, so a derived node is destroyed through serable*
            virtual ~serable() {}

        public:

            /**
             * \brief Payload size of this node, computed and cached by the derived class
             *
             * \return the node's whole wire size: overhead + name + payload. A second call
             *         recomputes; read() reuses whatever the last call cached
             */
            virtual BIT64 size() const { return EMPTY_SIZE + name.size() + (data_size = extend_size()); }

            /**
             * \brief Write this node at _ptr, header and tail included
             *
             * Uses the payload size cached by size(), which must have run on this node first.
             * No bound is checked against the buffer: the caller sizes it, usually from size().
             *
             * \param _ptr Start of the node; size() bytes must be writable there
             * \return pointer past the node, or nullptr when the name or the payload is longer
             *         than its header field allows, or when the derived read refused to write
             */
            virtual BIT8* read(BIT8* _ptr) const {
                if (data_size > _MAX_META__ || name.size() > _MAX_NAME__) return nullptr;

                HEAD head;
                TAIL tail;
                head.NAME_SIZE = name.size();
                head.DATA_SIZE = data_size;
                memcpy(_ptr, &head, __HEAD_SIZE__);
                _ptr += __HEAD_SIZE__;
                m_interpret(_ptr, type);
                _ptr += sizeof(BIT32);
                memcpy(_ptr, name.data(), head.NAME_SIZE);
                _ptr += head.NAME_SIZE;
                _ptr = extend_read(_ptr);
                if (nullptr == _ptr) return nullptr;
                memcpy(_ptr, &tail, __TAIL_SIZE__);
                return _ptr + __TAIL_SIZE__;
            }

            /**
             * \brief Write this node to _stm, header and tail included
             *
             * Same cached-size contract as the memory form: size() must have run on this node.
             *
             * \return false when the name or the payload is too long for its header field, or
             *         when the stream refused a write
             */
            virtual bool read(ostream& _stm) const {
                if (data_size > _MAX_META__ || name.size() > _MAX_NAME__) return false;

                HEAD head;
                TAIL tail;
                head.NAME_SIZE = name.size();
                head.DATA_SIZE = data_size;
                return _stm.append_ordinary(head) &&
                       _stm.append_ordinary(type) &&
                       _stm.append(name) &&
                       extend_read(_stm) &&
                       _stm.append_ordinary(tail);
            }
            /// Name of the node; inside a s_varmap it is the key the node is held under
            const std::string& get_name() const { return name; }
            /// Rename the node; a size() taken before this call no longer describes the node
            void set_name(const std::string& _name) { name = _name; }

        public:
            /**
             * \brief Write a node header, its type field and its name, for a caller that writes
             *        the payload itself
             *
             * \param _ptr Start of the node
             * \param _type Type id to record
             * \param _nptr Name bytes; not read when _nsize is 0, so nullptr is fine there
             * \param _nsize Name length
             * \param _dsize Payload length to record, which the caller must have computed
             * \return pointer to where the payload goes, or nullptr when _dsize or _nsize does
             *         not fit its header field
             */
            inline static BIT8* swrite_head(BIT8* _ptr, BIT32 _type, const char* _nptr, BIT64 _nsize, BIT64 _dsize) {
                if (_dsize > _MAX_META__ || _nsize > _MAX_NAME__) return nullptr;

                HEAD head;
                TAIL tail;
                head.NAME_SIZE = _nsize;
                head.DATA_SIZE = _dsize;
                memcpy(_ptr, &head, __HEAD_SIZE__);
                _ptr += __HEAD_SIZE__;
                m_interpret(_ptr, _type);
                _ptr += sizeof(BIT32);
                if (head.NAME_SIZE > 0) memcpy(_ptr, _nptr, head.NAME_SIZE);
                return _ptr + head.NAME_SIZE;
            }
            /// Stream form of swrite_head(); false on an over-long name or payload, or when the
            /// stream refused a write
            inline static bool swrite_head(ostream& _stm, BIT32 _type, const char* _nptr, BIT64 _nsize, BIT64 _dsize) {
                if (_dsize > _MAX_META__ || _nsize > _MAX_NAME__) return false;

                HEAD head;
                TAIL tail;
                head.NAME_SIZE = _nsize;
                head.DATA_SIZE = _dsize;
                return _stm.append_ordinary(head) &&
                       _stm.append_ordinary(_type) &&
                       _stm.append(_nptr, _nsize);
            }
            /// Write the node's tail magic; returns the pointer past it
            inline static BIT8* swrite_tail(BIT8* _ptr) {
                TAIL tail;
                memcpy(_ptr, &tail, __TAIL_SIZE__);
                return _ptr + __TAIL_SIZE__;
            }
            /// Stream form of swrite_tail()
            inline static bool swrite_tail(ostream& _stm) {
                TAIL tail;
                return _stm.append_ordinary(tail);
            }

        protected:

            /// Payload size in bytes; called by size(), which caches the answer
            virtual BIT64 extend_size() const = 0;

            /// Write the payload at _ptr; nullptr fails the whole node
            virtual BIT8* extend_read(BIT8* _ptr) const = 0;

            /// Write the payload to _stm; false fails the whole node
            virtual bool extend_read(ostream& _stm) const = 0;

        private:
            BIT32 type;
            std::string name;
            mutable BIT64 data_size{0};

        public:

            /// Node header: magic, name length and payload length packed into 8 bytes
            struct HEAD {
                /// _META__BEGIN_
                BIT64 BEGIN : 8;
                /// Name length in bytes; at most _MAX_NAME__
                BIT64 NAME_SIZE : 16;
                /// Payload length in bytes; at most _MAX_META__
                BIT64 DATA_SIZE : 40;
                /// Fill in the magic; the two lengths are the caller's business
                HEAD() { BEGIN = _META__BEGIN_; }
            };

            /// Node tail: the magic byte that closes the node
            struct TAIL {
                /// _META__END___
                BIT8 END;
                /// Fill in the magic
                TAIL() { END = _META__END___; }
            };
            /// Wire size of a HEAD, 8 bytes
            static constexpr BIT32 __HEAD_SIZE__ = sizeof(HEAD);
            /// Wire size of a TAIL, 1 byte
            static constexpr BIT32 __TAIL_SIZE__ = sizeof(TAIL);
            /// Offset of the payload from the start of the node: header + type field
            static constexpr BIT32 DATAP_OFST = __HEAD_SIZE__ + sizeof(BIT32);
            /// Wire size of a node with an empty name and an empty payload
            static constexpr BIT32 EMPTY_SIZE = DATAP_OFST + __TAIL_SIZE__;
        };

        /**
         * \brief Collects nodes and writes them out as one block
         *
         * Takes ownership of every node handed to append(): the destructor deletes them and so
         * does clear(). The copy constructor copies the pointer list rather than the nodes, so a
         * copy and its original then both delete the same nodes.
         */
        class serer {
        public:
            /// Empty collector; append() adds nodes to it
            serer() {}
            /// Share obj's node list: a shallow copy, both objects then own the same nodes
            serer(const serer& obj) : content(obj.content) {}
            /// Delete every appended node
            ~serer() { clear(); }

            /// Delete every appended node and empty the list
            void clear() {
                for (auto i : content) delete i;
                content.clear();
            }

            /**
             * \brief Append a node, taking ownership of it
             *
             * \param _obj Node to own; it must not be deleted anywhere else
             */
            void append(serable* _obj) { content.push_back(_obj); }

            /**
             * \brief Write every appended node into one block, header and tail included
             *
             * Calls size() on each node first -- that is what gives the nodes the payload size
             * their read() then caches -- and writes them in append order.
             *
             * \param _err_obj if non-null, receives the node that failed
             * \return the block, or empty bytes on failure: over _MAX_DATA__ in total, a node
             *         that would not write, or a written length that disagrees with size()
             */
            bytes read(serable** _err_obj = nullptr) const {
                BIT64 size{0};
                HEAD head;
                TAIL tail;
                for (auto& it : content) size += it->size();
                if (size > _MAX_DATA__) return bytes();

                head.SIZE = size;
                size += EMPTY_SIZE;
                bytes result(size);
                BIT8* ptr = result.data();
                BIT8* tmp;
                memcpy(ptr, &head, __HEAD_SIZE__);
                ptr += __HEAD_SIZE__;

                for (auto& it : content) {
                    tmp = it->read(ptr);
                    if (nullptr == tmp) {
                        if (_err_obj != nullptr) *_err_obj = it;
                        return bytes();
                    } else ptr = tmp;
                }
                if (ptr - result.data() + __TAIL_SIZE__ != result.size()) return bytes();
                return memcpy(ptr, &tail, __TAIL_SIZE__), result;
            }

            /**
             * \brief Write every appended node to _stm as one block, then flush the stream
             *
             * The length check reads the stream's own byte count, so _stm must carry nothing
             * when the call starts: a stream that already holds bytes always fails.
             *
             * \param _err_obj if non-null, receives the node that failed
             * \return false on the same failures as the memory form, or when the stream refused
             *         the tail write or the flush
             */
            bool read(ostream& _stm, serable** _err_obj = nullptr) const {
                BIT64 size{0};
                HEAD head;
                TAIL tail;
                for (auto& it : content) size += it->size();
                if (size > _MAX_DATA__) return false;

                head.SIZE = size;
                size += EMPTY_SIZE;
                _stm.append_ordinary(head);

                for (auto& it : content) {
                    if (!it->read(_stm)) {
                        if (_err_obj != nullptr) *_err_obj = it;
                        return false;
                    }
                }
                if (_stm.total() + __TAIL_SIZE__ != size) return false;
                return _stm.append_ordinary(tail) && _stm.flush();
            }

        public:
            /**
             * \brief Write a block header that announces _dsize bytes of nodes
             *
             * \param _dsize Size of the node area, header and tail excluded
             * \return pointer past the header, or nullptr when _dsize exceeds _MAX_DATA__
             */
            inline static BIT8* swrite_head(BIT8* _ptr, BIT64 _dsize) {
                if (_dsize > _MAX_DATA__) return nullptr;
                HEAD head;
                head.SIZE = _dsize;
                memcpy(_ptr, &head, __HEAD_SIZE__);
                return _ptr + __HEAD_SIZE__;
            }
            /// Stream form of swrite_head(): false on an over-large _dsize, or when the stream
            /// refused the write
            inline static bool swrite_head(ostream& _stm, BIT64 _dsize) {
                if (_dsize > _MAX_DATA__) return false;
                HEAD head;
                head.SIZE = _dsize;
                return _stm.append_ordinary(head);
            }
            /// Write the block's tail magic; returns the pointer past it
            inline static BIT8* swrite_tail(BIT8* _ptr) {
                TAIL tail;
                memcpy(_ptr, &tail, __TAIL_SIZE__);
                return _ptr + __TAIL_SIZE__;
            }
            /// Stream form of swrite_tail()
            inline static bool swrite_tail(ostream& _stm) {
                TAIL tail;
                return _stm.append_ordinary(tail);
            }

        public:

            /// Block header: magic and the size of the node area, packed into 8 bytes
            struct HEAD {
                /// _BLOCK_BEGIN_
                BIT64 BEGIN : 16;
                /// Total size of the nodes that follow, header and tail excluded; at most
                /// _MAX_DATA__
                BIT64 SIZE : 48;
                /// Fill in the magic; SIZE is the caller's business
                HEAD() { BEGIN = _BLOCK_BEGIN_; }
            };

            /// Block tail: the magic that closes the block
            struct TAIL {
                /// _BLOCK_END___
                BIT16 END;
                /// Fill in the magic
                TAIL() { END = _BLOCK_END___; }
            };
            /// The nodes, in write order; owned by this object
            std::vector<serable*> content;
            /// Wire size of a HEAD, 8 bytes
            static constexpr BIT64 __HEAD_SIZE__ = sizeof(HEAD);
            /// Wire size of a TAIL, 2 bytes
            static constexpr BIT64 __TAIL_SIZE__ = sizeof(TAIL);
            /// Wire size of a block that carries no node at all
            static constexpr BIT64 EMPTY_SIZE = __HEAD_SIZE__ + __TAIL_SIZE__;
        };

        /**
         * \brief Parses a block back into a collection, through a registry of per-type handlers
         *
         * A handler turns one node's metadata into one element. CORE_BASE is keyed by the type
         * id as it appears in the block, CORE__VEC and CORE_LIST by the element id alone (the
         * block id is element << 16 | _vector_ or _list_). The maps are static, so every user of
         * one instantiation shares them and a handler registered later replaces the earlier one.
         * A type with no handler yields a default-constructed element.
         *
         * METADAT points into the caller's buffer, which must outlive the parse: a handler that
         * keeps anything has to copy it.
         *
         * \tparam _COLLECT_ collection to fill; it needs operator[] taking a std::string key
         * \tparam _ELEM_    element type its values are built into
         */
        template <typename _COLLECT_, typename _ELEM_>
        class deserer {
        public:
            /// Block overhead: serer::EMPTY_SIZE, header + tail; a shorter buffer is no block
            static constexpr int EMPTY_SIZE = serer::EMPTY_SIZE;
            /// Node overhead: serable::EMPTY_SIZE, header + type field + tail
            static constexpr int EMPTY_DATA_SIZE = serable::EMPTY_SIZE;

            /**
             * \brief One node as the parser sees it
             *
             * `name` borrows the node's key bytes from the block instead of copying them, and
             * `ptr`/`size` point at its payload; a nameless node -- an element of a sequence --
             * is the one with name.size == 0.
             */
            struct METADAT {
                /// Type id as it appears in the block, container half included
                BIT32 type{0};
                /// Payload length in bytes; 0 until a node was read
                BIT32 size{0};
                /// The node's key: bytes of the block, not of this struct
                struct NAME {
                    /// First key byte; not null-terminated
                    char* data;
                    /// Key length in bytes; 0 for a nameless node
                    uint_64 size;
                    /// Compare the borrowed key against a string
                    inline bool operator==(const std::string& _str) const {
                        return size == _str.size() && (size == 0 || memcmp(data, _str.data(), size) == 0);
                    }
                } name;
                /// Start of the payload; nullptr until a node was read
                const BIT8* ptr{nullptr};
            };
            /// Handler for one node: fills _rst from the node's metadata
            typedef void (*COREFUNC)(const METADAT&, _ELEM_&);
            /// Handlers for a scalar node, keyed by the id the block carries
            static std::unordered_map<BIT16, COREFUNC> CORE_BASE;
            /// Handlers for a vector node, keyed by the element id, not the composite one
            static std::unordered_map<BIT16, COREFUNC> CORE__VEC;
            /// Handlers for a list node, keyed by the element id, not the composite one
            static std::unordered_map<BIT16, COREFUNC> CORE_LIST;

        public:

            /**
             * \brief Parse a whole block into a collection
             *
             * Each node becomes an entry under its own name. An invalid block is not reported
             * here: nothing is parsed and the collection comes back empty, so call valid() first
             * when that has to be told apart from an empty block.
             *
             * \param _data Block to parse
             */
            inline static _COLLECT_ parse(const bytes_view& _data) {
                _COLLECT_ result;
                for_each_meta(_data, [&](const METADAT& meta) {
                    read_dat(meta, result[std::string(meta.name.data, meta.name.size)]);
                    return true;
                });
                return result;
            }

            /**
             * \brief Walk a path into a block and return the element it lands on
             *
             * A component is a map key while the node reached so far is a varmap, and a decimal
             * index while it is a vector or a list of bare serable pointers. An invalid block, an
             * unknown key, a non-decimal or out-of-range index, and a node of any other kind all
             * answer _def. An empty path parses the whole block, which needs _ELEM_ to be
             * constructible from _COLLECT_.
             *
             * \param _data Block to walk
             * \param _path Keys and indices, outermost first
             * \param _def  Value to answer when the path does not resolve
             */
            inline static _ELEM_ parse(const bytes_view& _data, const std::list<std::string>& _path, const _ELEM_& _def) {
                if (!valid(_data)) return _def;
                if (_path.empty()) return parse(_data);

                serer::HEAD head = m_interpret<serer::HEAD>(_data.data());
                METADAT item;
                item.type = _varmap_;
                item.ptr = _data.data() + serer::__HEAD_SIZE__;
                item.size = head.SIZE;

                auto key_index = [](const std::string& _key, uint_64& _index) -> bool {
                    if (_key.empty()) return false;
                    uint_64 value{0};
                    for (const char ch : _key) {
                        if (!::isdigit((unsigned char) ch)) return false;
                        if (value > (max_uint_64 - (uint_64) (ch - '0')) / 10) return false;
                        value = value * 10 + (uint_64) (ch - '0');
                    }
                    return _index = value, true;
                };
                uint_64 index{0};
                for (const std::string& key : _path) {
                    if (_varmap_ == (TYPE) item.type) {
                        METADAT found{};
                        bool has_found{false};
                        for_each_meta(item.ptr, item.ptr + item.size, [&](const METADAT& meta) {
                            if (meta.name == key) {
                                found = meta;
                                has_found = true;
                                return false;
                            }
                            return true;
                        });
                        if (has_found) item = found;
                        else return _def;
                    } else if ((BIT32(_any_ << 16 | _vector_) == item.type || BIT32(_any_ << 16 | _list_) == item.type) &&
                               key_index(key, index)) {
                        METADAT found{};
                        bool has_found{false};
                        uint_64 idx{0};
                        for_each_meta(item.ptr, item.ptr + item.size, [&](const METADAT& meta) {
                            if (idx++ == index) {
                                found = meta;
                                has_found = true;
                                return false;
                            }
                            return true;
                        });
                        if (has_found) item = found;
                        else return _def;
                    } else return _def;
                }
                _ELEM_ result;
                return read_dat(item, result), result;
            }

            /**
             * \brief Register a scalar and a vector handler for one element type
             *
             * \param _type Element id, not the composite one: it is the key of both maps
             * \param _base Handler for a scalar node of that type
             * \param _vec  Handler for a vector node of that type
             */
            inline static void install_parse(BIT16 _type, COREFUNC _base, COREFUNC _vec) {
                CORE_BASE[_type] = _base;
                CORE__VEC[_type] = _vec;
            }

            /**
             * \brief Check a block's magics and its size fields
             *
             * \param _data Block to check
             * \return true when the buffer is longer than a bare header, when both magics match
             *         this format version, when the announced size fits inside the buffer, and
             *         when that size is past the node overhead. The nodes themselves are not
             *         inspected
             */
            inline static bool valid(const bytes_view& _data) {
                if (_data.size() <= EMPTY_SIZE) return false;
                serer::HEAD head = m_interpret<serer::HEAD>(_data.data());
                if ((head.SIZE + EMPTY_SIZE) > _data.size()) return false;
                serer::TAIL tail = m_interpret<serer::TAIL>(_data.data() + serer::__HEAD_SIZE__ + head.SIZE);
                if (head.BEGIN != _BLOCK_BEGIN_ || tail.END != _BLOCK_END___ ||
                    head.SIZE <= EMPTY_DATA_SIZE) return false;
                return true;
            }
            /// Raw bytes as a vector<T>: one memcpy of _size / sizeof(T) whole elements, so a
            /// trailing partial element is dropped
            template <typename T> inline static std::enable_if_t<!std::is_same_v<T, bool>, const std::vector<T>>
            r_interpret(const BIT8* _ptr, BIT32 _size) {

                const uint_64 count = _size / sizeof(T);
                std::vector<T> vec(count);
                if (0 == count) return vec;
                memcpy(vec.data(), _ptr, count * sizeof(T));
                return vec;
            }
            /// Raw bytes as a vector<bool>: one byte per element, since vector<bool> is packed
            /// and cannot be memcpy'd into
            template <typename T> inline static std::enable_if_t<std::is_same_v<T, bool>, const std::vector<bool>>
            r_interpret(const BIT8* _ptr, BIT32 _size) {
                std::vector<bool> result;
                result.resize(_size / sizeof(bool));
                for (size_t i = 0; i < result.size(); i++) result[i] = ((bool*) _ptr)[i];
                return result;
            }

            /**
             * \brief Walk the nodes of a raw byte range, calling _func on each of them
             *
             * Stops at the first node that would run past _end or whose magics are wrong, so a
             * truncated range yields the nodes it did hold.
             *
             * \param _ptr  First node
             * \param _end  End of the node area
             * \param _func Called with each node's metadata; return false to stop the walk
             */
            template <typename Func>
            static void for_each_meta(const BIT8* _ptr, const BIT8* _end, Func&& _func) {
                METADAT data;
                while (_ptr + EMPTY_DATA_SIZE <= _end) {
                    serable::HEAD head = m_interpret<serable::HEAD>(_ptr);
                    _ptr += serable::__HEAD_SIZE__;
                    data.type = m_interpret<BIT32>(_ptr);
                    _ptr += sizeof(BIT32);
                    if (_ptr + head.NAME_SIZE + head.DATA_SIZE + serable::__TAIL_SIZE__ > _end) break;
                    data.name = {(char*) _ptr, head.NAME_SIZE};
                    _ptr += head.NAME_SIZE;
                    data.ptr = _ptr;
                    data.size = head.DATA_SIZE;
                    _ptr += head.DATA_SIZE;
                    serable::TAIL tail = m_interpret<serable::TAIL>(_ptr);
                    _ptr += serable::__TAIL_SIZE__;
                    if (head.BEGIN != _META__BEGIN_ || tail.END != _META__END___) break;
                    if (!_func(data)) return;
                }
            }

            /**
             * \brief Walk the nodes of a whole block
             *
             * \param _data Block to walk; an invalid one is a silent no-op, the checks being the
             *              same as valid()'s
             * \param _func Called with each node's metadata; return false to stop the walk
             */
            template <typename Func>
            static void for_each_meta(const bytes_view& _data, Func&& _func) {
                if (_data.size() <= EMPTY_SIZE) return;
                serer::HEAD head = m_interpret<serer::HEAD>(_data.data());
                if ((head.SIZE + EMPTY_SIZE) > _data.size()) return;
                serer::TAIL tail = m_interpret<serer::TAIL>(_data.data() + serer::__HEAD_SIZE__ + head.SIZE);
                if (head.BEGIN != _BLOCK_BEGIN_ || tail.END != _BLOCK_END___ || head.SIZE <= EMPTY_DATA_SIZE) return;
                const BIT8* ptr = (BIT8*) _data.data() + serer::__HEAD_SIZE__;
                const BIT8* end = (BIT8*) _data.data() + serer::__HEAD_SIZE__ + head.SIZE;
                for_each_meta(ptr, end, std::forward<Func>(_func));
            }

            /**
             * \brief Dispatch one node to the handler its type id names
             *
             * An id no map carries sets _rst to a default-constructed element, which is also what
             * a _void_ node reads back as.
             */
            static void read_dat(const METADAT& _dat, _ELEM_& _rst) {
                if ((_dat.type & 0X0000FFFFU) == _dat.type) {
                    if (_varmap_ == (TYPE) _dat.type) read_map(_dat, _rst);
                    else {
                        auto it = CORE_BASE.find(_dat.type);
                        if (it != CORE_BASE.end()) it->second(_dat, _rst);
                        else _rst = _ELEM_();
                    }
                } else if ((_dat.type & 0X0000FFFFU) == _vector_) {
                    auto it = CORE__VEC.find(_dat.type >> 16);
                    if (it != CORE__VEC.end()) it->second(_dat, _rst);
                    else _rst = _ELEM_();
                } else if ((_dat.type & 0X0000FFFFU) == _list_) {
                    auto it = CORE_LIST.find(_dat.type >> 16);
                    if (it != CORE_LIST.end()) it->second(_dat, _rst);
                    else _rst = _ELEM_();
                } else _rst = _ELEM_();
            }

            /// Parse a varmap node's children into a fresh collection and move it into _rst
            static void read_map(const METADAT& _dat, _ELEM_& _rst) {
                _COLLECT_ value;
                for_each_meta(_dat.ptr, _dat.ptr + _dat.size, [&](const METADAT& meta) {
                    read_dat(meta, value[std::string(meta.name.data, meta.name.size)]);
                    return true;
                });
                _rst = std::move(value);
            }
        };

        /// Storage of deserer::CORE_BASE, one map per instantiation of the template
        template <typename _COLLECT_, typename _ELEM_>
        std::unordered_map<BIT16, typename deserer<_COLLECT_, _ELEM_>::COREFUNC> deserer<_COLLECT_, _ELEM_>::CORE_BASE;
        /// Storage of deserer::CORE__VEC, one map per instantiation of the template
        template <typename _COLLECT_, typename _ELEM_>
        std::unordered_map<BIT16, typename deserer<_COLLECT_, _ELEM_>::COREFUNC> deserer<_COLLECT_, _ELEM_>::CORE__VEC;
        /// Storage of deserer::CORE_LIST, one map per instantiation of the template
        template <typename _COLLECT_, typename _ELEM_>
        std::unordered_map<BIT16, typename deserer<_COLLECT_, _ELEM_>::COREFUNC> deserer<_COLLECT_, _ELEM_>::CORE_LIST;

        /**
         * \brief Binds a payload class to serable, with the payload's operations passed in as
         *        compile-time function pointers
         *
         * extend_size() and extend_read() forward to _SIZEIMPL_ / _READIMPL_ / _STEMIMPL_, and
         * _DESTROY_, when given, runs from the destructor -- that is where a container of owning
         * pointers deletes its pointees. The node's type id is _TYPE_ and is exposed as _Type;
         * together they are what a deserer's registry has to be keyed by.
         *
         * \tparam _CLASS_    payload type, held by value
         * \tparam _TYPE_     type id written into every node this class writes
         * \tparam _SIZEIMPL_ payload size of one _CLASS_ value
         * \tparam _READIMPL_ writes the payload, returns the pointer past it
         * \tparam _STEMIMPL_ writes the payload to a stream
         * \tparam _DESTROY_  optional cleanup run by the destructor; nullptr = nothing to free
         */
        template <class _CLASS_, BIT32 _TYPE_,
                  BIT64 (*_SIZEIMPL_)(const _CLASS_&),
                  BIT8* (*_READIMPL_)(const _CLASS_&, BIT8*),
                  bool (*_STEMIMPL_)(const _CLASS_&, ostream&),
                  void (*_DESTROY_)(_CLASS_&) = nullptr>
        class serobj
            : public serable {
        protected:
            /// The payload; a derived class is expected to read and write it in place
            _CLASS_ data;

            /// Payload size, through _SIZEIMPL_
            virtual BIT64 extend_size() const override { return _SIZEIMPL_(data); };
            /// Write the payload through _READIMPL_
            virtual BIT8* extend_read(BIT8* _ptr) const override { return _READIMPL_(data, _ptr); };
            /// Write the payload through _STEMIMPL_
            virtual bool extend_read(ostream& _stm) const override { return _STEMIMPL_(data, _stm); };

        public:
            enum { _Type = _TYPE_ };

            using serable::read;
            using serable::size;

            /// Run _DESTROY_ on the payload, when one was given
            ~serobj() {
                if (_DESTROY_ != nullptr) _DESTROY_(data);
            }

            /**
             * \brief Wrap a payload, copying it
             *
             * \param _data Payload to hold; the default is an empty one
             * \param _name Node name; empty for an element of a sequence
             */
            serobj(const _CLASS_& _data = _CLASS_(), const std::string& _name = std::string())
                : serable(_TYPE_, _name), data(_data) {
            }
            /**
             * \brief Wrap a payload, moving it in
             *
             * \param _data Payload to move
             * \param _name Node name; empty for an element of a sequence
             */
            serobj(_CLASS_&& _data, const std::string& _name = std::string())
                : serable(_TYPE_, _name), data(std::forward<_CLASS_>(_data)) {
            }
            /// Wrap a default-constructed payload under a name, for a node that starts out empty
            serobj(const std::string& _name)
                : serable(_TYPE_, _name), data(_CLASS_()) {
            }

            /// Copy the node's name and its payload; a payload of owning pointers is shared
            serobj(const serobj& obj)
                : serable(obj), data(obj.data) {
            }
            /// Assign the payload only; the node's name and type id are left alone
            serobj& operator=(const serobj& obj) {
                if (this == &obj) return *this;
                data = obj.data;
                return *this;
            }

            /// Move the payload in, leaving obj with a default-constructed one
            serobj(serobj&& obj) noexcept
                : serable(obj), data(std::move(obj.data)) {
                obj.data = _CLASS_();
            }
            /// Move-assign the payload, leaving obj with a default-constructed one
            serobj& operator=(serobj&& obj) noexcept {
                if (this == &obj) return *this;
                data = std::move(obj.data);
                obj.data = _CLASS_();
                return *this;
            }

            /// The payload of this node, to read or to write before the node is serialized
            _CLASS_& get() { return data; }

            /// The payload of this node, read-only
            const _CLASS_& get() const { return data; }

        public:
            /// Payload size of a _CLASS_ value, through _SIZEIMPL_; no node is involved
            inline static BIT64 sdata_size(const _CLASS_& _data) { return _SIZEIMPL_(_data); }
            /// Node header carrying _TYPE_, for a caller that writes the payload itself
            inline static BIT8* swrite_head(BIT8* _ptr, const char* _name, BIT64 _nsize, BIT64 _dsize) {
                return serable::swrite_head(_ptr, _TYPE_, _name, _nsize, _dsize);
            }
            /// Stream form of the header write, carrying _TYPE_
            inline static bool swrite_head(ostream& _stm, const char* _name, BIT64 _nsize, BIT64 _dsize) {
                return serable::swrite_head(_stm, _TYPE_, _name, _nsize, _dsize);
            }
            /// Write a payload through _READIMPL_; returns the pointer past it
            inline static BIT8* swrite_data(BIT8* _ptr, const _CLASS_& _data) {
                return _READIMPL_(_data, _ptr);
            }
            /// Write a payload through _STEMIMPL_
            inline static bool swrite_data(ostream& _stm, const _CLASS_& _data) {
                return _STEMIMPL_(_data, _stm);
            }
            using serable::swrite_tail;
        };
    }

    namespace ser
    {

        /// Bind a trivially copyable T as a serable payload: sizeof(T) raw bytes, host layout
        template <typename T>
        struct __s_stack_only {
        private:
            static constexpr BIT32 Type = __stack_only_type<T>;
            static BIT64 impl_size(const T& _data) {
                return sizeof(T);
            }
            static BIT8* impl_memcpy(const T& _data, BIT8* _ptr) {
                memcpy(_ptr, &_data, sizeof(T));
                return _ptr + sizeof(T);
            }
            static bool impl_stream(const T& _data, ostream& _stm) {
                return _stm.append(&_data, sizeof(T));
            }

        public:
            /// T, bound to its stack-only id and to the raw-bytes operations above
            typedef serobj<T, Type,
                           &__s_stack_only::impl_size,
                           &__s_stack_only::impl_memcpy,
                           &__s_stack_only::impl_stream>
                value;
        };
        /// The serable wrapper of T, for a T that is one of the stack-only types
        template <typename T>
        using s_stack_only_t = typename __s_stack_only<T>::value;

        /// bool, as a stack-only payload
        typedef s_stack_only_t<bool> s_bool;
        /// int_8, as a stack-only payload
        typedef s_stack_only_t<int_8> s_int_8;
        /// int_16, as a stack-only payload
        typedef s_stack_only_t<int_16> s_int_16;
        /// int_32, as a stack-only payload
        typedef s_stack_only_t<int_32> s_int_32;
        /// int_64, as a stack-only payload
        typedef s_stack_only_t<int_64> s_int_64;
        /// uint_8, as a stack-only payload
        typedef s_stack_only_t<uint_8> s_uint_8;
        /// uint_16, as a stack-only payload
        typedef s_stack_only_t<uint_16> s_uint_16;
        /// uint_32, as a stack-only payload
        typedef s_stack_only_t<uint_32> s_uint_32;
        /// uint_64, as a stack-only payload
        typedef s_stack_only_t<uint_64> s_uint_64;
        /// real_32, as a stack-only payload
        typedef s_stack_only_t<real_32> s_real_32;
        /// real_64, as a stack-only payload
        typedef s_stack_only_t<real_64> s_real_64;
    }

    namespace ser
    {

        /// Bind a type with data()/size() as a serable payload: those data() bytes, nothing else
        template <typename T>
        class __s_data_size {
        private:
            static constexpr BIT32 Type = __data_size_type<T>;
            static BIT64 impl_size(const T& _data) {
                return _data.size();
            }
            static BIT8* impl_memcpy(const T& _data, BIT8* _ptr) {
                memcpy(_ptr, _data.data(), _data.size());
                return _ptr + _data.size();
            }
            static bool impl_stream(const T& _data, ostream& _stm) {
                return _stm.append(_data.data(), _data.size());
            }

        public:
            /// T, bound to its data+size id and to the data()/size() operations above
            typedef serobj<T, Type,
                           &__s_data_size::impl_size,
                           &__s_data_size::impl_memcpy,
                           &__s_data_size::impl_stream>
                value;
        };
        /// The serable wrapper of T, for a T that is std::string or bytes
        template <typename T>
        using s_data_size_t = typename __s_data_size<T>::value;

        /// std::string, as a data+size payload
        typedef s_data_size_t<std::string> s_string;
        /// bytes, as a data+size payload
        typedef s_data_size_t<bytes> s_bytes;
    }

    namespace ser
    {
        /// std::vector<T> as a serable payload; which of the three element kinds T is decides
        /// how the vector is written
        template <class T>
        struct __s_vector;
        template <class T>
        struct __s_vector {
        private:
            template <class _T, bool _s_ptr_flag, bool _bool_flag>
            struct __impl {
                /// Element id, the container half dropped
                static constexpr BIT32 Type{_T::_Type & 0X0000FFFFU};
                /// Sum of the elements' own sizes
                static BIT64 impl_size(const std::vector<_T>& _data) {
                    BIT64 size{0};
                    for (const _T& it : _data) size += it.size();
                    return size;
                }
                /// Let every element write itself
                static BIT8* impl_memcpy(const std::vector<_T>& _data, BIT8* _ptr) {
                    for (const _T& it : _data) _ptr = it.read(_ptr);
                    return _ptr;
                }
                /// Let every element write itself to the stream
                static bool impl_stream(const std::vector<_T>& _data, ostream& _stm) {
                    for (const _T& it : _data)
                        if (!it.read(_stm)) return false;
                    return true;
                }
            };
            /// Element kind is a stack-only type: the elements are one contiguous byte run
            template <class _T>
            struct __impl<_T, false, false> {
                /// The element type's own id; the static_assert rejects anything unregistered
                static constexpr BIT32 Type = __stack_only_type<_T>;
                /// size() * sizeof(T)
                static BIT64 impl_size(const std::vector<_T>& _data) {
                    static_assert(__stack_only_type<_T> != _void_, "invalid s_vector<T>");
                    return _data.size() * sizeof(_T);
                }
                /// One memcpy for the whole run
                static BIT8* impl_memcpy(const std::vector<_T>& _data, BIT8* _ptr) {
                    memcpy(_ptr, _data.data(), _data.size() * sizeof(_T));
                    _ptr += _data.size() * sizeof(_T);
                    return _ptr;
                }
                /// Write the run in one append
                static bool impl_stream(const std::vector<_T>& _data, ostream& _stm) {
                    return _stm.append(_data.data(), _data.size() * sizeof(_T));
                }
            };
            /// Element kind is bool: one byte per element on the wire, since vector<bool> packs
            template <class _T>
            struct __impl<_T, false, true> {
                /// _bool_
                static constexpr BIT32 Type = __stack_only_type<bool>;
                /// size() * sizeof(bool), the packed in-memory form expanded
                static BIT64 impl_size(const std::vector<bool>& _data) {
                    return _data.size() * sizeof(bool);
                }
                /// Unpack the bit-packed vector, one byte per element
                static BIT8* impl_memcpy(const std::vector<bool>& _data, BIT8* _ptr) {
                    bool* ptr = (bool*) _ptr;
                    for (size_t i = 0; i < _data.size(); i++)
                        ptr[i] = _data[i];
                    _ptr += _data.size() * sizeof(bool);
                    return _ptr;
                }
                /// One append_ordinary per element
                static bool impl_stream(const std::vector<bool>& _data, ostream& _stm) {
                    for (size_t i = 0; i < _data.size(); i++)
                        if (!_stm.append_ordinary(_data[i])) return false;
                    return true;
                }
            };
            typedef __impl<T, std::is_base_of_v<serable, T>, std::is_same_v<T, bool>> impl;
            static constexpr BIT32 Type{impl::Type << 16 | _vector_};

        public:
            /// std::vector<T>, bound to its composite id element << 16 | _vector_
            typedef serobj<std::vector<T>, Type,
                           impl::impl_size,
                           impl::impl_memcpy,
                           impl::impl_stream>
                value;
        };
        /// std::vector<T*>: every element writes itself, and the node owns the pointees
        template <class T>
        struct __s_vector<T*> {
        private:
            template <typename _T, bool> struct _Type;
            template <typename _T, bool> struct _Type {
                enum : BIT32 { value = _T::_Type };
            };
            template <typename _T> struct _Type<_T, true> {
                enum : BIT32 { value = _any_ };
            };
            static constexpr BIT32 Type{_Type<T, std::is_same_v<T, serable>>::value << 16 | _vector_};
            static BIT64 impl_size(const std::vector<T*>& _data) {
                BIT64 size{0};
                for (T* const& it : _data) size += it->size();
                return size;
            }
            static BIT8* impl_memcpy(const std::vector<T*>& _data, BIT8* _ptr) {
                for (T* const& it : _data) _ptr = it->read(_ptr);
                return _ptr;
            }
            static bool impl_stream(const std::vector<T*>& _data, ostream& _stm) {
                for (T* const& it : _data)
                    if (!it->read(_stm)) return false;
                return true;
            }
            static void Destroy(std::vector<T*>& _data) {
                for (T*& it : _data) delete it;
                _data.clear();
            }

        public:
            /// std::vector<T*>, bound to T's id or to _any_ for a bare serable*; destroying the
            /// node deletes the pointees
            typedef serobj<std::vector<T*>, Type,
                           &impl_size,
                           &impl_memcpy,
                           &impl_stream,
                           &Destroy>
                BASE;
            /// The pointees belong to the node, so a copy would leave two nodes deleting them
            struct value : public BASE {
                using BASE::BASE;
                value(const value&) = delete;
                value& operator=(const value&) = delete;
            };
        };

        /**
         * \brief Serializable std::vector<T>
         *
         * T is a stack-only type (the elements go out as one raw run), bool (one byte per
         * element), or a class derived from serable (each element writes itself). A pointer
         * element type takes the specialization that owns its pointees: destroying the node
         * deletes them, so two nodes holding the same pointers would delete them twice.
         */
        template <class T>
        class s_vector
            : public __s_vector<T>::value {
        public:
            /// The serobj this class derives from
            typedef typename __s_vector<T>::value BASE;
            using BASE::BASE;
            using BASE::operator=;

            /// Append an element, copying it
            void append(const T& _value) { BASE::data.push_back(_value); }
            /// Append an element, moving it
            void append(T&& _value) { BASE::data.push_back(std::forward<T>(_value)); }
            /// Element at _index; throws std::out_of_range when _index is past the end
            const T& at(size_t _index) { return BASE::data.at(_index); }
            /// Element at _index, unchecked
            T& operator[](size_t _index) { return BASE::data[_index]; }
            /// Element at _index, unchecked
            const T& operator[](size_t _index) const { return BASE::data[_index]; }
        };

        /// A sequence of bare serable pointers; every element carries its own type id
        typedef s_vector<serable*> s_varvec;
    }

    namespace ser
    {
        template <class T>
        struct __s_list;
        template <class T>
        struct __s_list {
        private:
            template <class _T, bool _s_ptr_flag>
            struct __impl {
                /// Element id, the container half dropped
                static constexpr BIT32 Type{_T::_Type & 0X0000FFFFU};
                /// Sum of the elements' own sizes
                static BIT64 impl_size(const std::list<_T>& _data) {
                    BIT64 size{0};
                    for (const _T& it : _data) size += it.size();
                    return size;
                }
                /// Let every element write itself
                static BIT8* impl_memcpy(const std::list<_T>& _data, BIT8* _ptr) {
                    for (const _T& it : _data) _ptr = it.read(_ptr);
                    return _ptr;
                }
                /// Let every element write itself to the stream
                static bool impl_stream(const std::list<_T>& _data, ostream& _stm) {
                    for (const _T& it : _data)
                        if (!it.read(_stm)) return false;
                    return true;
                }
            };
            /// Element kind is a stack-only type: the elements go out as raw bytes, in order
            template <class _T>
            struct __impl<_T, false> {
                /// The element type's own id; the static_assert rejects anything unregistered
                static constexpr BIT32 Type = __stack_only_type<_T>;
                /// size() * sizeof(T)
                static BIT64 impl_size(const std::list<_T>& _data) {
                    static_assert(__stack_only_type<_T> != _void_, "invalid s_list<T>");
                    return _data.size() * sizeof(_T);
                }
                /// One memcpy per element, the list being non-contiguous
                static BIT8* impl_memcpy(const std::list<_T>& _data, BIT8* _ptr) {
                    for (const _T& it : _data) {
                        memcpy(_ptr, &it, sizeof(_T));
                        _ptr += sizeof(_T);
                    }
                    return _ptr;
                }
                /// One append per element
                static bool impl_stream(const std::list<_T>& _data, ostream& _stm) {
                    for (const _T& it : _data)
                        if (!_stm.append(&it, sizeof(_T))) return false;
                    return true;
                }
            };
            typedef __impl<T, std::is_base_of_v<serable, T>> impl;
            static constexpr BIT32 Type{impl::Type << 16 | _list_};

        public:
            /// std::list<T>, bound to its composite id element << 16 | _list_
            typedef serobj<std::list<T>, Type,
                           impl::impl_size,
                           impl::impl_memcpy,
                           impl::impl_stream>
                value;
        };
        /// std::list<T*>: every element writes itself, and the node owns the pointees
        template <class T>
        struct __s_list<T*> {
        private:
            template <typename _T, bool> struct _Type;
            template <typename _T, bool> struct _Type {
                enum : BIT32 { value = _T::_Type };
            };
            template <typename _T> struct _Type<_T, true> {
                enum : BIT32 { value = _any_ };
            };
            static constexpr BIT32 Type{_Type<T, std::is_same_v<T, serable>>::value << 16 | _list_};
            static BIT64 impl_size(const std::list<T*>& _data) {
                BIT64 size{0};
                for (T* const& it : _data) size += it->size();
                return size;
            }
            static BIT8* impl_memcpy(const std::list<T*>& _data, BIT8* _ptr) {
                for (T* const& it : _data) _ptr = it->read(_ptr);
                return _ptr;
            }
            static bool impl_stream(const std::list<T*>& _data, ostream& _stm) {
                for (T* const& it : _data)
                    if (!it->read(_stm)) return false;
                return true;
            }
            static void Destroy(std::list<T*>& _data) {
                for (T*& it : _data) delete it;
                _data.clear();
            }

        public:
            /// std::list<T*>, bound to T's id or to _any_ for a bare serable*; destroying the
            /// node deletes the pointees
            typedef serobj<std::list<T*>, Type,
                           &impl_size,
                           &impl_memcpy,
                           &impl_stream,
                           &Destroy>
                BASE;
            /// The pointees belong to the node, so a copy would leave two nodes deleting them
            struct value : public BASE {
                using BASE::BASE;
                value(const value&) = delete;
                value& operator=(const value&) = delete;
            };
        };

        /**
         * \brief Serializable std::list<T>
         *
         * T is a stack-only type (the elements go out as raw bytes), or a class derived from
         * serable (each element writes itself). A pointer element type takes the specialization
         * that owns its pointees: destroying the node deletes them.
         */
        template <class T>
        class s_list
            : public __s_list<T>::value {
        public:
            /// The serobj this class derives from
            typedef typename __s_list<T>::value BASE;
            using BASE::BASE;
            using BASE::operator=;

            /// Append an element, copying it
            void append(const T& _value) { BASE::data.push_back(_value); }
            /// Append an element, moving it
            void append(T&& _value) { BASE::data.push_back(std::forward<T>(_value)); }
        };

        /// A sequence of bare serable pointers; every element carries its own type id
        typedef s_list<serable*> s_varlst;
    }

    namespace ser
    {
        /// The map a s_varmap payload is made of; ordered, so the write order is the key order
        template <typename K, typename V>
        using __s_varmap_base = std::map<K, V>;

        /// Bind string -> serable* as a varmap payload; the node owns every value
        struct __s_varmap {
        private:
            static BIT64 impl_size(const __s_varmap_base<std::string, serable*>& _data) {
                BIT64 size{0};
                for (const auto& it : _data) size += it.second->size();
                return size;
            }
            static BIT8* impl_memcpy(const __s_varmap_base<std::string, serable*>& _data, BIT8* _ptr) {
                for (const auto& it : _data) _ptr = it.second->read(_ptr);
                return _ptr;
            }
            static bool impl_stream(const __s_varmap_base<std::string, serable*>& _data, ostream& _stm) {
                for (const auto& it : _data)
                    if (!it.second->read(_stm)) return false;
                return true;
            }
            static void Destroy(__s_varmap_base<std::string, serable*>& _data) {
                for (auto& it : _data) delete it.second;
                _data.clear();
            }

        public:
            /// The map, bound to _varmap_; destroying the node deletes every value in it
            typedef serobj<__s_varmap_base<std::string, serable*>, _varmap_,
                           &impl_size,
                           &impl_memcpy,
                           &impl_stream,
                           &Destroy>
                value;
        };

        /**
         * \brief Serializable string -> serable* map, the shape a varmap is written as
         *
         * The node owns every value it holds: destroying it deletes them. That is why copying is
         * deleted; moving hands the pointer over and leaves the source empty.
         */
        class s_varmap
            : public __s_varmap::value {
        public:
            /// The serobj this class derives from
            typedef typename __s_varmap::value BASE;
            using BASE::serobj;
            using BASE::operator=;
            /// Copy construction is deleted: two maps would delete the same values
            s_varmap(const s_varmap&) = delete;
            /// Copy assignment is deleted for the same reason
            s_varmap& operator=(const s_varmap&) = delete;
            /// Move construction: the base hands its pointer over and _v is left empty
            s_varmap(s_varmap&& _v) = default;
            /// Move assignment: the base hands its pointer over and _v is left empty
            s_varmap& operator=(s_varmap&& _v) = default;
            /**
             * \brief Insert a node under its own name, taking ownership of it
             *
             * \param _value Node to own; its name must already be set
             * \return false when the name is taken -- _value is then not stored, and not owned
             */
            bool append(serable* _value) { return BASE::data.insert({_value->get_name(), _value}).second; }
            /// The node held under _index; a key that is absent is inserted as a null pointer
            serable* operator[](const std::string& _index) { return BASE::data[_index]; }
        };
    }
}
#endif