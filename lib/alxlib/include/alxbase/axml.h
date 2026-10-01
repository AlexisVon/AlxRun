/*****************************************************************/ /**
 * \file   axml.h
 * \brief  XML document parsing and manipulation
 *
 * \author alexis
 *
 * Copyright (c) 2026 AlexisVon
 * SPDX-License-Identifier: MIT
 *********************************************************************/

#ifndef _ALEXIS_XML_H_
#define _ALEXIS_XML_H_

#include "abase.h"
#include "abytes.h"
#include "ajson.h"
#include "autility.h"

namespace alx {
    /**
     * \brief One XML element: a tag name, its attributes and its content
     *
     * Attributes and content belong to the node: inserting or appending hands the value over, and
     * the destructor deletes the whole subtree. Copying is deep -- a copy shares nothing with the
     * original -- and moving transfers the subtree.
     *
     * A node is standalone: it keeps no link back to the content that holds it, so a node built by
     * hand lives until it is added to a content (which then owns it) or the caller deletes it.
     */
    class ALXBASE_API xml_value {
    public:
        /**
         * \brief What one content entry holds
         *
         * The value drives the union in content_meta: ELEM means the pointer is an xml_value, STRI
         * and above mean it is a std::string, NONE means there is no pointer at all.
         */
        enum content_type : uint_8 {
            /// Nothing held: the pointer is null
            NONE = 0,
            /// A child element
            ELEM = 0X0FU,
            /// A string with no further meaning -- the base of the string range
            STRI = 0XF0U,
            /// A text run of an element; escaped when serialized
            TEXT,
            /// A comment: the body between "<!--" and "-->"
            NOTE,
            /// A processing instruction: the body between "<?" and "?>"
            PI__,
            /// A document type declaration: the body after "<!"
            DTD_,
            /// A CDATA section: the body between "<![CDATA[" and "]]>"
            CDAT,
            /// An entity reference other than the five predefined ones: the name, written "&name;"
            ENTY
        };
        /**
         * \brief One entry of a content list: a tagged pointer to a child element or to a string
         *
         * The entry owns what it points at and deletes it -- the destructor, the copy assignment
         * and set_elem()/set_stri() all release the previous target first -- and a copy duplicates
         * the target instead of sharing it. The tag has to stay in its range: ELEM for a node,
         * STRI or above for a string.
         */
        class content_meta {
        public:
            union {
                /// The pointer, whatever it points at; null when nothing is held
                void* vptr_{nullptr};
                /// The same pointer as a child element -- meaningful when type() is ELEM
                xml_value* elem_;
                /// The same pointer as a string -- meaningful when type() is STRI or above
                std::string* stri_;
            };

        public:
            /// An empty entry: type() is NONE and the pointer is null
            content_meta() = default;
            /// Delete the element or the string held
            ~content_meta() { free(); }
            /// Deep-copy the held element or string
            content_meta(const content_meta& _obj) { copy(_obj); }
            /// Steal the pointer; _obj is left empty
            content_meta(content_meta&& _obj) noexcept : type_(_obj.type_) {
                std::swap(vptr_, _obj.vptr_);
                _obj.type_ = NONE;
            }
            /// Deep-copy; the entry releases what it held before
            content_meta& operator=(const content_meta& _obj) {
                if (this != &_obj) copy(_obj);
                return *this;
            }
            /// Swap with _obj, so each ends up holding what the other held
            content_meta& operator=(content_meta&& _obj) noexcept {
                if (this != &_obj) {
                    std::swap(type_, _obj.type_);
                    std::swap(vptr_, _obj.vptr_);
                }
                return *this;
            }

