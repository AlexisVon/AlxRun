/*****************************************************************/ /**
 * \file   acache.h
 * \brief  Cache management (LRU and other caching strategies)
 *
 * \author alexis
 *
 * Copyright (c) 2026 AlexisVon
 * SPDX-License-Identifier: MIT
 *********************************************************************/

#ifndef _ALEXIS_CACHE_H_
#define _ALEXIS_CACHE_H_

#include "afactory.h"
#include "athread_safe.h"
#include "autility.h"
#include "avariant.h"

namespace alx {
    /**
     * \brief Cache base: key/value entries under a strategy that decides what to drop
     *
     * The capacity is a hard bound fixed at construction. A put() of a new key into a full cache
     * evicts one entry first, and the strategy picks which: fifo the one put longest ago, lru the
     * one least recently put or read, lfu the one used least, with ties going to the least
     * recently used. A put() of an existing key overwrites the value and counts as a use.
     *
     * Values leave by copy -- get() fills the caller's variant -- so remove() and clear() cannot
     * invalidate a value a caller already holds. put(), get(), remove() and clear() serialize on
     * the entity's own lock; size() reads the count without it.
     */
    class ALXCORE_API cache {
    public:
        /**
         * \brief Build an entity from a registered strategy name
         *
         * \param _type Name listed by cache_pool::list_types(); an unregistered name yields
         *              nullptr
         * \param _capacity Upper bound on the entries the entity may hold; must be at least 1
         * \return A new entity the caller owns and deletes
         */
        static cache* create(const std::string& _type, uint_64 _capacity);

    public:
        /// Fix the capacity bound for the entity's lifetime; must be at least 1
        cache(uint_64 _capacity) : m_capacity(_capacity) {};
        /// Virtual: entities from create() are deleted through this pointer
        virtual ~cache() = default;
        /**
         * \brief Store a value under a key, replacing any value already there
         *
         * On a full cache one entry is evicted first, so the entry count never exceeds
         * capacity().
         *
         * \param _value Copied in; the entity keeps its own copy
         */
        virtual void put(const std::string& _key, const variant& _value) = 0;
        /**
         * \brief Look a key up
         *
         * A hit counts as a use and refreshes the entry's place in the eviction order.
         *
         * \param _out Receives a copy of the value on a hit; left untouched on a miss
         * \return false when the key is absent
         */
        virtual bool get(const std::string& _key, variant& _out) = 0;
        /// Drop one key; no-op when it is absent
        virtual void remove(const std::string& _key) = 0;
        /// Drop every entry, keeping the capacity
        virtual void clear() = 0;
        /// Entries held right now, read without taking the entity's lock
        virtual uint_64 size() const = 0;
        /// The bound given at construction; never changes
        virtual uint_64 capacity() const { return m_capacity; }

    public:
        /// Name of the concrete strategy, as accepted by create()
        virtual const char* type_name() const = 0;
        /**
         * \brief Clone the entity
         *
         * \return A new entity of the same strategy and capacity holding a copy of every entry
         *         in the same eviction order; the caller owns it
         */
        virtual cache* copy_entity() const = 0;
        /// A new empty entity of the same strategy with that capacity; the caller owns it
        virtual cache* new_entity(uint_64 _capacity) const = 0;

    protected:
        /// Capacity bound, fixed by the constructor
        const uint_64 m_capacity;
    };

    /**
     * \brief Named cache entities: build, copy, share and drop them by name
     *
     * The pool owns every entity stored in it -- they are deleted on destruction, on
     * remove_entity(), and whenever create_entity()/copy_entity()/new_entity()/add_entity()
     * reuses a name. One entity belongs to the pool under one name only, and none ever leaves
     * it: callers name an entity, and get values back by copy.
     *
     * Reached through the process-wide instance() of single<>. Safe to call from any thread: the
     * pool's own lock serializes everything that touches the entity set.
     */
    class ALXCORE_API cache_pool
        : public alx::single<cache_pool> {
    public:
        /// Delete every entity still held
        ~cache_pool();
        /**
         * \brief Build an entity from a registered strategy name and store it under a name
         *
         * \param _key Name to store it under; an entity already there is deleted
         * \param _type Strategy name, as listed by list_types()
         * \param _capacity Upper bound on the entries the entity may hold; must be at least 1
         * \return false when _type is not registered, leaving the pool unchanged
         */
        bool create_entity(const std::string& _key, const std::string& _type, uint_64 _capacity);
        /**
         * \brief Clone an entity under a second name
         *
         * \param _key Entity to clone
         * \param _new Name for the clone; an entity already there is deleted
         * \return false when _key is absent, and nothing is stored then
         */
        bool copy_entity(const std::string& _key, const std::string& _new);
        /**
         * \brief Take over an entity built by the caller
         *
         * \param _key Name to store it under; an entity already there is deleted
         * \param _entity Deleted by the pool from then on; it must not be an entity the pool
         *                already owns
         */
        void add_entity(const std::string& _key, cache* _entity);
        /**
         * \brief Store an empty entity of the same strategy under a second name
         *
         * \param _key Entity whose strategy is reused
         * \param _new Name for the new entity; an entity already there is deleted
         * \param _capacity Upper bound on the entries the new entity may hold
         * \return false when _key is absent, and nothing is stored then
         */
        bool new_entity(const std::string& _key, const std::string& _new, uint_64 _capacity);
        /// True when an entity is stored under that name
        bool contain_entity(const std::string& _key) const;
        /// Drop the entity under that name and delete it; no-op when there is none
        void remove_entity(const std::string& _key);
        /// Drop every entry of that entity, keeping the entity and its capacity; no-op when the
        /// name is absent
        void clear_entity(const std::string& _key);
        /// Names of the entities held, in unspecified order
        std::list<std::string> list_entitys() const;
        /// Strategy names that create() and create_entity() accept, in unspecified order
        std::list<std::string> list_types() const;

    public:
        /**
         * \brief Store a value in the named entity
         *
         * \return false only when no entity is stored under _entity
         */
        bool put(const std::string& _entity, const std::string& _key, const variant& _value);
        /**
         * \brief Look a key up in the named entity, filling the miss if asked to
         *
         * \param _out Receives the value found, or the one _new produced
         * \param _new Fill for a miss: called, stored in the entity like a put(), then copied to
         *             _out. Null leaves the miss unfilled. It runs with the pool locked, so it
         *             must not call back into the pool
         * \return false when _entity is absent, or when the key is absent and _new is null
         */
        bool get(const std::string& _entity, const std::string& _key, variant& _out, std::function<variant()> _new = nullptr);
        /// Drop one entry of that entity; no-op when the entity or the key is absent
        void remove(const std::string& _entity, const std::string& _key);
        /// Entry count of that entity; 0 when no entity is stored under the name
        uint_64 size(const std::string& _entity) const;
        /// Capacity of that entity; 0 when no entity is stored under the name
        uint_64 capacity(const std::string& _entity) const;

    protected:
        /// Entity table keyed by name; owns and deletes the entities stored in it
        safe_map<std::string, cache> pool;
    };
}

#endif