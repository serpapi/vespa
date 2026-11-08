// Copyright Vespa.ai. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.

#pragma once

#include "ireplaypackethandler.h"

#include <vespa/searchlib/transactionlog/common.h>

#include <memory>

namespace document {
class DocumentTypeRepo;
}

namespace proton {

class FeedOperation;
/**
 * Utility class that deserializes packet entries into feed operations
 * during replay from the transaction log and dispatches the feed operations
 * to a given handler class.
 */
class ReplayPacketDispatcher {
private:
    using Packet = search::transactionlog::Packet;
    IReplayPacketHandler& _handler;

    template <typename OperationType>
    void replay(OperationType& op, vespalib::nbostream& is, const Packet::Entry& entry);

protected:
    virtual void store(const FeedOperation& op);

public:
    ReplayPacketDispatcher(IReplayPacketHandler& handler);
    virtual ~ReplayPacketDispatcher();

    void replayEntry(const Packet::Entry& entry);

    /**
     * Deserializes a put, update or remove entry without side effects, so that it can run on any thread.
     * Returns nullptr for every other entry type; those go through replayEntry().
     */
    static std::unique_ptr<FeedOperation> decode(const Packet::Entry& entry, const document::DocumentTypeRepo& repo);
    // Replays an operation that decode() returned.
    void dispatch(const FeedOperation& op);
};

} // namespace proton
