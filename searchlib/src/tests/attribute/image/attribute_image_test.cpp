// Copyright Vespa.ai. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.

#include <vespa/searchcommon/attribute/config.h>
#include <vespa/searchlib/attribute/attributefactory.h>
#include <vespa/searchlib/attribute/attributevector.h>
#include <vespa/searchlib/attribute/integerbase.h>
#include <vespa/searchlib/attribute/stringbase.h>
#include <vespa/searchlib/fef/termfieldmatchdata.h>
#include <vespa/searchlib/index/dummyfileheadercontext.h>
#include <vespa/searchlib/parsequery/parse.h>
#include <vespa/searchlib/queryeval/executeinfo.h>
#include <vespa/searchlib/queryeval/searchiterator.h>
#include <vespa/searchcommon/attribute/search_context_params.h>
#include <vespa/vespalib/gtest/gtest.h>
#include <vespa/vespalib/util/compress.h>
#include <vespa/vespalib/util/image_file.h>
#include <vespa/vespalib/util/mmap_file_allocator_factory.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>

using search::AttributeFactory;
using search::AttributeVector;
using search::IntegerAttribute;
using search::StringAttribute;
using search::attribute::BasicType;
using search::attribute::CollectionType;
using search::attribute::Config;

namespace {

constexpr uint32_t NUM_DOCS = 200000;
const std::string  dir = "attribute_image_test_dir";

std::string long_string(uint32_t doc) {
    return "long-" + std::to_string(doc % 50) + std::string(300, 'x');
}

std::string string_value(uint32_t doc) {
    if (doc % 1000 == 7) {
        return long_string(doc); // external (heap) string in the unique store
    }
    return "v" + std::to_string(doc % 997);
}

void fill(AttributeVector& attr, uint32_t first, uint32_t last, uint32_t salt) {
    auto* ia = dynamic_cast<IntegerAttribute*>(&attr);
    auto* sa = dynamic_cast<StringAttribute*>(&attr);
    for (uint32_t doc = first; doc < last; ++doc) {
        attr.clearDoc(doc);
        if (attr.hasMultiValue()) {
            uint32_t n = (doc % 500 == 3) ? 400 : 1 + doc % 4; // some large arrays
            for (uint32_t i = 0; i < n; ++i) {
                sa->append(doc, "q" + std::to_string((doc + i * 7 + salt) % 5000), int32_t(1 + i));
            }
        } else if (ia != nullptr) {
            ia->update(doc, int64_t((doc + salt) % 3)); // few values: big posting lists and bitvectors
        } else {
            sa->update(doc, string_value(doc + salt));
        }
    }
    attr.commit();
}

std::string dump(const AttributeVector& attr, uint32_t doc) {
    std::vector<AttributeVector::WeightedString> values(attr.getMaxValueCount() + 1);
    uint32_t                                     n = attr.get(doc, values.data(), values.size());
    std::vector<std::string>                     out;
    for (uint32_t i = 0; i < n; ++i) {
        out.push_back(values[i].getValue() + ":" + std::to_string(values[i].getWeight()));
    }
    std::sort(out.begin(), out.end());
    std::string result;
    for (auto& s : out) {
        result += s + ",";
    }
    return result;
}

uint32_t count_hits(const AttributeVector& attr, const std::string& term) {
    std::vector<char> buffer(term.size() + attr.getName().size() + 16);
    uint32_t          p = 0;
    buffer[p++] = search::ParseItem::ITEM_TERM;
    p += vespalib::compress::Integer::compressPositive(attr.getName().size(), &buffer[p]);
    memcpy(&buffer[p], attr.getName().data(), attr.getName().size());
    p += attr.getName().size();
    p += vespalib::compress::Integer::compressPositive(term.size(), &buffer[p]);
    memcpy(&buffer[p], term.data(), term.size());
    p += term.size();
    auto sc = attr.getSearch(std::string_view(buffer.data(), p), search::attribute::SearchContextParams());
    sc->fetchPostings(search::queryeval::ExecuteInfo::FULL, true);
    search::fef::TermFieldMatchData md;
    auto                            it = sc->createIterator(&md, true);
    it->initRange(1, attr.getCommittedDocIdLimit());
    uint32_t hits = 0;
    for (it->seek(1); !it->isAtEnd(); it->seek(it->getDocId() + 1)) {
        ++hits;
    }
    return hits;
}

struct Case {
    std::string    name;
    BasicType      basic;
    CollectionType collection;
    bool           fast_search;
    bool           paged;
};

void PrintTo(const Case& c, std::ostream* os) {
    *os << c.name;
}

class AttributeImageTest : public ::testing::TestWithParam<Case> {};

} // namespace

