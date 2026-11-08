// Copyright Vespa.ai. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.

#include "feedstates.h"

#include "feedconfigstore.h"
#include "ifeedview.h"
#include "ireplayconfig.h"
#include "replay_throttling_policy.h"
#include "replaypacketdispatcher.h"

#include <vespa/check_require.h>
#include <vespa/searchcore/proton/bucketdb/ibucketdbhandler.h>
#include <vespa/searchcore/proton/common/eventlogger.h>
#include <vespa/searchcore/proton/common/memory_usage_logger.h>
#include <vespa/searchcore/proton/common/replay_feed_token_factory.h>
#include <vespa/searchcore/proton/feedoperation/operations.h>
#include <vespa/vespalib/util/count_down_latch.h>
#include <vespa/vespalib/util/idestructorcallback.h>
#include <vespa/vespalib/util/lambdatask.h>
#include <vespa/vespalib/util/shared_operation_throttler.h>
#include <vespa/vespalib/util/threadstackexecutor.h>

#include <exception>
#include <vector>

#include <vespa/log/log.h>
LOG_SETUP(".proton.server.feedstates");

using proton::bucketdb::IBucketDBHandler;
using search::SerialNum;
using search::transactionlog::Packet;
using search::transactionlog::client::RPC;
using vespalib::Executor;
using vespalib::IDestructorCallback;
using vespalib::make_string;
using vespalib::makeLambdaTask;
using vespalib::SharedOperationThrottler;

namespace proton {

namespace {

const search::SerialNum REPLAY_PROGRESS_INTERVAL = 50000;

void handleProgress(TlsReplayProgress& progress, SerialNum currentSerial) {
    progress.updateCurrent(currentSerial);
    if (LOG_WOULD_LOG(event) && (LOG_WOULD_LOG(debug) || (progress.getCurrent() % REPLAY_PROGRESS_INTERVAL == 0))) {
        EventLogger::transactionLogReplayProgress(progress.getDomainName(), progress.getProgress(),
                                                  progress.getFirst(), progress.getLast(), progress.getCurrent());
        MemoryUsageLogger::log("progress replay transactionlog",
                               progress.getDomainName() + " " + std::to_string(progress.getProgress()));
    }
}

class TransactionLogReplayPacketHandler : public IReplayPacketHandler {
    IFeedView*&                                        _feed_view_ptr; // Pointer can be changed in executor thread.
    IBucketDBHandler&                                  _bucketDBHandler;
    IReplayConfig&                                     _replay_config;
    FeedConfigStore&                                   _config_store;
    IIncSerialNum&                                     _inc_serial_num;
    CommitTimeTracker                                  _commitTimeTracker;
    std::shared_ptr<SharedOperationThrottler>          _throttler;
    std::unique_ptr<feedtoken::ReplayFeedTokenFactory> _replay_feed_token_factory;

public:
    TransactionLogReplayPacketHandler(IFeedView*& feed_view_ptr, IBucketDBHandler& bucketDBHandler,
                                      IReplayConfig& replay_config, FeedConfigStore& config_store,
                                      std::shared_ptr<vespalib::SharedOperationThrottler> shared_replay_throttler,
                                      IIncSerialNum&                                      inc_serial_num)
        : _feed_view_ptr(feed_view_ptr),
          _bucketDBHandler(bucketDBHandler),
          _replay_config(replay_config),
          _config_store(config_store),
          _inc_serial_num(inc_serial_num),
          _commitTimeTracker(5ms),
          _throttler(std::move(shared_replay_throttler)),
          _replay_feed_token_factory(std::make_unique<feedtoken::ReplayFeedTokenFactory>(true)) {}

    ~TransactionLogReplayPacketHandler() override = default;

    FeedToken make_replay_feed_token(const FeedOperation& op) {
        uint64_t estimated_memory_footprint = 0;
        if (const auto* doc_op = dynamic_cast<const DocumentOperation*>(&op)) {
            estimated_memory_footprint = doc_op->getSerializedDocSize(); // Cached and "free" to read
        }
        SharedOperationThrottler::Token throttler_token =
            _throttler->blocking_acquire_one(estimated_memory_footprint);
        return _replay_feed_token_factory->make_replay_feed_token(std::move(throttler_token), op);
    }

