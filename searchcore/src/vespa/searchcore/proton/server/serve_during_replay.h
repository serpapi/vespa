// Copyright Vespa.ai. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.
#pragma once

#include <cstdlib>
#include <cstring>

namespace proton {

/*
 * VESPA_SERVE_DURING_REPLAY=1: proton goes online and starts the service layer as soon as its components are
 * loaded, while the transaction log tail replays. Queries see the flushed state plus replayed operations as
 * they are committed; external feed operations get a transient error until replay is done.
 */
inline bool serve_during_replay() noexcept {
    static const bool value = [] {
        const char* env = std::getenv("VESPA_SERVE_DURING_REPLAY");
        return env != nullptr && std::strcmp(env, "1") == 0;
    }();
    return value;
}

} // namespace proton
