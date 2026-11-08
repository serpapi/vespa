// Copyright Vespa.ai. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.

#include <vespa/fastos/file.h>
#include <vespa/searchcommon/attribute/config.h>
#include <vespa/searchlib/attribute/attributefactory.h>
#include <vespa/searchlib/attribute/attributevector.h>
#include <vespa/searchlib/attribute/delta_checkpoint.h>
#include <vespa/searchlib/attribute/floatbase.h>
#include <vespa/searchlib/attribute/integerbase.h>
#include <vespa/searchlib/util/file_with_header.h>
#include <vespa/searchlib/util/fileutil.h>
#include <vespa/vespalib/gtest/gtest.h>

#include <cmath>
#include <cstdlib>
#include <filesystem>

using search::AttributeFactory;
using search::AttributeVector;
using search::FileUtil;
using search::FileWithHeader;
using search::FloatingPointAttribute;
using search::IntegerAttribute;
using search::attribute::BasicType;
using search::attribute::CollectionType;
using search::attribute::Config;
using search::attribute::DeltaCheckpoints;

namespace fs = std::filesystem;

namespace {

const std::string test_dir = "delta_checkpoint_test_data";

std::string snapshot(const std::string& name) {
    fs::create_directories(test_dir + "/" + name);
    return test_dir + "/" + name + "/attr";
}

std::shared_ptr<AttributeVector> make_attribute(const std::string& base_file_name, BasicType type = BasicType::INT32) {
    return AttributeFactory::createAttribute(base_file_name, Config(type, CollectionType::SINGLE));
}

std::shared_ptr<AttributeVector> load(const std::string& base_file_name, BasicType type = BasicType::INT32) {
    auto attr = make_attribute(base_file_name, type);
    EXPECT_TRUE(attr->load());
    return attr;
}

void set(AttributeVector& attr, uint32_t lid, int64_t value) {
    static_cast<IntegerAttribute&>(attr).update(lid, value);
}

void expect_same(const AttributeVector& expected, const AttributeVector& actual) {
    ASSERT_EQ(expected.getCommittedDocIdLimit(), actual.getCommittedDocIdLimit());
    for (uint32_t lid = 0; lid < expected.getCommittedDocIdLimit(); ++lid) {
        const double e = expected.getFloat(lid);
        const double a = actual.getFloat(lid);
        ASSERT_TRUE(e == a || (std::isnan(e) && std::isnan(a))) << "lid " << lid << ": " << e << " vs " << a;
    }
}

bool is_delta(const std::string& base_file_name) {
    FileWithHeader dat(FileUtil::openFile(base_file_name + ".dat"));
    return DeltaCheckpoints::is_delta(dat.header());
}

uint64_t delta_entries(const std::string& base_file_name) {
    FileWithHeader dat(FileUtil::openFile(base_file_name + ".dat"));
    return dat.header().getTag(DeltaCheckpoints::delta_entries_tag).asInteger();
}

class DeltaCheckpointTest : public ::testing::Test {
protected:
    std::shared_ptr<AttributeVector> _attr;

    DeltaCheckpointTest() {
        setenv("VESPA_DELTA_ATTRIBUTE_FULL_PERCENT", "10", 1);
        fs::remove_all(test_dir);
        _attr = make_attribute("live");
        _attr->addDocs(1000);
        for (uint32_t lid = 1; lid < 1000; ++lid) {
            set(*_attr, lid, lid * 3);
        }
        _attr->commit();
        _attr->enable_delta_checkpoints();
    }
    ~DeltaCheckpointTest() override { fs::remove_all(test_dir); }

    bool save(const std::string& base_file_name) { return _attr->save(base_file_name); }
};

} // namespace

TEST_F(DeltaCheckpointTest, a_delta_holds_the_changes_since_the_full_checkpoint_and_loads_without_older_snapshots) {
    const auto full = snapshot("snapshot-1");
    ASSERT_TRUE(save(full));
    EXPECT_FALSE(is_delta(full));

    set(*_attr, 5, 77);
    set(*_attr, 999, -1);
    _attr->clearDoc(10);
    uint32_t first_new = 0;
    uint32_t last_new = 0;
    _attr->addDocs(first_new, last_new, 5);
    set(*_attr, last_new, 1234);
    _attr->commit();
    const auto delta = snapshot("snapshot-2");
    ASSERT_TRUE(save(delta));
    EXPECT_TRUE(is_delta(delta));
    EXPECT_EQ(4u, delta_entries(delta));
    EXPECT_TRUE(fs::exists(delta + ".base.dat"));
    EXPECT_LT(fs::file_size(delta + ".dat"), fs::file_size(delta + ".base.dat"));

    fs::remove_all(test_dir + "/snapshot-1");
    expect_same(*_attr, *load(delta));
}

