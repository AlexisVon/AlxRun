// Copyright (c) 2026 AlexisVon
// SPDX-License-Identifier: Apache-2.0

#ifndef ALXRUN_CAPABILITY_STOP_H
#define ALXRUN_CAPABILITY_STOP_H

#include <atomic>

/// The wall's flag: the kernel's watchdog sets it when a run's window is used up, and a capability
/// that can be stopped mid-native-call reads it (the regex callout is the only reader today).
namespace capability {
    inline std::atomic<bool> g_stop{false};
} // namespace capability

#endif // ALXRUN_CAPABILITY_STOP_H
