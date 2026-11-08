// Copyright Vespa.ai. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.

#include <vespa/document/base/documentid.h>
#include <vespa/searchcore/proton/bucketdb/bucket_db_owner.h>
#include <vespa/searchcore/proton/documentmetastore/documentmetastore.h>
#include <vespa/searchcore/proton/documentmetastore/documentmetastoreflushtarget.h>
#include <vespa/searchcore/proton/documentmetastore/documentmetastoreinitializer.h>
#include <vespa/searchcore/proton/server/itlssyncer.h>
#include <vespa/searchlib/attribute/attributefilesavetarget.h>
#include <vespa/searchlib/common/flush_token.h>
#include <vespa/searchlib/common/tunefileinfo.h>
#include <vespa/searchlib/fef/matchdatalayout.h>
#include <vespa/searchlib/index/dummyfileheadercontext.h>
#include <vespa/searchlib/queryeval/blueprint.h>
#include <vespa/searchlib/queryeval/searchiterator.h>
#include <vespa/vespalib/gtest/gtest.h>
#include <vespa/vespalib/util/hw_info.h>
#include <vespa/vespalib/util/image_file.h>
#include <vespa/vespalib/util/stringfmt.h>
#include <vespa/vespalib/util/threadstackexecutor.h>
#include <vespa/vespalib/util/time.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>

#include <vespa/log/log.h>
LOG_SETUP("document_meta_store_image_test");

using document::BucketId;
using document::DocumentId;
using document::GlobalId;
using proton::bucketdb::BucketDBOwner;
using proton::bucketdb::BucketState;
using search::AttributeFileSaveTarget;
using search::CommitParam;
using search::TuneFileAttributes;
using search::index::DummyFileHeaderContext;
using storage::spi::Timestamp;

namespace proton {

namespace {

constexpr uint64_t timestamp_bias = 2000000000000ul;

DocumentId make_docid(uint32_t n) {
    return DocumentId(vespalib::make_string("id:ns:testdoc::%u", n));
}

// With bit_variants > 1, neighbouring gids get different used bits, giving overlapping buckets.
BucketId make_bucket_id(const GlobalId& gid, uint32_t n, uint32_t base_bits, uint32_t bit_variants) {
    return BucketId(base_bits + (n % bit_variants) * 2, gid.convertToBucketId().getRawId());
}

class DummyTlsSyncer : public ITlsSyncer {
public:
    void sync() override {}
};

void remove_files(const std::string& name) {
    for (const char* suffix : {".dat", ".img", ".img.tmp"}) {
        std::filesystem::remove(std::filesystem::path(name + suffix));
    }
}

struct Feeder {
    DocumentMetaStore&    dms;
    std::vector<GlobalId> gids; // every gid ever fed, also removed ones
    uint32_t              base_bits;
    uint32_t              bit_variants;

    Feeder(DocumentMetaStore& dms_in, uint32_t base_bits_in, uint32_t bit_variants_in = 3)
        : dms(dms_in), gids(), base_bits(base_bits_in), bit_variants(bit_variants_in) {}

    uint32_t put(uint32_t n, uint64_t timestamp) {
        auto     docid = make_docid(n);
        auto&    gid = docid.getGlobalId();
        auto     inspect = dms.inspect(gid, 0u);
        uint32_t doc_size = 100 + n % 1000;
        auto     bucket_id = make_bucket_id(gid, n, base_bits, bit_variants);
        auto     res = dms.put(docid, bucket_id, Timestamp(timestamp), doc_size, inspect.getLid(), 0u);
        CHECK(res.ok());
        gids.push_back(gid);
        return res.getLid();
    }

