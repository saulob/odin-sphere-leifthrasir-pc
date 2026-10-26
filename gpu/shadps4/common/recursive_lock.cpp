// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <vector>
#include "common/assert.h"
#include "common/recursive_lock.h"

namespace Common::Detail {

struct RecursiveLockState {
    void* mutex;
    RecursiveLockType type;
    int count;
};

// A thread holds very few of these at once: a short array, not a map that allocates a node on the
// first lock and frees it on the last (several times per draw).
thread_local std::vector<RecursiveLockState> g_recursive_locks;

bool IncrementRecursiveLock(void* mutex, RecursiveLockType type) {
    for (auto& state : g_recursive_locks) {
        if (state.mutex == mutex) {
            ASSERT(state.type == type);
            return state.count++ == 0;
        }
    }
    g_recursive_locks.push_back({mutex, type, 1});
    return true;
}

bool DecrementRecursiveLock(void* mutex, RecursiveLockType type) {
    for (size_t i = g_recursive_locks.size(); i-- > 0;) {
        auto& state = g_recursive_locks[i];
        if (state.mutex != mutex) {
            continue;
        }
        ASSERT(state.type == type && state.count > 0);
        if (--state.count == 0) {
            state = g_recursive_locks.back();
            g_recursive_locks.pop_back();
            return true;
        }
        return false;
    }
    ASSERT_MSG(false, "Recursive lock released without being held");
    return false;
}

} // namespace Common::Detail
