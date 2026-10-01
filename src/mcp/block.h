// Copyright (c) 2026 AlexisVon
// SPDX-License-Identifier: Apache-2.0

#ifndef ALXRUN_MCP_BLOCK_H
#define ALXRUN_MCP_BLOCK_H

#include "config.h"

#include <cstddef>
#include <cstdint>

/// The supervisor<->child hand-off (design.md §4): one mapping, one write each way, sizes fixed up
/// front, and a crash still leaves whatever was written.
///
///   [0..4)   out_len    ring bytes valid; the reply begins where they end (kDataOff + out_len)
///   [4..8)   ring_pos   where the next printed byte goes
///   [8..12)  dropped    bytes off the ring's front (dropped + out_len = printed)
///   [12..16) req_len    the parent writes it with the request
///   [16..20) reply_len  written last: non-zero *is* the commit; 0 = no reply, just a fragment
///   [20..24) wall_ms    the window granted -- one number, both clocks (this side kills at +1 s)
///   [24..64) reserved   zeroed by the parent; room for the next resource dimension
///   [64..)   the payload: the ring [kDataOff, kPrint), then the reply slot [kPrint, end); the
///            header rides in the ring's first cache line, so the mapping *is* the dimension.
///
/// The reply streams in where the log ends: a short log lends it the ring it left empty, and the
/// log is never written over -- that is what lets the parent hand it back whole on a mid-reply
/// death. Counters, not one slot: a single slot cannot tell "no reply" from its own request still
/// sitting there (measured).
namespace mcp {
    namespace block {

        constexpr size_t kHeadBytes = 64; // one cache line: the header never shares a line with the payload
        constexpr size_t kBytes = static_cast<size_t>(config::kBlockMb) << 20;
        constexpr size_t kPrint = kBytes / 8; // the ring's region; kRingBytes below is what it holds
        constexpr size_t kReply = kBytes - kPrint; // the reply's reservation: the rest of the block
        constexpr size_t kRingBytes = kPrint - kHeadBytes; // the ring's own span, header excluded
        constexpr size_t kDataOff = kHeadBytes;
        constexpr size_t kMaxPayload = kBytes - kDataOff;
        constexpr size_t kOutOff = 0; // out_len
        constexpr size_t kRingPosOff = kOutOff + sizeof(uint32_t); // ring_pos
        constexpr size_t kDroppedOff = kRingPosOff + sizeof(uint32_t); // dropped
        constexpr size_t kReqOff = kDroppedOff + sizeof(uint32_t); // req_len
        constexpr size_t kReplyLenOff = kReqOff + sizeof(uint32_t); // reply_len
        constexpr size_t kWallOff = kReplyLenOff + sizeof(uint32_t); // wall_ms
        static_assert(kHeadBytes % sizeof(uint32_t) == 0, "the header must be u32-aligned");
        // The ring is the whole of the log's reservation: below a megabyte it is a look at nothing.
        static_assert(kPrint >= (1u << 20), "the ring needs at least 1 MB: config::kBlockMb >= 8");
        // Why an eighth: the reply is a value's transport, the ring a look at where the run got to.
        // A full log then still leaves the value three quarters of the reply slot.
        static_assert(4 * kPrint <= kReply, "the ring may be at most a quarter of the reply");

    } // namespace block
} // namespace mcp

#endif // ALXRUN_MCP_BLOCK_H