    void put_range(uint32_t first, uint32_t count) {
        for (uint32_t n = first; n < first + count; ++n) {
            put(n, timestamp_bias + n);
            if ((n % 1000) == 0) {
                dms.commit();
            }
        }
        dms.commit();
    }
};

// Removes, holds, frees, moves and updates documents so that the saved store has every kind of lid state.
void churn(DocumentMetaStore& dms, uint32_t num_docs) {
    std::vector<uint32_t> batch;
    for (uint32_t lid = 7; lid <= num_docs; lid += 7) {
        batch.push_back(lid);
    }
    dms.removeBatch(batch, dms.getCommittedDocIdLimit());
    dms.commit();
    std::vector<uint32_t> held;
    for (uint32_t lid : batch) {
        if ((lid % 14) == 0) {
            held.push_back(lid); // the rest stay pending hold
        }
    }
    dms.removes_complete(held);
    for (uint32_t lid = 11; lid <= num_docs; lid += 11) {
        if ((lid % 7) != 0) {
            EXPECT_TRUE(dms.remove(lid, 0u));
            dms.removes_complete({lid});
        }
    }
    dms.commit();
    dms.reclaim_unused_memory(); // held lids become free
    // Move the highest document down into the lowest free lid, as lid space compaction does.
    auto     stats = dms.getLidUsageStats();
    uint32_t from_lid = stats.getHighestUsedLid();
    uint32_t to_lid = stats.getLowestFreeLid();
    ASSERT_LT(to_lid, from_lid);
    dms.move(make_docid(1), from_lid, to_lid, 0u); // docid string is unused without full document ids
    dms.removes_complete({from_lid});
    // Update timestamps and sizes of some documents, moving a few to other buckets.
    for (uint32_t lid = 3; lid <= num_docs; lid += 13) {
        if (dms.validLid(lid)) {
            const auto& meta = dms.getRawMetadata(lid);
            EXPECT_TRUE(dms.updateMetadata(lid, meta.getBucketId(), Timestamp(meta.getTimestamp() + 5)));
        }
    }
    dms.commit();
}

uint32_t bucket_state_mismatches(BucketDBOwner& exp_db, BucketDBOwner& act_db) {
    auto exp_guard = exp_db.takeGuard();
    auto act_guard = act_db.takeGuard();
    auto exp_buckets = exp_guard->getBuckets();
    auto act_buckets = act_guard->getBuckets();
    std::sort(exp_buckets.begin(), exp_buckets.end());
    std::sort(act_buckets.begin(), act_buckets.end());
    EXPECT_EQ(exp_buckets.size(), act_buckets.size());
    if (exp_buckets != act_buckets) {
        return std::max(exp_buckets.size(), act_buckets.size());
    }
    uint32_t mismatches = 0;
    for (const auto& bucket_id : exp_buckets) {
        BucketState exp = exp_guard->get(bucket_id);
        BucketState act = act_guard->get(bucket_id);
        if (exp.getChecksum() != act.getChecksum() || exp.getReadyCount() != act.getReadyCount() ||
            exp.getNotReadyCount() != act.getNotReadyCount() || exp.getRemovedCount() != act.getRemovedCount() ||
            exp.getReadyDocSizes() != act.getReadyDocSizes() ||
            exp.getNotReadyDocSizes() != act.getNotReadyDocSizes() ||
            exp.getRemovedDocSizes() != act.getRemovedDocSizes() || exp.isActive() != act.isActive())
        {
            ++mismatches;
        }
    }
    return mismatches;
}

void expect_same_stores(DocumentMetaStore& exp, DocumentMetaStore& act, const std::vector<GlobalId>& gids) {
    // Memory usage stats walk every data store buffer, as proton does periodically.
    exp.commit(CommitParam::UpdateStats::FORCE);
    act.commit(CommitParam::UpdateStats::FORCE);
    EXPECT_EQ(exp.getNumDocs(), act.getNumDocs());
    EXPECT_EQ(exp.getCommittedDocIdLimit(), act.getCommittedDocIdLimit());
    EXPECT_EQ(exp.getNumUsedLids(), act.getNumUsedLids());
    EXPECT_EQ(exp.getNumActiveLids(), act.getNumActiveLids());
    EXPECT_EQ(exp.getFreeListActive(), act.getFreeListActive());
    auto exp_stats = exp.getLidUsageStats();
    auto act_stats = act.getLidUsageStats();
    EXPECT_EQ(exp_stats.getLidLimit(), act_stats.getLidLimit());
    EXPECT_EQ(exp_stats.getUsedLids(), act_stats.getUsedLids());
    EXPECT_EQ(exp_stats.getLowestFreeLid(), act_stats.getLowestFreeLid());
    EXPECT_EQ(exp_stats.getHighestUsedLid(), act_stats.getHighestUsedLid());
    uint32_t lid_mismatches = 0;
    uint32_t limit = std::min(exp.getCommittedDocIdLimit(), act.getCommittedDocIdLimit());
    for (uint32_t lid = 0; lid < limit; ++lid) {
        if (exp.validLid(lid) != act.validLid(lid) ||
            exp.getActiveLids().testBit(lid) != act.getActiveLids().testBit(lid))
        {
            ++lid_mismatches;
            continue;
        }
        if (!exp.validLid(lid)) {
            continue;
        }
        const auto& e = exp.getRawMetadata(lid);
        const auto& a = act.getRawMetadata(lid);
        if (e.getGid() != a.getGid() || e.getBucketId().getRawId() != a.getBucketId().getRawId() ||
            e.getTimestamp() != a.getTimestamp() || e.getDocSize() != a.getDocSize())
        {
            ++lid_mismatches;
        }
    }
    EXPECT_EQ(0u, lid_mismatches);
    uint32_t gid_mismatches = 0;
    for (const auto& gid : gids) {
        uint32_t exp_lid = 0;
        uint32_t act_lid = 0;
        bool     exp_found = exp.getLid(gid, exp_lid);
        bool     act_found = act.getLid(gid, act_lid);
        if (exp_found != act_found || exp_lid != act_lid) {
            ++gid_mismatches;
        }
    }
    EXPECT_EQ(0u, gid_mismatches);
    EXPECT_EQ(0u, bucket_state_mismatches(exp.getBucketDB(), act.getBucketDB()));
}

// What the sub db does with a loaded store before and after replay, applied to both stores in the same way.
void use_after_load(DocumentMetaStore& dms, Feeder& feeder, uint32_t first_new) {
    BucketId::List active;
    {
        auto buckets = dms.getBucketDB().takeGuard()->getBuckets();
        std::sort(buckets.begin(), buckets.end());
        for (size_t i = 0; i < buckets.size(); i += 5) {
            active.push_back(buckets[i]);
        }
    }
    dms.populateActiveBuckets(active);
    dms.constructFreeList();
    feeder.put_range(first_new, 3000);
    std::vector<uint32_t> batch;
    for (uint32_t lid = 5; lid < dms.getCommittedDocIdLimit(); lid += 101) {
        if (dms.validLid(lid)) {
            batch.push_back(lid);
        }
    }
    dms.removeBatch(batch, dms.getCommittedDocIdLimit());
    dms.removes_complete(batch);
    dms.commit();
    dms.reclaim_unused_memory();
    feeder.put_range(first_new + 3000, 1000); // reuses freed lids
}

// Compares every bucket in either db; a bucket missing from one db compares as an empty, inactive one.
uint32_t bucket_union_mismatches(BucketDBOwner& exp_db, BucketDBOwner& act_db) {
    auto exp_guard = exp_db.takeGuard();
    auto act_guard = act_db.takeGuard();
    EXPECT_EQ(exp_guard->getNumActiveDocs(), act_guard->getNumActiveDocs());
    auto buckets = exp_guard->getBuckets();
    auto act_buckets = act_guard->getBuckets();
    buckets.insert(buckets.end(), act_buckets.begin(), act_buckets.end());
    std::sort(buckets.begin(), buckets.end());
    buckets.erase(std::unique(buckets.begin(), buckets.end()), buckets.end());
    uint32_t mismatches = 0;
    for (const auto& bucket_id : buckets) {
        BucketState exp = exp_guard->get(bucket_id);
        BucketState act = act_guard->get(bucket_id);
        if (exp.getChecksum() != act.getChecksum() || exp.getReadyCount() != act.getReadyCount() ||
            exp.getNotReadyCount() != act.getNotReadyCount() || exp.getRemovedCount() != act.getRemovedCount() ||
            exp.getReadyDocSizes() != act.getReadyDocSizes() || exp.isActive() != act.isActive())
        {
            ++mismatches;
        }
    }
    return mismatches;
}

uint32_t active_lid_mismatches(const DocumentMetaStore& exp, const DocumentMetaStore& act) {
    EXPECT_EQ(exp.getNumActiveLids(), act.getNumActiveLids());
    uint32_t mismatches = 0;
    uint32_t limit = std::min(exp.getCommittedDocIdLimit(), act.getCommittedDocIdLimit());
    for (uint32_t lid = 0; lid < limit; ++lid) {
        if (exp.getActiveLids().testBit(lid) != act.getActiveLids().testBit(lid)) {
            ++mismatches;
        }
    }
    return mismatches;
}

// Search-visible lids, as matching sees them through the white list blueprint.
std::vector<uint32_t> visible_lids(const DocumentMetaStore& dms) {
    auto     blueprint = dms.createWhiteListBlueprint();
    uint32_t limit = dms.getCommittedDocIdLimit();
    blueprint->basic_plan(true, limit);
    blueprint->fetchPostings(search::queryeval::ExecuteInfo::FULL);
    search::fef::MatchDataLayout mdl;
    auto                         md = mdl.createMatchData();
    auto                         search = blueprint->createSearch(*md);
    std::vector<uint32_t>        result;
    search->initRange(1, limit);
    for (uint32_t lid = search->seekFirst(1); lid < limit; lid = search->seekNext(lid + 1)) {
        result.push_back(lid);
    }
    return result;
}

std::string snapshot_file(const std::string& base_dir, uint64_t serial) {
    return base_dir + "/snapshot-" + std::to_string(serial) + "/" + DocumentMetaStore::getFixedName();
}

double elapsed_ms(vespalib::steady_time start) {
    return vespalib::to_s(vespalib::steady_clock::now() - start) * 1000.0;
}

} // namespace

class DocumentMetaStoreImageTest : public ::testing::Test {
protected:
    TuneFileAttributes     _tune;
    DummyFileHeaderContext _file_header_context;