TEST_F(DeltaCheckpointTest, deltas_are_cumulative_and_continue_after_loading_a_delta) {
    ASSERT_TRUE(save(snapshot("snapshot-1")));
    set(*_attr, 1, 11);
    _attr->commit();
    ASSERT_TRUE(save(snapshot("snapshot-2")));
    set(*_attr, 1, 12);
    set(*_attr, 2, 22);
    _attr->commit();
    const auto second = snapshot("snapshot-3");
    ASSERT_TRUE(save(second));
    EXPECT_EQ(2u, delta_entries(second));
    fs::remove_all(test_dir + "/snapshot-1");
    fs::remove_all(test_dir + "/snapshot-2");

    // A restart: the loaded attribute bases its next delta on the same full checkpoint.
    auto loaded = load(second);
    expect_same(*_attr, *loaded);
    set(*loaded, 3, 33);
    loaded->commit();
    const auto third = snapshot("snapshot-4");
    ASSERT_TRUE(loaded->save(third));
    EXPECT_TRUE(is_delta(third));
    EXPECT_EQ(3u, delta_entries(third));
    fs::remove_all(test_dir + "/snapshot-3");
    expect_same(*loaded, *load(third));
}

TEST_F(DeltaCheckpointTest, a_full_checkpoint_is_written_when_the_delta_reaches_the_full_ratio) {
    ASSERT_TRUE(save(snapshot("snapshot-1")));
    for (uint32_t lid = 1; lid < 200; ++lid) { // 199 entries of 8 bytes > 10 % of 1000 x 4 bytes
        set(*_attr, lid, 5);
    }
    _attr->commit();
    const auto next = snapshot("snapshot-2");
    ASSERT_TRUE(save(next));
    EXPECT_FALSE(is_delta(next));
    EXPECT_FALSE(fs::exists(next + ".base.dat"));
    set(*_attr, 7, 8);
    _attr->commit();
    const auto delta = snapshot("snapshot-3");
    ASSERT_TRUE(save(delta));
    EXPECT_EQ(1u, delta_entries(delta));
    fs::remove_all(test_dir + "/snapshot-1");
    fs::remove_all(test_dir + "/snapshot-2");
    expect_same(*_attr, *load(delta));
}

TEST_F(DeltaCheckpointTest, a_delta_whose_base_is_gone_fails_and_the_next_checkpoint_is_full) {
    ASSERT_TRUE(save(snapshot("snapshot-1")));
    fs::remove_all(test_dir + "/snapshot-1");
    set(*_attr, 1, 2);
    _attr->commit();
    EXPECT_FALSE(save(snapshot("snapshot-2")));
    const auto next = snapshot("snapshot-3");
    ASSERT_TRUE(save(next));
    EXPECT_FALSE(is_delta(next));
    expect_same(*_attr, *load(next));
}

TEST_F(DeltaCheckpointTest, shrinking_the_lid_space_makes_the_next_checkpoint_full) {
    ASSERT_TRUE(save(snapshot("snapshot-1")));
    _attr->compactLidSpace(500);
    _attr->commit();
    ASSERT_TRUE(_attr->canShrinkLidSpace());
    _attr->shrinkLidSpace();
    uint32_t first_new = 0;
    uint32_t last_new = 0;
    _attr->addDocs(first_new, last_new, 100);
    _attr->commit();
    const auto next = snapshot("snapshot-2");
    ASSERT_TRUE(save(next));
    EXPECT_FALSE(is_delta(next));
    fs::remove_all(test_dir + "/snapshot-1");
    auto loaded = load(next);
    expect_same(*_attr, *loaded);
    EXPECT_EQ(_attr->getFloat(550), loaded->getFloat(550)); // undefined, not the old 1650
}

TEST_F(DeltaCheckpointTest, a_float_attribute_round_trips_through_a_delta) {
    auto attr = make_attribute("live_double", BasicType::DOUBLE);
    attr->addDocs(100);
    attr->commit();
    attr->enable_delta_checkpoints();
    ASSERT_TRUE(attr->save(snapshot("double-1")));
    static_cast<FloatingPointAttribute&>(*attr).update(42, 0.25);
    attr->commit();
    const auto delta = snapshot("double-2");
    ASSERT_TRUE(attr->save(delta));
    EXPECT_TRUE(is_delta(delta));
    fs::remove_all(test_dir + "/double-1");
    expect_same(*attr, *load(delta, BasicType::DOUBLE));
}

TEST(DeltaCheckpointDisabledTest, an_attribute_without_delta_checkpoints_saves_a_plain_file) {
    fs::remove_all(test_dir);
    auto attr = make_attribute("plain");
    attr->addDocs(10);
    attr->commit();
    const auto file = snapshot("plain");
    ASSERT_TRUE(attr->save(file));
    FileWithHeader dat(FileUtil::openFile(file + ".dat"));
    EXPECT_FALSE(dat.header().hasTag(DeltaCheckpoints::checkpoint_id_tag));
    fs::remove_all(test_dir);
}

GTEST_MAIN_RUN_ALL_TESTS()