    void replay(const PutOperation& op) override { _feed_view_ptr->handlePut(make_replay_feed_token(op), op); }
    void replay(const RemoveOperation& op) override { _feed_view_ptr->handleRemove(make_replay_feed_token(op), op); }
    void replay(const UpdateOperation& op) override { _feed_view_ptr->handleUpdate(make_replay_feed_token(op), op); }
    void replay(const NoopOperation&) override {} // ignored
    void replay(const NewConfigOperation& op) override { _replay_config.replayConfig(op.getSerialNum()); }

    void replay(const DeleteBucketOperation& op) override {
        _feed_view_ptr->handleDeleteBucket(op, make_replay_feed_token(op));
    }
    void replay(const SplitBucketOperation& op) override {
        _bucketDBHandler.handleSplit(op.getSerialNum(), op.getSource(), op.getTarget1(), op.getTarget2());
    }
    void replay(const JoinBucketsOperation& op) override {
        _bucketDBHandler.handleJoin(op.getSerialNum(), op.getSource1(), op.getSource2(), op.getTarget());
    }
    void replay(const PruneRemovedDocumentsOperation& op) override {
        _feed_view_ptr->handlePruneRemovedDocuments(op, make_replay_feed_token(op));
    }
    void replay(const MoveOperation& op) override { _feed_view_ptr->handleMove(op, make_replay_feed_token(op)); }
    void replay(const CreateBucketOperation&) override {}
    void replay(const CompactLidSpaceOperation& op) override {
        _feed_view_ptr->handleCompactLidSpace(op, make_replay_feed_token(op));
    }
    NewConfigOperation::IStreamHandler& getNewConfigStreamHandler() override { return _config_store; }
    const document::DocumentTypeRepo& getDeserializeRepo() override { return *_feed_view_ptr->getDocumentTypeRepo(); }
    void check_serial_num(search::SerialNum serial_num) override {
        auto exp_serial_num = _inc_serial_num.inc_serial_num();
        if (exp_serial_num != serial_num) {
            LOG(warning, "Expected replay serial number %" PRIu64 ", got serial number %" PRIu64, exp_serial_num,
                serial_num);
            CHECK(exp_serial_num == serial_num);
        }
    }
    void optionalCommit(search::SerialNum serialNum) override {
        if (_commitTimeTracker.needCommit()) {
            _feed_view_ptr->forceCommit(serialNum);
        }
    }
};

class PacketDispatcher {
public:
    PacketDispatcher(IReplayPacketHandler* packet_handler, vespalib::Executor* decode_executor)
        : _packet_handler(packet_handler), _decode_executor(decode_executor) {}