        public:
            /// The tag of what is held, NONE when the entry is empty
            inline content_type type() const { return type_; }
            /// True when no pointer is held; every empty entry reports it
            inline bool is_null() const { return vptr_ == nullptr; }
            /**
             * \brief Whether the entry holds exactly that kind
             *
             * \param _type Kind to test
             */
            inline bool is_type(const content_type _type) const { return _type == type_; }
            /**
             * \brief Take over an element built elsewhere, releasing the previous target
             *
             * The entry deletes _elem from now on, so the caller must not delete it, and must use
             * the returned pointer rather than its own after this call.
             *
             * \param _elem Element to own; must be heap-allocated, and must not be null, which
             *              would tag the entry as an element that is not one
             * \return _elem, so the call chains into the container
             */
            inline xml_value* set_elem(xml_value* _elem) {
                free();
                type_ = ELEM;
                elem_ = _elem;
                return elem_;
            }
            /// Allocate an element with that name and own it
            inline xml_value* set_elem(const std::string _name) {
                free();
                type_ = ELEM;
                elem_ = new xml_value(_name);
                return elem_;
            }
            /**
             * \brief Copy a string into a new entry of that kind
             *
             * \param _stri Text to store
             * \param _type Kind to tag it with; it must be in the string range (STRI or above) --
             *              a kind below would make the entry free the string as an element
             * \return The stored string; the entry owns it, and writing to it changes what gets
             *         serialized
             */
            inline std::string* set_stri(const std::string _stri, content_type _type) {
                free();
                type_ = _type;
                stri_ = new std::string(_stri);
                return stri_;
            }

        private:
            void free() {
                if (ELEM == type_) delete elem_;
                else if (STRI <= type_) delete stri_;
                type_ = NONE;
                vptr_ = nullptr;
            }
            void copy(const content_meta& _obj) {
                free();
                type_ = _obj.type_;
                if (ELEM == type_) elem_ = new xml_value(*_obj.elem_);
                else if (STRI <= type_) stri_ = new std::string(*_obj.stri_);
            }
            content_type type_{NONE};
        };
        /**
         * \brief The children of an element, in document order
         *
         * Text runs, comments, CDATA sections, processing instructions, entity references and
         * child elements sit in one list, each as its own entry. The list owns every entry: the
         * elements and strings it hands out stay alive until clear() drops them or the content
         * dies, and copying a content duplicates all of them deeply. Entries are appended as they
         * come -- nothing is merged, split or reordered.
         */
        class ALXBASE_API content {
        public:
            friend class xml_value;
            friend class xml_object;

        public:
            /// An element with no content
            content() = default;
            /// Drop every entry, deleting the elements and strings owned
            ~content() { clear(); }
            /// Deep-copy every entry
            content(const content& _obj) : v_(_obj.v_) {}
            /// Steal every entry; _obj is left empty
            content(content&& _obj) noexcept : v_(std::move(_obj.v_)) {}
            /// Deep-copy; the entries held before are dropped
            content& operator=(const content& _obj) {
                if (&_obj != this) {
                    v_ = _obj.v_;
                }
                return *this;
            }
            /// Steal every entry; the entries held before are dropped
            content& operator=(content&& _obj) noexcept {
                if (&_obj != this) {
                    v_ = std::move(_obj.v_);
                }
                return *this;
            }

        public:
            /// Drop every entry, deleting the elements and strings owned
            inline void clear() { v_.clear(); }
            /// True when there is not a single entry, text runs included
            inline bool empty() const { return v_.empty(); }
            /// Number of entries -- child elements and loose text alike
            inline uint_64 size() const { return v_.size(); }
            /// The raw entries; a reference into the content, valid until it changes
            inline const std::list<content_meta>& container() const { return v_; }

