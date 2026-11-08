// Copyright Vespa.ai. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.

#include "replaypacketdispatcher.h"

#include <vespa/document/util/serializableexceptions.h>
#include <vespa/searchcore/proton/feedoperation/operations.h>

using vespalib::IllegalStateException;
using vespalib::make_string;

namespace proton {

template <typename OperationType>
void ReplayPacketDispatcher::replay(OperationType& op, vespalib::nbostream& is, const Packet::Entry& entry) {
    op.deserialize(is, _handler.getDeserializeRepo());
    op.setSerialNum(entry.serial());
    store(op);
    _handler.replay(op);
}

ReplayPacketDispatcher::ReplayPacketDispatcher(IReplayPacketHandler& handler) : _handler(handler) {
}

void ReplayPacketDispatcher::replayEntry(const Packet::Entry& entry) {
    vespalib::nbostream is(entry.data().c_str(), entry.data().size());
    switch (entry.type()) {
    case FeedOperation::PUT: {
        PutOperation op;
        replay(op, is, entry);
        break;
    }
    case FeedOperation::REMOVE: {
        RemoveOperationWithDocId op;
        replay(op, is, entry);
        break;
    }
    case FeedOperation::REMOVE_GID: {
        RemoveOperationWithGid op;
        replay(op, is, entry);
        break;
    }
    case FeedOperation::UPDATE: {
        UpdateOperation op(static_cast<FeedOperation::Type>(entry.type()));
        replay(op, is, entry);
        break;
    }
    case FeedOperation::NOOP: {
        NoopOperation op;
        replay(op, is, entry);
        break;
    }
    case FeedOperation::NEW_CONFIG: {
        NewConfigOperation op(entry.serial(), _handler.getNewConfigStreamHandler());
        op.deserialize(is, _handler.getDeserializeRepo());
        _handler.replay(op);
        break;
    }
    case FeedOperation::DELETE_BUCKET: {
        DeleteBucketOperation op;
        replay(op, is, entry);
        break;
    }
    case FeedOperation::SPLIT_BUCKET: {
        SplitBucketOperation op;
        replay(op, is, entry);
        break;
    }
    case FeedOperation::JOIN_BUCKETS: {
        JoinBucketsOperation op;
        replay(op, is, entry);
        break;
    }
    case FeedOperation::PRUNE_REMOVED_DOCUMENTS: {
        PruneRemovedDocumentsOperation op;
        replay(op, is, entry);
        break;
    }
    case FeedOperation::MOVE: {
        MoveOperation op;
        replay(op, is, entry);
        break;
    }
    case FeedOperation::CREATE_BUCKET: {
        CreateBucketOperation op;
        replay(op, is, entry);
        break;
    }
    case FeedOperation::COMPACT_LID_SPACE: {
        CompactLidSpaceOperation op;
        replay(op, is, entry);
        break;
    }
    default:
        throw IllegalStateException(make_string("Got packet entry with unknown type id '%u' from TLS", entry.type()));
    }
    if (!is.empty()) {
        throw document::DeserializeException(
            make_string("Too much data in packet entry (type id '%u', %ld bytes)", entry.type(), is.size()));
    }
}

std::unique_ptr<FeedOperation> ReplayPacketDispatcher::decode(const Packet::Entry&           entry,
                                                              const document::DocumentTypeRepo& repo) {
    std::unique_ptr<FeedOperation> op;
    switch (entry.type()) {
    case FeedOperation::PUT:
        op = std::make_unique<PutOperation>();
        break;
    case FeedOperation::REMOVE:
        op = std::make_unique<RemoveOperationWithDocId>();
        break;
    case FeedOperation::REMOVE_GID:
        op = std::make_unique<RemoveOperationWithGid>();
        break;
    case FeedOperation::UPDATE:
        op = std::make_unique<UpdateOperation>(static_cast<FeedOperation::Type>(entry.type()));
        break;
    default:
        return {};
    }
    vespalib::nbostream is(entry.data().c_str(), entry.data().size());
    op->deserialize(is, repo);
    op->setSerialNum(entry.serial());
    if (!is.empty()) {
        throw document::DeserializeException(
            make_string("Too much data in packet entry (type id '%u', %ld bytes)", entry.type(), is.size()));
    }
    return op;
}

void ReplayPacketDispatcher::dispatch(const FeedOperation& op) {
    store(op);
    switch (op.getType()) {
    case FeedOperation::PUT:
        _handler.replay(static_cast<const PutOperation&>(op));
        break;
    case FeedOperation::REMOVE:
    case FeedOperation::REMOVE_GID:
        _handler.replay(static_cast<const RemoveOperation&>(op));
        break;
    case FeedOperation::UPDATE:
        _handler.replay(static_cast<const UpdateOperation&>(op));
        break;
    default:
        throw IllegalStateException(make_string("Cannot dispatch decoded operation of type '%u'", op.getType()));
    }
}

ReplayPacketDispatcher::~ReplayPacketDispatcher() = default;

void ReplayPacketDispatcher::store(const FeedOperation&) {
}

} // namespace proton