TEST_P(AttributeImageTest, image_round_trip_matches_original) {
    const Case& c = GetParam();
    Config      cfg(c.basic, c.collection);
    cfg.setFastSearch(c.fast_search);
    cfg.setPaged(c.paged);
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    std::string base = dir + "/" + c.name;

    auto original = AttributeFactory::createAttribute(base, cfg);
    original->addReservedDoc();
    uint32_t doc_id = 0;
    for (uint32_t i = 1; i < NUM_DOCS; ++i) {
        original->addDoc(doc_id);
    }
    fill(*original, 1, NUM_DOCS, 0);
    search::index::DummyFileHeaderContext header_context;
    ASSERT_TRUE(original->save_image_file(base, header_context));
    ASSERT_TRUE(std::filesystem::exists(base + ".img"));

    auto loaded = AttributeFactory::createAttribute(base, cfg);
    ASSERT_TRUE(loaded->has_image_file());
    ASSERT_TRUE(loaded->load());
    EXPECT_EQ(original->getMaxValueCount(), loaded->getMaxValueCount());
    EXPECT_EQ(original->getNumDocs(), loaded->getNumDocs());
    EXPECT_EQ(original->getCommittedDocIdLimit(), loaded->getCommittedDocIdLimit());
    for (uint32_t doc = 1; doc < NUM_DOCS; ++doc) {
        ASSERT_EQ(dump(*original, doc), dump(*loaded, doc)) << "doc " << doc;
    }
    std::vector<std::string> terms = c.basic == BasicType::INT32
                                         ? std::vector<std::string>{"0", "1", "2"}
                                         : std::vector<std::string>{"v1", "v996", "q17", "q4999", long_string(7)};
    for (const auto& term : terms) {
        EXPECT_EQ(count_hits(*original, term), count_hits(*loaded, term)) << term;
    }
    // Writes after load: new values, removals and new documents behave as on the original.
    fill(*original, 1, NUM_DOCS / 10, 1);
    fill(*loaded, 1, NUM_DOCS / 10, 1);
    for (uint32_t i = 0; i < 1000; ++i) {
        original->addDoc(doc_id);
        loaded->addDoc(doc_id);
    }
    fill(*original, NUM_DOCS, NUM_DOCS + 1000, 2);
    fill(*loaded, NUM_DOCS, NUM_DOCS + 1000, 2);
    for (uint32_t doc = 1; doc < NUM_DOCS + 1000; doc += 13) {
        ASSERT_EQ(dump(*original, doc), dump(*loaded, doc)) << "doc " << doc;
    }
    for (const auto& term : terms) {
        EXPECT_EQ(count_hits(*original, term), count_hits(*loaded, term)) << term;
    }
    loaded.reset();
    original.reset();
    std::filesystem::remove_all(dir);
}

INSTANTIATE_TEST_SUITE_P(
    AllSchemaTypes, AttributeImageTest,
    ::testing::Values(Case{"int32", BasicType::INT32, CollectionType::SINGLE, false, false},
                      Case{"int32_fast_search", BasicType::INT32, CollectionType::SINGLE, true, false},
                      Case{"string", BasicType::STRING, CollectionType::SINGLE, false, false},
                      Case{"string_paged", BasicType::STRING, CollectionType::SINGLE, false, true},
                      Case{"string_fast_search", BasicType::STRING, CollectionType::SINGLE, true, false},
                      Case{"wset_string", BasicType::STRING, CollectionType::WSET, false, false},
                      Case{"wset_string_fast_search", BasicType::STRING, CollectionType::WSET, true, false}),
    [](const ::testing::TestParamInfo<Case>& param_info) { return param_info.param.name; });

int main(int argc, char** argv) {
    setenv("VESPA_IMAGE_RESTART", "1", 1);
    vespalib::alloc::MmapFileAllocatorFactory::instance().setup(dir + "_swap");
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    vespalib::alloc::MmapFileAllocatorFactory::instance().setup("");
    std::filesystem::remove_all(dir + "_swap");
    return result;
}