        public:
            /// Append an entry built elsewhere, taking it over
            inline void add_meta(content_meta&& _meta) { v_.push_back(std::move(_meta)); }
            /**
             * \brief Append an element built elsewhere, taking it over
             *
             * \param _elem Element to own; it must not be null and must not be deleted, or reused
             *              as a separate node, afterwards
             * \return _elem, the node now owned by this content
             */
            inline xml_value* add_elem(xml_value* _elem) {
                content_meta m;
                m.set_elem(_elem);
                v_.push_back(std::move(m));
                return _elem;
            }
            /**
             * \brief Build and append a child element
             *
             * \param _name Tag name of the child
             * \param _attr Attributes to set on it, in order; a repeated key keeps the last value
             * \param _text Text runs to give it, appended in order
             * \return The new child, owned by this content
             */
            inline xml_value* add_elem(const std::string& _name,
                                       const std::list<std::pair<std::string, std::string>>& _attr = {},
                                       const std::list<std::string>& _text = {}) {
                content_meta m;
                xml_value* elem = m.set_elem(_name);
                v_.push_back(std::move(m));
                for (const auto& it : _attr) elem->attr().insert(it.first, it.second);
                for (const auto& it : _text) elem->cont().add_text(it);
                return elem;
            }
            /// Append a text run, escaped when serialized; the returned string is writable in place
            inline std::string* add_text(const std::string& _text) { return add_stri(_text, content_type::TEXT); }
            /// Append a comment, serialized verbatim between "<!--" and "-->"
            inline std::string* add_note(const std::string& _note) { return add_stri(_note, content_type::NOTE); }
            /// Append a processing instruction, serialized verbatim between "<?" and "?>"
            inline std::string* add_pi__(const std::string& _pi__) { return add_stri(_pi__, content_type::PI__); }
            /// Append a CDATA section, serialized verbatim between "<![CDATA[" and "]]>"
            inline std::string* add_cdat(const std::string& _cdat) { return add_stri(_cdat, content_type::CDAT); }
            /// Append an entity reference, serialized as "&name;"
            inline std::string* add_enty(const std::string& _enty) { return add_stri(_enty, content_type::ENTY); }
            /**
             * \brief Collect the child elements of that name
             *
             * Direct children only, compared by exact name, in document order.
             *
             * \param _name Tag name to look for
             * \return The nodes, which this content still owns; empty when none match
             */
            std::list<xml_value*> get_elem(const std::string& _name);
            /// The const overload: read-only nodes, owned as above
            std::list<const xml_value*> get_elem(const std::string& _name) const;

        private:
            inline std::string* add_stri(const std::string& _stri, content_type _type) {
                content_meta m;
                std::string* stri = m.set_stri(_stri, _type);
                v_.push_back(std::move(m));
                return stri;
            }
            std::list<content_meta> v_;
        };
        /**
         * \brief The attributes of a tag: name to value, each name at most once
         *
         * Backed by an unordered map, so the attributes come out in no particular order: the same
         * document parsed or edited twice may serialize with its attributes in another order. A
         * name that was never set reads as an empty string.
         */
        class attribute {
        public:
            friend class xml_value;

        public:
            /// Drop every attribute
            inline void clear() { v_.clear(); }
            /// True when the tag carries no attribute
            inline bool empty() const { return v_.empty(); }
            /// Remove _key; no-op when it is absent
            inline void remove(const std::string& _key) { v_.erase(_key); }
            /**
             * \brief Set a value, overwriting the one _key had
             *
             * \param _key Attribute name
             * \param _val Value; escaped when serialized
             * \return *this, so calls chain
             */
            inline attribute& insert(const std::string& _key, const std::string& _val) {
                v_[_key] = _val;
                return *this;
            }
            /// True when _key was set, even to an empty value
            inline bool contain(const std::string& _key) const {
                auto iter = v_.find(_key);
                return iter != v_.end();
            }
            /// The value of _key, or an empty string when it is absent
            inline std::string value(const std::string& _key) const { return alx::map_value(v_, _key); }
            /// The underlying map; a reference into the attribute, valid until it changes
            inline const std::unordered_map<std::string, std::string>& container() const { return v_; }

        private:
            std::unordered_map<std::string, std::string> v_;
        };