    void handlePacket(PacketWrapper& wrap);

private:
    void handleEntry(const Packet::Entry& entry);
    void handleEntries(const std::vector<Packet::Entry>& entries, TlsReplayProgress* progress);
    void handleEntriesDecodedInParallel(const std::vector<Packet::Entry>& entries, TlsReplayProgress* progress);
    IReplayPacketHandler* _packet_handler;
    vespalib::Executor*   _decode_executor;
};

constexpr size_t decode_chunk_size = 16;

void PacketDispatcher::handlePacket(PacketWrapper& wrap) {
    vespalib::nbostream_longlivedbuf handle(wrap.packet.getHandle().data(), wrap.packet.getHandle().size());
    std::vector<Packet::Entry>       entries;
    bool                             has_config = false;
    while (!handle.empty()) {
        Packet::Entry entry;
        entry.deserialize(handle);
        has_config |= (entry.type() == FeedOperation::NEW_CONFIG);
        entries.push_back(entry);
    }
    // A config entry changes the document type repo that later entries are deserialized with.
    if (_decode_executor != nullptr && !has_config && entries.size() >= 2 * decode_chunk_size) {
        handleEntriesDecodedInParallel(entries, wrap.progress);
    } else {
        handleEntries(entries, wrap.progress);
    }
    wrap.result = RPC::OK;
    wrap.gate.countDown();
}

void PacketDispatcher::handleEntries(const std::vector<Packet::Entry>& entries, TlsReplayProgress* progress) {
    for (const auto& entry : entries) {
        handleEntry(entry);
        if (progress != nullptr) {
            handleProgress(*progress, entry.serial());
        }
    }
}

void PacketDispatcher::handleEntriesDecodedInParallel(const std::vector<Packet::Entry>& entries,
                                                      TlsReplayProgress*                progress) {
    // Called in the master thread, which waits while the decode threads deserialize the documents and then
    // dispatches the operations in serial number order, as handleEntries() does.
    const auto&                                 repo = _packet_handler->getDeserializeRepo();
    std::vector<std::unique_ptr<FeedOperation>> decoded(entries.size());
    const size_t                                num_chunks = (entries.size() + decode_chunk_size - 1) / decode_chunk_size;
    std::vector<std::exception_ptr>             errors(num_chunks);
    vespalib::CountDownLatch                    latch(num_chunks);
    for (size_t chunk = 0; chunk < num_chunks; ++chunk) {
        auto rejected = _decode_executor->execute(makeLambdaTask([&, chunk]() {
            try {
                size_t end = std::min(entries.size(), (chunk + 1) * decode_chunk_size);
                for (size_t i = chunk * decode_chunk_size; i < end; ++i) {
                    decoded[i] = ReplayPacketDispatcher::decode(entries[i], repo);
                }
            } catch (...) {
                errors[chunk] = std::current_exception();
            }
            latch.countDown();
        }));
        CHECK(!rejected);
    }
    latch.await();
    for (const auto& error : errors) {
        if (error) {
            std::rethrow_exception(error);
        }
    }
    ReplayPacketDispatcher dispatcher(*_packet_handler);
    for (size_t i = 0; i < entries.size(); ++i) {
        const auto& entry = entries[i];
        _packet_handler->check_serial_num(entry.serial());
        if (decoded[i]) {
            dispatcher.dispatch(*decoded[i]);
        } else {
            dispatcher.replayEntry(entry);
        }
        _packet_handler->optionalCommit(entry.serial());
        if (progress != nullptr) {
            handleProgress(*progress, entry.serial());
        }
    }
}

void PacketDispatcher::handleEntry(const Packet::Entry& entry) {
    // Called by handlePacket() in executor thread.
    LOG(spam, "replay packet entry: entrySerial(%" PRIu64 "), entryType(%u)", entry.serial(), entry.type());

    auto entry_serial_num = entry.serial();
    _packet_handler->check_serial_num(entry_serial_num);
    ReplayPacketDispatcher dispatcher(*_packet_handler);
    dispatcher.replayEntry(entry);
    _packet_handler->optionalCommit(entry_serial_num);
}

// Prototype: VESPA_REPLAY_DECODE_THREADS (default 0 = decode in the master thread, as before).
std::unique_ptr<vespalib::ThreadStackExecutor> make_decode_executor() {
    const char* value = getenv("VESPA_REPLAY_DECODE_THREADS");
    uint32_t    threads = (value != nullptr) ? static_cast<uint32_t>(atoi(value)) : 0u;
    if (threads == 0) {
        return {};
    }
    LOG(info, "Decoding transaction log entries in %u threads during replay", threads);
    return std::make_unique<vespalib::ThreadStackExecutor>(threads);
}

} // namespace

ReplayTransactionLogState::ReplayTransactionLogState(
    const std::string& name, IFeedView*& feed_view_ptr, IBucketDBHandler& bucketDBHandler,
    IReplayConfig& replay_config, FeedConfigStore& config_store,
    std::shared_ptr<vespalib::SharedOperationThrottler> shared_replay_throttler, IIncSerialNum& inc_serial_num)
    : FeedState(REPLAY_TRANSACTION_LOG),
      _doc_type_name(name),
      _packet_handler(std::make_unique<TransactionLogReplayPacketHandler>(
          feed_view_ptr, bucketDBHandler, replay_config, config_store, std::move(shared_replay_throttler),
          inc_serial_num)),
      _decode_executor(make_decode_executor()) {
}

ReplayTransactionLogState::~ReplayTransactionLogState() = default;

void ReplayTransactionLogState::receive(const PacketWrapper::SP& wrap, Executor& executor) {
    executor.execute(makeLambdaTask([this, wrap = wrap]() {
        PacketDispatcher dispatcher(_packet_handler.get(), _decode_executor.get());
        dispatcher.handlePacket(*wrap);
    }));
}

NormalState::NormalState(FeedHandler& handler) noexcept : FeedState(NORMAL), _handler(handler) {
}

NormalState::~NormalState() = default;

void NormalState::handleOperation(FeedToken token, FeedOperationUP op) {
    _handler.performOperation(std::move(token), std::move(op));
}

void NormalState::receive(const PacketWrapperSP& wrap, vespalib::Executor&) {
    throwExceptionInReceive(_handler.getDocTypeName().c_str(), wrap->packet.range().from(), wrap->packet.range().to(),
                            wrap->packet.size());
}

} // namespace proton