    void save_stock(DocumentMetaStore& dms, const std::string& name) {
        AttributeFileSaveTarget target(_tune, _file_header_context);
        ASSERT_TRUE(dms.save(target, name));
    }
};

TEST_F(DocumentMetaStoreImageTest, image_round_trip_matches_stock_load) {
    ASSERT_TRUE(vespalib::image::enabled());
    constexpr uint32_t num_docs = 300000;
    std::string        stock_name("dms_stock");
    std::string        image_name("dms_image");
    DocumentMetaStore  dms(std::make_shared<BucketDBOwner>());
    dms.constructFreeList();
    Feeder feeder(dms, 12);
    feeder.put_range(1, num_docs);
    churn(dms, num_docs);
    ASSERT_FALSE(HasFatalFailure());
    LOG(info, "saved store: %u used lids, lid limit %u, %zu buckets", dms.getNumUsedLids(),
        dms.getCommittedDocIdLimit(), dms.getBucketDB().takeGuard()->size());
    save_stock(dms, stock_name);
    ASSERT_TRUE(dms.save_image_file(image_name, _file_header_context));
    ASSERT_TRUE(std::filesystem::exists(image_name + ".img"));

    DocumentMetaStore stock(std::make_shared<BucketDBOwner>(), stock_name);
    DocumentMetaStore image(std::make_shared<BucketDBOwner>(), image_name);
    EXPECT_FALSE(stock.has_image_file());
    EXPECT_TRUE(image.has_image_file());
    ASSERT_TRUE(stock.load());
    ASSERT_TRUE(image.load());
    EXPECT_EQ(dms.getNumUsedLids(), image.getNumUsedLids());
    EXPECT_EQ(0u, image.getNumActiveLids());
    EXPECT_FALSE(image.requires_doc_sizes_from_docstore());
    EXPECT_FALSE(image.requires_document_ids_from_docstore());
    expect_same_stores(stock, image, feeder.gids);
    // Lookups also match the store that was saved.
    uint32_t saved_mismatches = 0;
    for (const auto& gid : feeder.gids) {
        uint32_t saved_lid = 0;
        uint32_t image_lid = 0;
        if (dms.getLid(gid, saved_lid) != image.getLid(gid, image_lid) || saved_lid != image_lid) {
            ++saved_mismatches;
        }
    }
    EXPECT_EQ(0u, saved_mismatches);

    // The next lid for a new document, and puts and removes after load, behave the same.
    stock.constructFreeList();
    image.constructFreeList();
    auto new_docid = make_docid(num_docs + 100000);
    EXPECT_EQ(stock.inspect(new_docid.getGlobalId(), 0u).getLid(),
              image.inspect(new_docid.getGlobalId(), 0u).getLid());
    // constructFreeList() may only be called once; use fresh stores for the post-load sequence.
    DocumentMetaStore stock2(std::make_shared<BucketDBOwner>(), stock_name);
    DocumentMetaStore image2(std::make_shared<BucketDBOwner>(), image_name);
    ASSERT_TRUE(stock2.load());
    ASSERT_TRUE(image2.load());
    Feeder stock_feeder(stock2, 12);
    Feeder image_feeder(image2, 12);
    use_after_load(stock2, stock_feeder, num_docs + 1);
    use_after_load(image2, image_feeder, num_docs + 1);
    EXPECT_GT(image2.getNumActiveLids(), 0u);
    std::vector<GlobalId> all_gids = feeder.gids;
    all_gids.insert(all_gids.end(), stock_feeder.gids.begin(), stock_feeder.gids.end());
    expect_same_stores(stock2, image2, all_gids);
    remove_files(stock_name);
    remove_files(image_name);
}

TEST_F(DocumentMetaStoreImageTest, image_restores_active_state) {
    constexpr uint32_t num_docs = 100000;
    std::string        stock_name("active_stock");
    std::string        image_name("active_image");
    DocumentMetaStore  dms(std::make_shared<BucketDBOwner>());
    dms.constructFreeList();
    Feeder feeder(dms, 12);
    feeder.put_range(1, num_docs);
    churn(dms, num_docs);
    ASSERT_FALSE(HasFatalFailure());
    BucketId::List buckets = dms.getBucketDB().takeGuard()->getBuckets();
    std::sort(buckets.begin(), buckets.end());
    BucketId::List active;
    for (size_t i = 0; i < buckets.size(); i += 3) {
        dms.setBucketState(buckets[i], true);
        active.push_back(buckets[i]);
    }
    for (size_t i = 0; i < buckets.size(); i += 21) {
        dms.setBucketState(buckets[i], false); // activated above, then deactivated
    }
    dms.setBucketState(BucketId(16, 0x1234), true); // an active bucket without documents
    feeder.put_range(num_docs + 1, 2000);           // inserts into active buckets get active lids
    dms.commit();
    ASSERT_GT(dms.getNumActiveLids(), 0u);
    ASSERT_LT(dms.getNumActiveLids(), dms.getNumUsedLids());
    size_t active_docs = dms.getBucketDB().takeGuard()->getNumActiveDocs();
    ASSERT_GT(active_docs, 0u);
    save_stock(dms, stock_name);
    ASSERT_TRUE(dms.save_image_file(image_name, _file_header_context));

    DocumentMetaStore image(std::make_shared<BucketDBOwner>(), image_name);
    ASSERT_TRUE(image.load());
    EXPECT_EQ(dms.getNumActiveLids(), image.getNumActiveLids());
    EXPECT_EQ(0u, active_lid_mismatches(dms, image));
    EXPECT_EQ(0u, bucket_union_mismatches(dms.getBucketDB(), image.getBucketDB()));
    EXPECT_EQ(dms.getBucketDB().takeGuard()->getActiveBuckets(),
              image.getBucketDB().takeGuard()->getActiveBuckets());
    EXPECT_EQ(visible_lids(dms), visible_lids(image));
    EXPECT_TRUE(image.getBucketDB().takeGuard()->validateIntegrity());

    // A cluster state arrives: the stock store gets the ramp, the restored one only applies changes.
    DocumentMetaStore stock(std::make_shared<BucketDBOwner>(), stock_name);
    ASSERT_TRUE(stock.load());
    EXPECT_EQ(0u, stock.getNumActiveLids());
    auto active_buckets = dms.getBucketDB().takeGuard()->getActiveBuckets();
    stock.populateActiveBuckets(active_buckets);
    image.populateActiveBuckets(active_buckets);
    EXPECT_EQ(0u, active_lid_mismatches(stock, image));
    EXPECT_EQ(0u, bucket_union_mismatches(stock.getBucketDB(), image.getBucketDB()));
    EXPECT_EQ(visible_lids(stock), visible_lids(image));
    for (size_t i = 0; i < active_buckets.size(); i += 4) {
        stock.setBucketState(active_buckets[i], false);
        image.setBucketState(active_buckets[i], false);
    }
    for (size_t i = 1; i < buckets.size(); i += 7) {
        stock.setBucketState(buckets[i], true);
        image.setBucketState(buckets[i], true);
    }
    EXPECT_EQ(0u, active_lid_mismatches(stock, image));
    EXPECT_EQ(0u, bucket_union_mismatches(stock.getBucketDB(), image.getBucketDB()));
    EXPECT_EQ(visible_lids(stock), visible_lids(image));
    // Deactivating everything, as when the node goes down, leaves nothing visible.
    auto still_active = image.getBucketDB().takeGuard()->getActiveBuckets(); // setBucketState takes the guard
    for (const auto& bucket_id : still_active) {
        image.setBucketState(bucket_id, false);
    }
    EXPECT_EQ(0u, image.getNumActiveLids());
    EXPECT_EQ(0u, image.getBucketDB().takeGuard()->getNumActiveDocs());
    EXPECT_TRUE(visible_lids(image).empty());
    remove_files(stock_name);
    remove_files(image_name);
}

TEST_F(DocumentMetaStoreImageTest, only_the_ready_sub_db_restores_active_state) {
    std::string       image_name("notready_image");
    auto              bucket_db = std::make_shared<BucketDBOwner>();
    DocumentMetaStore dms(bucket_db, "documentmetastore", search::GrowStrategy(), SubDbType::NOTREADY);
    dms.constructFreeList();
    Feeder feeder(dms, 12);
    feeder.put_range(1, 1000);
    auto buckets = bucket_db->takeGuard()->getBuckets();
    bucket_db->takeGuard()->setBucketState(buckets[0], true);
    feeder.put_range(1001, 1000);
    ASSERT_TRUE(dms.save_image_file(image_name, _file_header_context));
    DocumentMetaStore image(std::make_shared<BucketDBOwner>(), image_name, search::GrowStrategy(),
                            SubDbType::NOTREADY);
    ASSERT_TRUE(image.load());
    EXPECT_EQ(0u, image.getNumActiveLids());
    EXPECT_TRUE(image.getBucketDB().takeGuard()->getActiveBuckets().empty());
    remove_files(image_name);
}

TEST_F(DocumentMetaStoreImageTest, flush_target_writes_image_and_initializer_loads_it) {
    ASSERT_TRUE(vespalib::image::enabled());
    constexpr uint32_t num_docs = 20000;
    std::string        base_dir("flush_dir");
    std::string        stock_name("flush_stock");
    std::filesystem::remove_all(std::filesystem::path(base_dir));
    auto dms = std::make_shared<DocumentMetaStore>(std::make_shared<BucketDBOwner>(),
                                                   base_dir + "/" + DocumentMetaStore::getFixedName());
    dms->constructFreeList();
    Feeder feeder(*dms, 10);
    feeder.put_range(1, num_docs);
    churn(*dms, num_docs);
    ASSERT_FALSE(HasFatalFailure());
    save_stock(*dms, stock_name);
    {
        DummyTlsSyncer                tls_syncer;
        vespalib::HwInfo              hw_info;
        DocumentMetaStoreFlushTarget  ft(dms, tls_syncer, base_dir, _tune, _file_header_context, hw_info);
        vespalib::ThreadStackExecutor exec(1);
        ASSERT_TRUE(ft.can_flush(10));
        auto task = ft.initFlush(10, std::make_shared<search::FlushToken>());
        ASSERT_TRUE(task);
        exec.execute(std::move(task));
        exec.sync();
        exec.shutdown();
        EXPECT_EQ(10u, ft.getFlushedSerialNum());
    }
    EXPECT_TRUE(std::filesystem::exists(snapshot_file(base_dir, 10) + ".img"));
    EXPECT_TRUE(std::filesystem::exists(snapshot_file(base_dir, 10) + ".dat"));

    auto loaded = std::make_shared<DocumentMetaStore>(std::make_shared<BucketDBOwner>(),
                                                      base_dir + "/" + DocumentMetaStore::getFixedName());
    documentmetastore::DocumentMetaStoreInitializer initializer(base_dir, "ready", "testdoc", loaded);
    initializer.run();
    EXPECT_TRUE(loaded->isLoaded());
    EXPECT_EQ(10u, loaded->getStatus().getLastSyncToken());
    EXPECT_TRUE(loaded->has_image_file());
    DocumentMetaStore stock(std::make_shared<BucketDBOwner>(), stock_name);
    ASSERT_TRUE(stock.load());
    expect_same_stores(stock, *loaded, feeder.gids);
    remove_files(stock_name);
    std::filesystem::remove_all(std::filesystem::path(base_dir));
}

/*
 * Load time benchmark, disabled by default. Run with --gtest_also_run_disabled_tests; DMS_IMAGE_BENCH_DOCS sets
 * the number of documents (default 5M). Both loads read files that are in the page cache.
 */
TEST_F(DocumentMetaStoreImageTest, DISABLED_benchmark_stock_and_image_load) {
    ASSERT_TRUE(vespalib::image::enabled());
    const char* env = std::getenv("DMS_IMAGE_BENCH_DOCS");
    uint32_t    num_docs = env != nullptr ? std::strtoul(env, nullptr, 10) : 5000000u;
    std::string stock_name("bench_stock");
    std::string image_name("bench_image");
    double      stock_save_ms = 0;
    double      image_save_ms = 0;
    uint64_t    image_bytes = 0;
    {
        DocumentMetaStore dms(std::make_shared<BucketDBOwner>());
        dms.constructFreeList();
        Feeder feeder(dms, 16, 1);
        auto   start = vespalib::steady_clock::now();
        feeder.put_range(1, num_docs);
        LOG(info, "fed %u documents in %.0f ms", num_docs, elapsed_ms(start));
        auto all_buckets = dms.getBucketDB().takeGuard()->getBuckets(); // populateActiveBuckets takes the guard
        dms.populateActiveBuckets(all_buckets);                          // a ready sub db serving everything
        start = vespalib::steady_clock::now();
        save_stock(dms, stock_name);
        stock_save_ms = elapsed_ms(start);
        start = vespalib::steady_clock::now();
        ASSERT_TRUE(dms.save_image_file(image_name, _file_header_context));
        image_save_ms = elapsed_ms(start);
        image_bytes = std::filesystem::file_size(std::filesystem::path(image_name + ".img"));
    }
    uint64_t stock_bytes = std::filesystem::file_size(std::filesystem::path(stock_name + ".dat"));
    double   stock_load_ms = 0;
    double   image_load_ms = 0;
    double   image_lookup_ms = 0;
    for (int round = 0; round < 2; ++round) {
        DocumentMetaStore stock(std::make_shared<BucketDBOwner>(), stock_name);
        auto              start = vespalib::steady_clock::now();
        ASSERT_TRUE(stock.load());
        stock_load_ms = elapsed_ms(start);
        DocumentMetaStore image(std::make_shared<BucketDBOwner>(), image_name);
        start = vespalib::steady_clock::now();
        ASSERT_TRUE(image.load());
        image_load_ms = elapsed_ms(start);
        start = vespalib::steady_clock::now();
        uint32_t found = 0;
        for (uint32_t n = 1; n <= num_docs; n += 1000) {
            uint32_t lid = 0;
            found += image.getLid(make_docid(n).getGlobalId(), lid) ? 1 : 0;
        }
        image_lookup_ms = elapsed_ms(start);
        EXPECT_EQ((num_docs + 999) / 1000, found);
        EXPECT_EQ(stock.getNumUsedLids(), image.getNumUsedLids());
        EXPECT_EQ(image.getNumUsedLids(), image.getNumActiveLids());
    }
    fprintf(stderr,
            "docs=%u stock: save %.0f ms, %lu bytes, load %.1f ms | image: save %.0f ms, %lu bytes, load %.1f ms, "
            "first %u lookups %.1f ms\n",
            num_docs, stock_save_ms, stock_bytes, stock_load_ms, image_save_ms, image_bytes, image_load_ms,
            (num_docs + 999) / 1000, image_lookup_ms);
    remove_files(stock_name);
    remove_files(image_name);
}

} // namespace proton

int main(int argc, char** argv) {
    // vespalib::image::enabled() reads the environment once, so set it before anything asks.
    setenv("VESPA_IMAGE_RESTART", "1", 1);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
