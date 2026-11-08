// Copyright Vespa.ai. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.

#pragma once

#include <cstdint>
#include <map>
#include <vector>

namespace searchcorespi::index {

/**
 * Specifies a set of disk index ids for fusion.
 *
 * Note: All ids in FusionSpec are absolute ids.
 **/
struct FusionSpec {
    uint32_t              last_fusion_id;
    std::vector<uint32_t> flush_ids;
    // Flush ids merged away by an intermediate fusion, mapped to the flush id that now holds their
    // documents. A source selector saved before that merge can still name them.
    std::map<uint32_t, uint32_t> merged_into;

    FusionSpec() : last_fusion_id(0), flush_ids(), merged_into() {}
};

} // namespace searchcorespi::index