    public:
        /**
         * \brief Build an element with that name and nothing in it
         *
         * \param _name Tag name; an empty one leaves the node invalid -- see valid() -- and it
         *              still serializes, as an empty tag
         */
        xml_value(const std::string& _name);
        /// Delete the node together with the elements and strings its content owns
        ~xml_value();
        /// Deep copy: the attributes and the whole subtree are duplicated
        xml_value(const xml_value& _obj) : name_(_obj.name_), attr_(_obj.attr_), cont_(_obj.cont_) {}
        /// Steal the name, attributes and content; _obj is left invalid
        xml_value(xml_value&& _obj) noexcept : name_(std::move(_obj.name_)), attr_(std::move(_obj.attr_)), cont_(std::move(_obj.cont_)) {}
        /// Deep copy; the attributes and content held before are dropped
        xml_value& operator=(const xml_value& _obj) {
            if (&_obj != this) {
                name_ = _obj.name_;
                attr_ = _obj.attr_;
                cont_ = _obj.cont_;
            }
            return *this;
        }
        /// Steal the name, attributes and content, leaving _obj without them
        xml_value& operator=(xml_value&& _obj) noexcept {
            if (&_obj != this) {
                name_ = std::move(_obj.name_);
                attr_ = std::move(_obj.attr_);
                cont_ = std::move(_obj.cont_);
            }
            return *this;
        }

    public:
        /// True when the node carries a name; a node built from an empty name is invalid
        inline bool valid() const { return !name_.empty(); }
        /// The tag name, empty when the node is invalid
        inline const std::string& name() const { return name_; };
        /// The attributes of the tag
        inline const attribute& attr() const { return attr_; };
        /// The content of the tag
        inline const content& cont() const { return cont_; };
        /// Mutable name: renaming a node changes the name get_elem() matches it by
        inline std::string& name() { return name_; };
        /// Mutable attributes: insert() and remove() apply to this node
        inline attribute& attr() { return attr_; };
        /// Mutable content: add_text(), add_elem() and the rest append to this node
        inline content& cont() { return cont_; };
        /**
         * \brief Serialize this node and everything below it
         *
         * The pretty form, the default, indents two spaces per nesting level and ends each line
         * with '\n'; the compact form writes no whitespace between the tags and no trailing
         * newline. Either way a node with no content becomes a self-closing tag, and a tag whose
         * only child is text stays on one line. Text runs and attribute values are escaped (see
         * escape()); the bodies of comments, CDATA sections, instructions and declarations are
         * written as stored. Nothing is validated on the way out.
         *
         * \param _compact true for the compact form
         * \return The serialized bytes; an invalid node yields an empty tag, "</>" in the compact
         *         form and "< />" in the pretty one
         */
        bytes to_bytes(bool _compact = false) const;
        /**
         * \brief This element as JSON, the shape the JSON bridge reads back
         *
         * A node with no attribute and a single text child becomes that text as a JSON string.
         * Any other node becomes an object: one entry per attribute under "@_<name>", one under
         * "#text" for the text runs, one per child element name, and "#note", "#pi__" and
         * "#cdat" for the rest. A group of one is stored as the value itself, a group of several
         * as an array. Entity references become "#enty"; a declaration (DTD_) is dropped here --
         * only xml_object::to_json() keeps it. The node's own name is not part of the result:
         * the parent that holds the node supplies the key.
         */
        json_value to_json() const;

