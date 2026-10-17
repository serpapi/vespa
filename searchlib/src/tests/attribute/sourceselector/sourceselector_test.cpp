// Copyright Vespa.ai. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.
// Unit tests for sourceselector.

#include <vespa/searchlib/attribute/fixedsourceselector.h>
#include <vespa/searchlib/index/dummyfileheadercontext.h>
#include <vespa/searchcommon/attribute/config.h>
#include <vespa/searchcommon/common/undefinedvalues.h>
#include <vespa/vespalib/gtest/gtest.h>
#include <vespa/vespalib/util/size_literals.h>
#include <filesystem>
#include <limits>
#include <span>

using std::unique_ptr;
using std::string;
using namespace search;
using namespace search::queryeval;
using search::common::FileHeaderContext;
using search::index::DummyFileHeaderContext;

namespace {
template <typename T, size_t N> size_t arraysize(const T (&)[N]) { return N; }

const uint32_t maxDocId = 4_Ki;
struct DocSource { uint32_t docId; uint8_t source; };
const DocSource docs[] = { {0,1}, {1, 0}, {2, 2}, {4, 3}, {8, 9}, {16, 178},
                           {32, 1}, {64, 2}, {128, 3}, {256,4}, {512, 2},
                           {1024, 1}, {2048,5}, {maxDocId,1} };
const string index_dir = "test_data";
const string base_file_name = "test_data/sourcelist";
const string base_file_name2 = "test_data/sourcelist2";
const uint32_t default_source = 7;
const uint32_t invalid_source = (uint8_t)search::attribute::getUndefined<int8_t>();
const uint32_t base_id = 42;

uint8_t capSource(uint8_t source, uint8_t defaultSource, bool cap) {
    return (cap ? std::min(source, defaultSource) : source);
}

class SourceSelectorTest : public ::testing::Test
{
protected:
    SourceSelectorTest();
    ~SourceSelectorTest() override;
    void testSourceSelector(const DocSource *docSource, size_t sz, uint8_t defaultSource, ISourceSelector & selector, bool cap);
    void requireThatSelectorCanSaveAndLoad(bool compactLidSpace);
};

SourceSelectorTest::SourceSelectorTest() = default;
SourceSelectorTest::~SourceSelectorTest() = default;

void setSources(ISourceSelector &selector) {
    for (size_t i = 0; i < arraysize(docs); ++i) {
        selector.setSource(docs[i].docId, docs[i].source);
    }
}

TEST_F(SourceSelectorTest, test_fixed)
{
    const DocSource *docSource = docs;
    size_t sz = arraysize(docs);
    FixedSourceSelector selector(default_source, base_file_name, 10);
    EXPECT_EQ(default_source, selector.getDefaultSource());
    EXPECT_EQ(10u, selector.getDocIdLimit());
    setSources(selector);
    /*
     * One extra element beyond highest explicitly set element is
     * initialized to accommodate a match loop optimization. See
     * setSource() and reserve() member functions in
     * FixedSourceSelector for details.
     */
    EXPECT_EQ(default_source, selector.createIterator()->getSource(maxDocId + 1));
    testSourceSelector(docSource, sz, selector.getDefaultSource(), selector, false);
    EXPECT_EQ(maxDocId+1, selector.getDocIdLimit());
}

void
SourceSelectorTest::testSourceSelector(const DocSource *docSource, size_t sz,
                                       uint8_t defaultSource, ISourceSelector &selector, bool cap)
{
    {
        auto it(selector.createIterator());
        for (size_t i = 0; i < sz; ++i) {
            EXPECT_EQ((uint32_t)capSource(docSource[i].source, defaultSource, cap), (uint32_t)it->getSource(docSource[i].docId));
        }
    }
    {
        auto it(selector.createIterator());
        for (size_t i = 0, j = 0; i <= docSource[sz - 1].docId; ++i) {
            if (i != docSource[j].docId) {
                EXPECT_EQ((uint32_t)defaultSource, (uint32_t)it->getSource(i));
            } else {
                EXPECT_EQ((uint32_t)capSource(docSource[j].source, defaultSource, cap), (uint32_t)it->getSource(i));
                ++j;
            }
        }
    }
}

TEST_F(SourceSelectorTest, require_that_selector_can_clone_and_subtract)
{
    FixedSourceSelector selector(default_source, base_file_name);
    setSources(selector);
    selector.setBaseId(base_id);

    const uint32_t diff = 3;
    auto new_selector(selector.cloneAndSubtract(base_file_name2, diff));
    EXPECT_EQ(default_source - diff, new_selector->getDefaultSource());
    EXPECT_EQ(base_id + diff, new_selector->getBaseId());
    EXPECT_EQ(maxDocId+1, new_selector->getDocIdLimit());

    auto it(new_selector->createIterator());
    for(size_t i = 0; i < arraysize(docs); ++i) {
        if (docs[i].source > diff) {
            EXPECT_EQ(docs[i].source - diff, it->getSource(docs[i].docId));
        } else {
            EXPECT_EQ(0, it->getSource(docs[i].docId));
        }
    }
}

void
SourceSelectorTest::requireThatSelectorCanSaveAndLoad(bool compactLidSpace)
{
    SCOPED_TRACE(compactLidSpace ? "compactLidSpace=true" : "compactLidSpace=false");
    FixedSourceSelector selector(default_source, base_file_name2);
    setSources(selector);
    selector.setBaseId(base_id);
    selector.setSource(maxDocId + 1, default_source);
    if (compactLidSpace) {
        selector.compactLidSpace(maxDocId - 4);
    }

    std::filesystem::remove_all(std::filesystem::path(index_dir));
    std::filesystem::create_directory(std::filesystem::path(index_dir));

    SourceSelector::SaveInfo::UP save_info =
        selector.extractSaveInfo(base_file_name);
    save_info->save(TuneFileAttributes(), DummyFileHeaderContext());
    auto selector2(FixedSourceSelector::load(base_file_name, default_source + base_id));
    testSourceSelector(docs, arraysize(docs) - compactLidSpace, default_source, *selector2, true);
    EXPECT_EQ(base_id, selector2->getBaseId());
    if (compactLidSpace) {
        EXPECT_EQ(maxDocId - 4, selector2->getDocIdLimit());
    } else {
        EXPECT_EQ(maxDocId + 2, selector2->getDocIdLimit());
    }

    std::filesystem::remove_all(std::filesystem::path(index_dir));
}

TEST_F(SourceSelectorTest, require_that_selector_can_save_and_Load)
{
    requireThatSelectorCanSaveAndLoad(false);
    requireThatSelectorCanSaveAndLoad(true);
}

TEST_F(SourceSelectorTest, require_that_complete_source_range_is_handled)
{
    FixedSourceSelector selector(default_source, base_file_name);
    for (uint32_t i = 0; i < ISourceSelector::SOURCE_LIMIT; ++i) {
        selector.setSource(i, i);
    }
    auto itr = selector.createIterator();
    for (uint32_t i = 0; i < ISourceSelector::SOURCE_LIMIT; ++i) {
        EXPECT_EQ((queryeval::Source)i, itr->getSource(i));
    }
}

TEST_F(SourceSelectorTest, require_that_sources_are_counted_correctly)
{
    FixedSourceSelector selector(default_source, base_file_name);
    for (uint32_t i = 0; i < 256; ++i) {
        selector.setSource(i, i%16);
    }
    SourceSelector::Histogram hist = selector.getDistribution();
    for (uint32_t i = 0; i < 16; ++i) {
        EXPECT_EQ(16u, hist[i]);
    }
    for (uint32_t i = 16; i < 256; ++i) {
        EXPECT_EQ(0u, hist[i]);
    }
}

TEST_F(SourceSelectorTest, require_that_doc_id_limit_is_correct)
{
    FixedSourceSelector selector(default_source, base_file_name);
    EXPECT_EQ(0u, selector.getDocIdLimit());
    selector.setSource(8, 10);
    EXPECT_EQ(9u, selector.getDocIdLimit());
    selector.compactLidSpace(4);
    EXPECT_EQ(4u, selector.getDocIdLimit());
    selector.setSource(6, 10);
    EXPECT_EQ(7u, selector.getDocIdLimit());
    auto selector2 = selector.cloneAndSubtract(base_file_name2, 3);
    EXPECT_EQ(7u, selector2->getDocIdLimit());
}

TEST_F(SourceSelectorTest, require_that_correct_default_value_is_used_after_compaction)
{
    FixedSourceSelector selector(default_source, base_file_name);
    EXPECT_EQ(0u, selector.getDocIdLimit());
    auto it(selector.createIterator());
    selector.setSource(8, 4);
    EXPECT_EQ(default_source, (uint32_t) it->getSource(9));
    EXPECT_EQ(default_source, (uint32_t) it->getSource(6));
    selector.compactLidSpace(4);
    EXPECT_EQ(4u, selector.getDocIdLimit());
    EXPECT_EQ(default_source, (uint32_t) it->getSource(4));
    EXPECT_EQ(invalid_source, (uint32_t) it->getSource(5)); // beyond guard
    selector.setSource(6, 4);
    EXPECT_EQ(7u, selector.getDocIdLimit());
    EXPECT_EQ(default_source, (uint32_t) it->getSource(5));
    EXPECT_EQ(4u, (uint8_t) it->getSource(6));
    EXPECT_EQ(default_source, (uint32_t) it->getSource(7));
}

using SourceStore = queryeval::sourceselector::Iterator::SourceStore;

// Reaches the selector's attribute, including lids at or above the committed docid limit.
struct SelectorPeek : public FixedSourceSelector {
    static SourceStore &store(FixedSourceSelector &selector) {
        return static_cast<SourceStore &>(*(selector.*(&SelectorPeek::_realSource)));
    }
};

uint8_t expected_new_source(uint8_t source, uint32_t diff) {
    return (source > diff) ? source - diff : 0;
}

std::vector<uint8_t> all_bytes(FixedSourceSelector &selector) {
    SourceStore &store = SelectorPeek::store(selector);
    std::vector<uint8_t> result;
    for (uint32_t lid = 0; lid < store.getNumDocs(); ++lid) {
        result.push_back(static_cast<uint8_t>(store.getFast(lid)));
    }
    return result;
}

TEST_F(SourceSelectorTest, require_that_constructor_initializes_every_doc_to_default_source)
{
    for (uint32_t num_docs : {0u, 1u, 2u, 63u, 64u, 65u, 4095u, 4096u, 4097u}) {
        SCOPED_TRACE(num_docs);
        FixedSourceSelector selector(default_source, base_file_name, num_docs);
        SourceStore &store = SelectorPeek::store(selector);
        EXPECT_EQ(num_docs, selector.getDocIdLimit());
        EXPECT_EQ(num_docs + 1, store.getNumDocs());
        EXPECT_EQ(num_docs + 1, store.getCommittedDocIdLimit());
        EXPECT_EQ(std::vector<uint8_t>(num_docs + 1, default_source), all_bytes(selector));
    }
}

TEST_F(SourceSelectorTest, require_that_clone_and_subtract_maps_every_source_byte)
{
    for (uint32_t diff : {0u, 1u, 2u, 7u, 200u, 253u, 254u, 255u, 256u, 1000u, std::numeric_limits<uint32_t>::max()}) {
        SCOPED_TRACE(diff);
        FixedSourceSelector selector(default_source, base_file_name, 300);
        selector.setBaseId(base_id);
        SourceStore &store = SelectorPeek::store(selector);
        std::vector<uint8_t> sources;
        for (uint32_t lid = 0; lid < store.getNumDocs(); ++lid) {
            uint8_t source = lid % 256;
            if (expected_new_source(source, diff) >= ISourceSelector::SOURCE_LIMIT) {
                source = default_source;
            }
            store.set(lid, static_cast<int8_t>(source));
            sources.push_back(source);
        }
        store.commit();
        auto clone = selector.cloneAndSubtract(base_file_name2, diff);
        std::vector<uint8_t> expected;
        for (uint8_t source : sources) {
            expected.push_back(expected_new_source(source, diff));
        }
        EXPECT_EQ(expected, all_bytes(*clone));
        EXPECT_EQ(expected_new_source(default_source, diff), clone->getDefaultSource());
        EXPECT_EQ(base_id + diff, clone->getBaseId());
        EXPECT_EQ(300u, clone->getDocIdLimit());
        EXPECT_EQ(301u, SelectorPeek::store(*clone).getCommittedDocIdLimit());
    }
}

TEST_F(SourceSelectorTest, require_that_clone_and_subtract_copies_lids_above_compacted_limit)
{
    FixedSourceSelector selector(default_source, base_file_name);
    for (uint32_t lid = 0; lid < 100; ++lid) {
        selector.setSource(lid, lid % 8);
    }
    selector.compactLidSpace(50);
    auto source_bytes = all_bytes(selector);
    ASSERT_EQ(101u, source_bytes.size());
    auto clone = selector.cloneAndSubtract(base_file_name2, 2);
    std::vector<uint8_t> expected;
    for (uint8_t source : source_bytes) {
        expected.push_back(expected_new_source(source, 2));
    }
    EXPECT_EQ(expected, all_bytes(*clone));
    EXPECT_EQ(50u, clone->getDocIdLimit());
    EXPECT_EQ(128u - 2, expected[51]); // cleared by compactLidSpace to undefined (0x80), then subtracted
    clone->setSource(60, 3);
    EXPECT_EQ(61u, clone->getDocIdLimit());
    EXPECT_EQ(3u, (uint32_t) clone->createIterator()->getSource(60));
}

TEST_F(SourceSelectorTest, require_that_empty_selector_can_be_cloned)
{
    FixedSourceSelector selector(default_source, base_file_name, std::numeric_limits<uint32_t>::max());
    auto clone = selector.cloneAndSubtract(base_file_name2, 1);
    EXPECT_EQ(0u, SelectorPeek::store(*clone).getNumDocs());
    EXPECT_EQ(0u, SelectorPeek::store(*clone).getCommittedDocIdLimit());
    EXPECT_EQ(default_source - 1, clone->getDefaultSource());
    clone->setSource(3, 2);
    EXPECT_EQ(4u, clone->getDocIdLimit());
    EXPECT_EQ(std::vector<uint8_t>({default_source - 1, default_source - 1, default_source - 1, 2, default_source - 1}),
              all_bytes(*clone));
}

void clone_with_source_byte(uint8_t source, uint32_t diff) {
    FixedSourceSelector selector(default_source, base_file_name, 70);
    SelectorPeek::store(selector).set(65, static_cast<int8_t>(source));
    SelectorPeek::store(selector).commit();
    auto clone = selector.cloneAndSubtract(base_file_name2, diff);
    EXPECT_EQ(source - diff, (uint32_t) clone->createIterator()->getSource(65));
}

TEST_F(SourceSelectorTest, require_that_clone_and_subtract_rejects_new_sources_at_or_above_limit)
{
    EXPECT_DEATH(clone_with_source_byte(255, 1), "SOURCE_LIMIT");
    EXPECT_DEATH(clone_with_source_byte(254, 0), "SOURCE_LIMIT");
    clone_with_source_byte(255, 2);
}

uint64_t generation(FixedSourceSelector &selector) {
    return SelectorPeek::store(selector).getCurrentGeneration().value();
}

TEST_F(SourceSelectorTest, require_that_generation_count_does_not_depend_on_lid_count)
{
    std::vector<uint64_t> constructed;
    std::vector<uint64_t> cloned;
    for (uint32_t num_docs : {1_Mi, 4_Mi}) {
        FixedSourceSelector selector(default_source, base_file_name, num_docs - 1);
        constructed.push_back(generation(selector));
        auto clone = selector.cloneAndSubtract(base_file_name2, 1);
        cloned.push_back(generation(*clone));
        EXPECT_EQ(num_docs, SelectorPeek::store(*clone).getNumDocs());
    }
    EXPECT_EQ(constructed[0], constructed[1]);
    EXPECT_EQ(cloned[0], cloned[1]);
}

TEST_F(SourceSelectorTest, require_that_clone_and_subtract_leaves_room_to_grow)
{
    const uint32_t num_docs = 1_Mi;
    FixedSourceSelector selector(default_source, base_file_name, num_docs - 1);
    auto clone = selector.cloneAndSubtract(base_file_name2, 1);
    auto before = generation(*clone);
    clone->setSource(num_docs + 5, 2);
    EXPECT_EQ(before + 1, generation(*clone)); // the commit in setSource(), no reallocation
    EXPECT_EQ(num_docs + 6, clone->getDocIdLimit());
    EXPECT_EQ(2u, (uint32_t) clone->createIterator()->getSource(num_docs + 5));
    EXPECT_EQ(default_source - 1, (uint32_t) clone->createIterator()->getSource(num_docs + 6));
}

TEST(SourceStoreTest, require_that_init_docs_fills_empty_attribute_and_holds_old_buffer_for_readers)
{
    auto store = std::make_shared<SourceStore>(base_file_name, attribute::Config(attribute::BasicType::INT8));
    const uint32_t num_docs = 1_Mi;
    {
        AttributeGuard guard(store);
        store->init_docs(num_docs, [](std::span<int8_t> values) noexcept {
            for (size_t i = 0; i < values.size(); ++i) {
                values[i] = static_cast<int8_t>(i % 5);
            }
        });
        EXPECT_EQ(num_docs, store->getNumDocs());
        EXPECT_EQ(0u, store->getCommittedDocIdLimit());
        store->commit(CommitParam::UpdateStats::FORCE);
        EXPECT_EQ(num_docs, store->getCommittedDocIdLimit());
        EXPECT_LT(0u, store->getStatus().getOnHold());
    }
    store->commit(CommitParam::UpdateStats::FORCE);
    EXPECT_EQ(0u, store->getStatus().getOnHold());
    EXPECT_LE(num_docs, store->getStatus().getAllocated());
    auto values = store->get_values_for_writer();
    ASSERT_EQ(num_docs, values.size());
    for (uint32_t lid = 0; lid < num_docs; ++lid) {
        ASSERT_EQ(static_cast<int8_t>(lid % 5), values[lid]);
    }
    EXPECT_DEATH(store->init_docs(1, [](std::span<int8_t>) noexcept {}), "getNumDocs");
}

}  // namespace

int
main(int argc, char* argv[])
{
    ::testing::InitGoogleTest(&argc, argv);
    if (argc > 0) {
        DummyFileHeaderContext::setCreator(argv[0]);
    }
    return RUN_ALL_TESTS();
}
