/*****************************************************************/ /**
 * \file   adatetime.h
 * \brief  Date and time processing
 *
 * \author alexis
 *
 * Copyright (c) 2026 AlexisVon
 * SPDX-License-Identifier: MIT
 *********************************************************************/

#ifndef _ALEXIS_DATETIME_H_
#define _ALEXIS_DATETIME_H_

#include "autility.h"
#include <chrono>
#include <ctime>

namespace alx {
    /**
     * \brief A point in time on the wall clock, reported in local time
     *
     * The stored instant comes from std::chrono::system_clock -- the wall clock, the same source
     * as to_time_t() and file timestamps, not a monotonic one, so an NTP or manual step of the
     * clock shows up in every comparison and in the elapsed_*() family. Calendar fields are read
     * in the machine's local zone on each call (localtime_r / localtime_s), which is also what
     * makes the getters and to_string() safe to call from several threads.
     */
    class ALXBASE_API datetime {
    private:
        using clock = std::chrono::system_clock;
        using time_point = std::chrono::time_point<clock>;
        time_point m_tp;
        std::tm get_tm() const {
            time_t now_time_t = std::chrono::system_clock::to_time_t(m_tp);
            std::tm tm_time{};
#ifdef _WIN32
            localtime_s(&tm_time, &now_time_t);
#else
            localtime_r(&now_time_t, &tm_time);
#endif
            return tm_time;
        }

    public:
        /// The current wall-clock time; the same instant a default-constructed instance holds
        static inline datetime current() { return datetime(); }
        /// Take the current wall-clock time
        datetime() : m_tp(clock::now()) {}
        /// Wrap an existing std::chrono::system_clock instant
        datetime(const time_point& _time) : m_tp(_time) {}
        /**
         * \brief Build an instant from calendar fields, read as local time
         *
         * _y is a full year (2026, not 126), _m a 1-12 month, _d a day of the month, then hour,
         * minute, second, millisecond and microsecond, the last two added past the whole second.
         * An out-of-range field is normalized by mktime(), so month 13 or day 32 rolls into the
         * next period, and the DST flag is left clear, so in a DST zone a summer timestamp is
         * taken as standard time and its fields read back one hour later.
         */
        datetime(uint_16 _y, uint_16 _m, uint_16 _d,
                 uint_16 _h = 0, uint_16 _min = 0, uint_16 _s = 0,
                 uint_16 _ms = 0, uint_16 _us = 0);
        /// Year in local time, e.g. 2026
        inline uint_16 year() const { return get_tm().tm_year + 1900; }
        /// Month in local time, 1-12
        inline uint_16 month() const { return get_tm().tm_mon + 1; }
        /// Day of the month in local time, 1-31
        inline uint_16 day() const { return get_tm().tm_mday; }
        /// Hour in local time, 0-23
        inline uint_16 hour() const { return get_tm().tm_hour; }
        /// Minute in local time, 0-59
        inline uint_16 minute() const { return get_tm().tm_min; }
        /// Second in local time, 0-59
        inline uint_16 second() const { return get_tm().tm_sec; }
        /**
         * \brief Sub-second part of the stored instant, in milliseconds
         *
         * 0-999, read from the instant itself rather than from the local-time fields. Before 1970
         * the remainder is negative and wraps around the unsigned return value.
         */
        uint_16 millisecond() const;
        /**
         * \brief Sub-second part of the stored instant, in microseconds
         *
         * 0-999999, the whole sub-second part: 1500 us reads as microsecond() 1500 together with
         * millisecond() 1, so this is not the sub-millisecond field the name pair suggests. The
         * same pre-1970 wrap as millisecond() applies.
         */
        uint_32 microsecond() const;

        /// A copy shifted by secs seconds (negative shifts back); this instance is unchanged
        inline datetime add_seconds(int secs) const {
            return datetime(m_tp + std::chrono::seconds(secs));
        }
        /// A copy shifted by msecs milliseconds (negative shifts back); this instance is unchanged
        inline datetime add_milliseconds(int msecs) const {
            return add_seconds(msecs / 1000).add_microseconds((msecs % 1000) * 1000);
        }
        /// A copy shifted by usecs microseconds (negative shifts back); this instance is unchanged
        inline datetime add_microseconds(int usecs) const {
            return datetime(m_tp + std::chrono::microseconds(usecs));
        }

        /**
         * \brief Format as "YYYY-MM-DD HH:MM:SS.mmm" in local time
         *
         * The calendar fields are converted one at a time, so a second boundary crossed during
         * the call can leave the string holding two different instants.
         */
        std::string to_string() const;
        /// Replace the stored instant with the current one, making it the origin of elapsed_*()
        inline void start() { m_tp = clock::now(); }
        /**
         * \brief Milliseconds from the stored instant to now
         *
         * The stored instant is the one taken at construction, by the explicit constructor or by
         * the last start(). The difference is taken on the wall clock and returned unsigned, so an
         * instant ahead of now -- or a step of the clock backwards, as NTP may cause -- wraps to a
         * huge value instead of coming out negative.
         */
        uint_64 elapsed_ms() const;
        /// The same wall-clock difference in microseconds; see elapsed_ms() for the caveat
        uint_64 elapsed_us() const;
        /// The same wall-clock difference in nanoseconds; see elapsed_ms() for the caveat
        uint_64 elapsed_ns() const;
    };
}

#endif