    protected:
        /**
         * \brief The recursive half of to_bytes(), one call per node
         *
         * \param _indent_level Nesting depth -- two spaces per level -- or max_uint_64, which
         *                      selects the compact form and is passed down unchanged
         */
        bytes to_bytes_impl(uint_64 _indent_level) const;
        /// Trim leading and trailing whitespace in place; an all-blank string becomes empty
        static void skip_blanks(std::string& _str);
        /**
         * \brief Read an XML name starting at _from
         *
         * A name starts with a letter, '_' or ':' and continues with letters, digits, '_', '-',
         * '.' or ':'; it ends at whitespace, '>', '/' or '=', which _next is left on.
         *
         * \param _stream Document being parsed
         * \param _from Offset the name starts at
         * \param _next Set to the first character after the name
         * \return The name, or an empty string when none starts here or the stream ends inside it
         */
        static std::string read_name(const bytes_view& _stream, const uint_64 _from, uint_64& _next);
        /**
         * \brief Parse the four tag-shaped nodes: instruction, comment, CDATA and declaration
         *
         * _from must sit on the '<'. The stored string is the body of the construct: between "<?"
         * and "?>", "<!--" and "-->", "<![CDATA[" and "]]>", and after "<!" for a declaration.
         * _next is left on the '<' of the node that follows, or on uint_64_npos when the
         * construct ends the stream.
         *
         * \param _meta Entry to store the body in, tagged with the kind found
         * \param _stream Document being parsed
         * \param _from Offset of the '<'
         * \param _next Set to the next '<'
         * \param _skip_blanks true to trim the body of a comment
         * \return false when the construct is truncated, unterminated or not one of the four
         */
        static bool from_bytes_stri(content_meta& _meta, const bytes_view& _stream, const uint_64 _from, uint_64& _next, bool _skip_blanks);
        /**
         * \brief Parse one element -- "<name attrs>children</name>" or "<name attrs/>"
         *
         * _from must sit on the '<'. The name overwrites the one _xml had; attributes and
         * children are appended to whatever it already holds, so it is meant to be empty. _next
         * is left on the '<' of the node that follows the element, or on uint_64_npos when the
         * element closes the stream -- which is how the caller learns the document is complete.
         *
         * \param _xml Node to fill
         * \param _stream Document being parsed
         * \param _from Offset of the '<'
         * \param _next Set to the next '<'
         * \param _skip_blanks true to trim text runs while parsing the children
         * \return false when the tag is malformed, unterminated or closed by another name
         */
        static bool from_bytes_elem(xml_value& _xml, const bytes_view& _stream, const uint_64 _from, uint_64& _next, bool _skip_blanks);

    protected:
        /**
         * \brief Parse one name="value" attribute into the attributes of _xml
         *
         * The value must be quoted, with ' or ". A name the node already has is refused: XML
         * allows an attribute only once, and the refusal fails the whole parse.
         *
         * \param _xml Node to add the attribute to
         * \param _stream Document being parsed
         * \param _from Offset the name starts at
         * \param _next Set past the closing quote
         * \return false when there is no '=', the value is unquoted or unterminated, or the name
         *         repeats -- _next then says how far the parse got, which is how the caller tells
         *         "no attribute here" from "a broken one"
         */
        static bool from_bytes_attr(xml_value& _xml, const bytes_view& _stream, const uint_64 _from, uint_64& _next);
        /**
         * \brief Parse the content of an element, up to its closing tag
         *
         * Text runs become TEXT entries, with the five predefined references -- &lt; &gt; &amp;
         * &quot; &apos; -- expanded; any other reference becomes an ENTY entry of its own. Runs
         * of text and of references are separate entries, never merged. _next is left on the '<'
         * of the closing tag, for the caller to consume.
         *
         * \param _xml Node whose content is filled
         * \param _stream Document being parsed
         * \param _from Offset just past the '>' of the opening tag
         * \param _next Set to the '<' of the closing tag
         * \param _skip_blanks true to trim each text run and drop the blank ones, so the
         *                     indentation of a pretty document does not enter the tree; false to
         *                     keep every character as data
         * \return false when a reference is unterminated or a child element is malformed
         */
        static bool from_bytes_cont(xml_value& _xml, const bytes_view& _stream, const uint_64 _from, uint_64& _next, bool _skip_blanks);

    protected:
        /// Tag name; empty makes the node invalid
        std::string name_;
        /// Attributes of the tag, at most one value per name
        attribute attr_;
        /// Content of the tag, in document order
        content cont_;

    public:
        /**
         * \brief Escape the five XML metacharacters into entity references
         *
         * '&', '<', '>', '"' and '\'' become &amp; &lt; &gt; &quot; and &apos;; every other
         * character is copied through. The serializer applies this to text runs and attribute
         * values, and to nothing else.
         *
         * \param _str Text to escape
         * \return The escaped text, empty when _str is empty
         */
        static std::string escape(const std::string& _str);
    };

