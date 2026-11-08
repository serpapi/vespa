// Copyright Vespa.ai. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.

#include <vespa/vespalib/datastore/datastore.h>
#include <vespa/vespalib/gtest/gtest.h>
#include <vespa/vespalib/util/generationholder.h>
#include <vespa/vespalib/util/image_file.h>
#include <vespa/vespalib/util/type_stable_vector.h>

#include <vespa/vespalib/datastore/datastore.hpp>
#include <vespa/vespalib/util/type_stable_vector.hpp>

#include <filesystem>
#include <unistd.h>

using namespace vespalib;
using namespace vespalib::datastore;
using vespalib::image::ImageReader;
using vespalib::image::ImageWriter;

namespace {

std::string image_path(const std::string& name) {
    return "image_file_test_" + name + "_" + std::to_string(getpid()) + ".img";
}

using MyRef = EntryRefT<10, 10>;

class MyStore : public DataStore<uint64_t, MyRef> {
    using ParentType = DataStore<uint64_t, MyRef>;

public:
    MyStore() : ParentType() {}
    void hold_entry(EntryRef ref) { ParentType::hold_entry(ref); }
    void assign_generation(Generation g) { ParentType::assign_generation(g); }
    void reclaim_entry_refs(Generation g) override { ParentType::reclaim_entry_refs(g); }
    void enableFreeLists() { ParentType::enableFreeLists(); }
    void switch_primary_buffer() { ParentType::switch_primary_buffer(0, 0u); }
};

} // namespace

TEST(ImageFileTest, type_stable_vector_round_trip_and_grow_after_load) {
    auto             path = image_path("vector");
    {
        TypeStableVector<uint32_t> v(GrowStrategy(16, 1.0, 0, 0));
        for (uint32_t i = 0; i < 100000; ++i) {
            v.push_back(i * 7);
        }
        ImageWriter writer(path);
        v.save_image(writer);
        writer.meta() << uint32_t(42);
        writer.finish();
    }
    TypeStableVector<uint32_t> loaded(GrowStrategy(16, 1.0, 0, 0));
    {
        ImageReader reader(path);
        loaded.load_image(reader);
        uint32_t tail = 0;
        reader.meta() >> tail;
        EXPECT_EQ(42u, tail);
    }
    ASSERT_EQ(100000u, loaded.size());
    for (uint32_t i = 0; i < 100000; ++i) {
        ASSERT_EQ(i * 7, loaded[i]);
    }
    loaded[5] = 1; // copy-on-write page
    for (uint32_t i = 0; i < 300000; ++i) {
        loaded.push_back(i);
    }
    EXPECT_EQ(1u, loaded[5]);
    EXPECT_EQ(6u * 7, loaded[6]);
    EXPECT_EQ(299999u, loaded[100000 + 299999]);
    loaded.reclaim_memory(Generation(1000));
    std::filesystem::remove(path);
}

TEST(ImageFileTest, data_store_round_trip_keeps_refs_and_free_lists) {
    auto                  path = image_path("datastore");
    std::vector<EntryRef> refs;
    EntryRef              freed;
    {
        MyStore store;
        store.enableFreeLists();
        for (uint64_t i = 0; i < 5000; ++i) {
            refs.push_back(store.addEntry(i * 3 + 1));
            if (i == 2000) {
                store.switch_primary_buffer();
            }
        }
        freed = refs[10];
        store.hold_entry(freed);
        store.assign_generation(Generation(1));
        store.reclaim_entry_refs(Generation(2)); // freed is now on the free list
        ImageWriter writer(path);
        ASSERT_TRUE(store.save_image(writer));
        writer.finish();
    }
    MyStore loaded;
    loaded.enableFreeLists();
    {
        ImageReader reader(path);
        loaded.load_image(reader);
    }
    for (uint64_t i = 0; i < refs.size(); ++i) {
        if (refs[i] != freed) {
            ASSERT_EQ(i * 3 + 1, loaded.getEntry(refs[i])) << i;
        }
    }
    auto reused = loaded.freeListAllocator<uint64_t, DefaultReclaimer<uint64_t>>(0).alloc(77).ref;
    EXPECT_EQ(freed, reused);
    EXPECT_EQ(77u, loaded.getEntry(reused));
    auto fresh = loaded.addEntry(99);
    EXPECT_EQ(99u, loaded.getEntry(fresh));
    EXPECT_EQ(1u + 2000 * 3, loaded.getEntry(refs[2000]));
    std::filesystem::remove(path);
}

GTEST_MAIN_RUN_ALL_TESTS()