    /**
     * \brief A whole XML document: the root element plus what surrounds it
     *
     * Everything xml_value is -- name, attributes, content -- describes the root element; the
     * global content holds the nodes outside it, in the order they were read: the XML
     * declaration, the document type declaration and the comments between them.
     */
    class ALXBASE_API xml_object
        : public xml_value {
    public:
        /// An empty document: no root name yet, so valid() is false
        xml_object() : xml_object(std::string()) {}
        /**
         * \brief A document whose root element carries that name and nothing else
         *
         * \param _name Tag name of the root element; an empty one leaves the document invalid
         */
        xml_object(const std::string& _name);
        /// Delete the root element together with the global content
        ~xml_object();
        /// Deep copy: root element and global content are duplicated
        xml_object(const xml_object& _obj) : xml_value(_obj), gcont_(_obj.gcont_) {}
        /// Steal both parts; _obj is left invalid
        xml_object(xml_object&& _obj) noexcept : xml_value(std::move(_obj)), gcont_(std::move(_obj.gcont_)) {}
        /// Deep copy; the previous root element and global content are dropped
        xml_object& operator=(const xml_object& _obj) { return xml_value::operator=(_obj), gcont_ = _obj.gcont_, *this; }
        /// Steal both parts; the previous root element and global content are dropped
        xml_object& operator=(xml_object&& _obj) noexcept { return xml_value::operator=(std::move(_obj)), gcont_ = std::move(_obj.gcont_), *this; }

    public:
        /**
         * \brief The nodes outside the root element, in document order
         *
         * What add_note(), add_pi__() and add_dtd_() append to, and what parsing collects before
         * the root element. Only those three kinds are meaningful here: the serializer emits
         * them and skips every other entry, and so does to_json().
         */
        inline const content& gcont() const { return gcont_; };
        /// Append a document-level comment; the returned string is writable in place
        inline std::string* add_note(const std::string& _note) { return gcont_.add_stri(_note, content_type::NOTE); }
        /// Append a processing instruction, the XML declaration among them
        inline std::string* add_pi__(const std::string& _pi__) { return gcont_.add_stri(_pi__, content_type::PI__); }
        /**
         * \brief Append a document type declaration
         *
         * \param _dtd_ Declaration body, the text written between "<!" and ">": add_dtd_("DOCTYPE
         *              html") yields <!DOCTYPE html>
         */
        inline std::string* add_dtd_(const std::string& _dtd_) { return gcont_.add_stri(_dtd_, content_type::DTD_); }

    public:
        /**
         * \brief The whole document as JSON: the global content, then the root element
         *
         * The "#pi__", "#dtd_" and "#note" entries of the global content come first, each stored
         * as the value alone or, for several of a kind, as an array; then one entry keyed by the
         * root element's own name holding xml_value::to_json() of it. Not virtual: it hides
         * xml_value::to_json(), so the same call through an xml_value& returns the root element
         * alone, without the global content.
         */
        json_object to_json() const;
        /**
         * \brief Serialize the whole document: the global content first, then the root element
         *
         * The PI, DTD_ and NOTE entries of the global content are written in order, each on its
         * own line in the pretty form; any other entry of it is skipped. Not virtual: it hides
         * xml_value::to_bytes(), so the same call through an xml_value& serializes the root
         * element alone.
         *
         * \param _compact true for the compact form, one line with no trailing newline
         */
        bytes to_bytes(bool _compact = false) const;
        /**
         * \brief Parse a document
         *
         * Instructions, declarations and comments are collected until the root element, which is
         * then parsed; afterwards the stream must be exhausted -- trailing text without a '<' is
         * accepted and dropped. The parse is strict: an unterminated construct, an unclosed or
         * mismatched tag, an unquoted or repeated attribute all fail it, and nothing half-parsed
         * survives -- the result is then a default-constructed, invalid document.
         *
         * \param _xml Bytes to parse
         * \param _skip_blanks true to trim text runs and drop the blank ones, so the indentation
         *                     of a pretty document does not enter the tree
         * \param _ok Optional flag, set on every path: true only when the document parsed
         * \return The parsed document, or an invalid one when the parse failed
         */
        static xml_object from_bytes(const bytes_view& _xml, bool _skip_blanks = true, bool* _ok = nullptr);

    private:
        content gcont_;
    };
}

#endif
