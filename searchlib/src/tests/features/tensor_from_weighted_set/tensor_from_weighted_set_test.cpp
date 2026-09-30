// Copyright Vespa.ai. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.

#include <vespa/eval/eval/function.h>
#include <vespa/eval/eval/simple_value.h>
#include <vespa/eval/eval/tensor_spec.h>
#include <vespa/eval/eval/value.h>
#include <vespa/eval/eval/test/value_compare.h>
#include <vespa/searchcommon/attribute/attributecontent.h>
#include <vespa/searchcommon/attribute/config.h>
#include <vespa/searchlib/attribute/attribute_read_guard.h>
#include <vespa/searchlib/attribute/attributefactory.h>
#include <vespa/searchlib/attribute/attributevector.h>
#include <vespa/searchlib/attribute/extendableattributes.h>
#include <vespa/searchlib/attribute/integerbase.h>
#include <vespa/searchlib/attribute/multistringattribute.h>
#include <vespa/searchlib/attribute/stringbase.h>
#include <vespa/searchlib/features/setup.h>
#include <vespa/searchlib/features/tensor_from_attribute_executor.h>
#include <vespa/searchlib/features/tensor_from_enum_attribute_executor.h>
#include <vespa/searchlib/features/tensor_from_weighted_set_feature.h>
#include <vespa/searchlib/fef/fef.h>
#include <vespa/searchlib/fef/test/indexenvironment.h>
#include <vespa/searchlib/test/ft_test_app_base.h>
#include <vespa/searchlib/test/imported_attribute_fixture.h>
#include <vespa/vespalib/gtest/gtest.h>
#include <vespa/vespalib/util/shared_string_repo.h>
#include <vespa/vespalib/util/stash.h>

#include <algorithm>
#include <numeric>
#include <random>

using search::feature_t;
using namespace search::fef;
using namespace search::fef::test;
using namespace search::features;
using search::AttributeFactory;
using search::IntegerAttribute;
using search::StringAttribute;
using vespalib::eval::Value;
using vespalib::eval::Function;
using vespalib::eval::TensorSpec;
using vespalib::eval::SimpleValue;
using search::attribute::WeightedConstCharContent;
using search::attribute::WeightedEnumContent;
using vespalib::SharedStringRepo;
using vespalib::datastore::CompactionStrategy;
using vespalib::eval::ValueType;

using AVC = search::attribute::Config;
using AVBT = search::attribute::BasicType;
using AVCT = search::attribute::CollectionType;
using AttributePtr = search::AttributeVector::SP;
using EnumHandle = search::attribute::IAttributeVector::EnumHandle;
using FTA = FtTestAppBase;

Value::UP make_tensor(const TensorSpec &spec) {
    return SimpleValue::from_spec(spec);
}

Value::UP make_empty(const std::string &type) {
    return make_tensor(TensorSpec(type));
}

struct SetupFixture
{
    TensorFromWeightedSetBlueprint blueprint;
    IndexEnvironment indexEnv;
    SetupFixture()
        : blueprint(),
          indexEnv()
    {
    }
};

TEST(TensorFromWeightedSetTest, require_that_blueprint_can_be_created_from_factory)
{
    SetupFixture f;
    EXPECT_TRUE(FTA::assertCreateInstance(f.blueprint, "tensorFromWeightedSet"));
}

TEST(TensorFromWeightedSetTest, require_that_setup_fails_if_source_spec_is_invalid)
{
    SetupFixture f;
    FTA::FT_SETUP_FAIL(f.blueprint, f.indexEnv, StringList().add("source(foo)"));
}

TEST(TensorFromWeightedSetTest, require_that_setup_succeeds_with_attribute_source)
{
    SetupFixture f;
    FTA::FT_SETUP_OK(f.blueprint, f.indexEnv, StringList().add("attribute(foo)"),
            StringList(), StringList().add("tensor"));
}

TEST(TensorFromWeightedSetTest, require_that_setup_succeeds_with_query_source)
{
    SetupFixture f;
    FTA::FT_SETUP_OK(f.blueprint, f.indexEnv, StringList().add("query(foo)"),
            StringList(), StringList().add("tensor"));
}

struct ExecFixture
{
    BlueprintFactory factory;
    FtFeatureTest test;
    ExecFixture(const std::string &feature)
        : factory(),
          test(factory, feature)
    {
        setup_search_features(factory);
        setupAttributeVectors();
        setupQueryEnvironment();
        EXPECT_TRUE(test.setup());
    }
    void setupAttributeVectors() {
        std::vector<AttributePtr> attrs;
        attrs.push_back(AttributeFactory::createAttribute("wsstr", AVC(AVBT::STRING,  AVCT::WSET)));
        attrs.push_back(AttributeFactory::createAttribute("wsint", AVC(AVBT::INT32,  AVCT::WSET)));
        attrs.push_back(AttributeFactory::createAttribute("astr", AVC(AVBT::STRING,  AVCT::ARRAY)));

        for (const auto &attr : attrs) {
            attr->addReservedDoc();
            attr->addDocs(1);
            test.getIndexEnv().getAttributeMap().add(attr);
        }

        StringAttribute *wsstr = static_cast<StringAttribute *>(attrs[0].get());
        wsstr->append(1, "a", 3);
        wsstr->append(1, "b", 5);
        wsstr->append(1, "c", 7);

        IntegerAttribute *wsint = static_cast<IntegerAttribute *>(attrs[1].get());
        wsint->append(1, 11, 3);
        wsint->append(1, 13, 5);
        wsint->append(1, 17, 7);

        for (const auto &attr : attrs) {
            attr->commit();
        }
    }
    void setupQueryEnvironment() {
        test.getQueryEnv().getProperties().add("wsquery", "{d:11,e:13,f:17}");
    }
    const Value &extractTensor(uint32_t docid) {
        return test.resolveObjectFeature(docid);
    }
    const Value &execute() {
        return extractTensor(1);
    }
};

TEST(TensorFromWeightedSetTest, require_that_weighted_set_string_attribute_can_be_converted_to_tensor_using_default_dimension)
{
    ExecFixture f("tensorFromWeightedSet(attribute(wsstr))");
    EXPECT_EQ(*make_tensor(TensorSpec("tensor(wsstr{})")
                           .add({{"wsstr","b"}}, 5)
                           .add({{"wsstr","c"}}, 7)
                           .add({{"wsstr","a"}}, 3)), f.execute());
}

TEST(TensorFromWeightedSetTest, require_that_weighted_set_string_attribute_can_be_converted_to_tensor_using_explicit_dimension)
{
    ExecFixture f("tensorFromWeightedSet(attribute(wsstr),dim)");
    EXPECT_EQ(*make_tensor(TensorSpec("tensor(dim{})")
                           .add({{"dim","a"}}, 3)
                           .add({{"dim","b"}}, 5)
                           .add({{"dim","c"}}, 7)), f.execute());
}

TEST(TensorFromWeightedSetTest, require_that_weighted_set_integer_attribute_can_be_converted_to_tensor_using_default_dimension)
{
    ExecFixture f("tensorFromWeightedSet(attribute(wsint))");
    EXPECT_EQ(*make_tensor(TensorSpec("tensor(wsint{})")
                           .add({{"wsint","13"}}, 5)
                           .add({{"wsint","17"}}, 7)
                           .add({{"wsint","11"}}, 3)), f.execute());
}

TEST(TensorFromWeightedSetTest, require_that_weighted_set_integer_attribute_can_be_converted_to_tensor_using_explicit_dimension)
{
    ExecFixture f("tensorFromWeightedSet(attribute(wsint),dim)");
    EXPECT_EQ(*make_tensor(TensorSpec("tensor(dim{})")
                           .add({{"dim","17"}}, 7)
                           .add({{"dim","11"}}, 3)
                           .add({{"dim","13"}}, 5)), f.execute());
}

TEST(TensorFromWeightedSetTest, require_that_weighted_set_from_query_can_be_converted_to_tensor_using_default_dimension)
{
    ExecFixture f("tensorFromWeightedSet(query(wsquery))");
    EXPECT_EQ(*make_tensor(TensorSpec("tensor(wsquery{})")
                           .add({{"wsquery","f"}}, 17)
                           .add({{"wsquery","d"}}, 11)
                           .add({{"wsquery","e"}}, 13)), f.execute());
}

TEST(TensorFromWeightedSetTest, require_that_weighted_set_from_query_can_be_converted_to_tensor_using_explicit_dimension)
{
    ExecFixture f("tensorFromWeightedSet(query(wsquery),dim)");
    EXPECT_EQ(*make_tensor(TensorSpec("tensor(dim{})")
                           .add({{"dim","d"}}, 11)
                           .add({{"dim","e"}}, 13)
                           .add({{"dim","f"}}, 17)), f.execute());
}

TEST(TensorFromWeightedSetTest, require_that_empty_tensor_is_created_if_attribute_does_not_exists)
{
    ExecFixture f("tensorFromWeightedSet(attribute(null))");
    EXPECT_EQ(*make_empty("tensor(null{})"), f.execute());
}

TEST(TensorFromWeightedSetTest, require_that_empty_tensor_is_created_if_attribute_type_is_not_supported)
{
    ExecFixture f("tensorFromWeightedSet(attribute(astr))");
    EXPECT_EQ(*make_empty("tensor(astr{})"), f.execute());
}

TEST(TensorFromWeightedSetTest, require_that_empty_tensor_is_created_if_query_parameter_is_not_found)
{
    ExecFixture f("tensorFromWeightedSet(query(null))");
    EXPECT_EQ(*make_empty("tensor(null{})"), f.execute());
}

using DocLabels = std::vector<std::pair<std::string, int32_t>>;

// Every 5th document is empty, the others have labels_per_doc labels out of num_labels.
std::vector<DocLabels> make_docs_sharing_labels(uint32_t num_docs, uint32_t labels_per_doc, uint32_t num_labels) {
    std::vector<DocLabels> docs(num_docs + 1);
    for (uint32_t docid = 1; docid <= num_docs; ++docid) {
        if ((docid % 5) == 0) {
            continue;
        }
        for (uint32_t i = 0; i < labels_per_doc; ++i) {
            int32_t weight = int32_t(docid % 9) + 1;
            docs[docid].emplace_back("label_" + std::to_string((docid * 7 + i) % num_labels),
                                     ((i % 2) == 0) ? weight : -weight);
        }
    }
    return docs;
}

// Every label is used by one document only.
std::vector<DocLabels> make_docs_with_unique_labels(uint32_t num_docs, uint32_t labels_per_doc) {
    std::vector<DocLabels> docs(num_docs + 1);
    for (uint32_t docid = 1; docid <= num_docs; ++docid) {
        for (uint32_t i = 0; i < labels_per_doc; ++i) {
            docs[docid].emplace_back("unique_" + std::to_string(docid * labels_per_doc + i), int32_t(i) + 1);
        }
    }
    return docs;
}

void fill_attribute(search::AttributeVector& attr, const std::vector<DocLabels>& docs) {
    attr.addReservedDoc();
    attr.addDocs(docs.size() - 1);
    auto& str_attr = dynamic_cast<StringAttribute&>(attr);
    for (uint32_t docid = 1; docid < docs.size(); ++docid) {
        for (const auto& [label, weight] : docs[docid]) {
            if (attr.hasMultiValue()) {
                str_attr.append(docid, label, weight);
            } else {
                str_attr.update(docid, label);
            }
        }
    }
    attr.commit();
}

AttributePtr make_string_attribute(AVCT::Type collection_type, const std::vector<DocLabels>& docs) {
    auto attr = AttributeFactory::createAttribute("wsstr", AVC(AVBT::STRING, collection_type));
    fill_attribute(*attr, docs);
    return attr;
}

TensorSpec make_spec(const DocLabels& labels) {
    TensorSpec spec("tensor(dim{})");
    for (const auto& [label, weight] : labels) {
        spec.add({{"dim", label}}, weight);
    }
    return spec;
}

std::vector<uint32_t> shuffled_docids(uint32_t num_docs) {
    std::vector<uint32_t> docids(num_docs);
    std::iota(docids.begin(), docids.end(), 1);
    std::shuffle(docids.begin(), docids.end(), std::mt19937(42));
    return docids;
}

size_t active_labels() {
    return SharedStringRepo::stats().active_entries;
}

class CountingWeightedSetStringAttribute : public search::WeightedSetStringAttribute {
public:
    mutable size_t string_lookups;
    explicit CountingWeightedSetStringAttribute(const std::string& name)
        : search::WeightedSetStringAttribute(name, AVC(AVBT::STRING, AVCT::WSET)), string_lookups(0) {}
    const char* getStringFromEnum(EnumHandle e) const override {
        ++string_lookups;
        return search::WeightedSetStringAttribute::getStringFromEnum(e);
    }
};

struct ManyDocsFixture {
    BlueprintFactory factory;
    FtFeatureTest    test;
    // Takes an attribute, or a read guard on it like a query holds
    template <typename AttributeOrGuard>
    explicit ManyDocsFixture(AttributeOrGuard attr)
        : factory(), test(factory, "tensorFromWeightedSet(attribute(wsstr),dim)") {
        setup_search_features(factory);
        test.getIndexEnv().getAttributeMap().add(std::move(attr));
        EXPECT_TRUE(test.setup());
    }
    TensorSpec execute(uint32_t docid) { return TensorSpec::from_value(test.resolveObjectFeature(docid)); }
};

TEST(TensorFromWeightedSetTest, require_that_documents_sharing_string_labels_give_expected_tensors) {
    auto            docs = make_docs_sharing_labels(200, 10, 25);
    ManyDocsFixture f(make_string_attribute(AVCT::WSET, docs));
    auto            docids = shuffled_docids(200);
    for (size_t round = 0; round < 2; ++round) {
        for (uint32_t docid : docids) {
            EXPECT_EQ(make_spec(docs[docid]), f.execute(docid)) << "docid " << docid;
        }
    }
}

TEST(TensorFromWeightedSetTest, require_that_each_distinct_string_label_is_resolved_once) {
    auto docs = make_docs_sharing_labels(200, 10, 25);
    auto attr = std::make_shared<CountingWeightedSetStringAttribute>("wsstr");
    fill_attribute(*attr, docs);
    ManyDocsFixture f(attr);
    attr->string_lookups = 0;
    for (uint32_t docid = 1; docid <= 200; ++docid) {
        EXPECT_EQ(make_spec(docs[docid]), f.execute(docid)) << "docid " << docid;
    }
    EXPECT_EQ(25u, attr->string_lookups);
}

TEST(TensorFromWeightedSetTest, require_that_the_label_cache_is_bounded_and_dropped_when_it_serves_few_lookups) {
    constexpr uint32_t labels_per_doc = 16;
    constexpr size_t   cache_size = TensorFromEnumAttributeExecutor::max_cached_labels;
    constexpr uint32_t docs_per_phase = cache_size / labels_per_doc;
    // Phase 0 fills the cache, phase 1 reuses its labels, phase 2 has new labels that the full cache
    // cannot take, and phase 3 reuses the labels of phase 0 after the cache was dropped.
    std::vector<DocLabels> docs((4 * docs_per_phase) + 1);
    for (uint32_t i = 1; i <= docs_per_phase; ++i) {
        for (uint32_t j = 0; j < labels_per_doc; ++j) {
            docs[i].emplace_back("first_" + std::to_string((i * labels_per_doc) + j), int32_t(j) + 1);
            docs[(2 * docs_per_phase) + i].emplace_back("other_" + std::to_string((i * labels_per_doc) + j),
                                                        int32_t(j) + 1);
        }
        docs[docs_per_phase + i] = docs[i];
        docs[(3 * docs_per_phase) + i] = docs[i];
    }
    auto attr = std::make_shared<CountingWeightedSetStringAttribute>("wsstr");
    fill_attribute(*attr, docs);
    size_t before = active_labels();
    {
        ManyDocsFixture f(attr);
        attr->string_lookups = 0;
        auto run_phase = [&](uint32_t phase) {
            size_t max_held = 0;
            for (uint32_t docid = (phase * docs_per_phase) + 1; docid <= (phase + 1) * docs_per_phase; ++docid) {
                EXPECT_EQ(make_spec(docs[docid]), f.execute(docid)) << "docid " << docid;
                max_held = std::max(max_held, active_labels() - before);
            }
            return max_held;
        };
        run_phase(0);
        EXPECT_EQ(cache_size, attr->string_lookups);
        run_phase(1);
        EXPECT_EQ(cache_size, attr->string_lookups);
        size_t max_held = run_phase(2);
        EXPECT_EQ(2 * cache_size, attr->string_lookups);
        run_phase(3);
        EXPECT_EQ(3 * cache_size, attr->string_lookups);
        if (SharedStringRepo::will_reclaim()) {
            EXPECT_EQ(cache_size + labels_per_doc, max_held);
            EXPECT_EQ(labels_per_doc, active_labels() - before);
        }
    }
    if (SharedStringRepo::will_reclaim()) {
        EXPECT_EQ(before, active_labels());
    }
}

TEST(TensorFromWeightedSetTest,
     require_that_a_document_with_more_distinct_labels_than_the_cache_gives_expected_tensor) {
    constexpr uint32_t     num_labels = TensorFromEnumAttributeExecutor::max_cached_labels + 1000;
    std::vector<DocLabels> docs(4);
    for (uint32_t i = 0; i < num_labels; ++i) {
        int32_t weight = int32_t(i % 100) + 1;
        docs[1].emplace_back("big_" + std::to_string(i), ((i % 2) == 0) ? weight : -weight);
    }
    docs[2] = {{"big_0", 7}, {"big_" + std::to_string(num_labels - 1), 9}, {"small", 1}};
    ManyDocsFixture f(make_string_attribute(AVCT::WSET, docs));
    for (uint32_t docid : {1, 2, 3, 1, 2}) {
        EXPECT_EQ(make_spec(docs[docid]), f.execute(docid)) << "docid " << docid;
    }
}

std::vector<EnumHandle> enum_handles(const search::AttributeVector& attr, uint32_t docid) {
    WeightedEnumContent content;
    content.fill(attr, docid);
    std::vector<EnumHandle> result;
    for (const auto& value : content) {
        result.push_back(value.value());
    }
    return result;
}

TEST(TensorFromWeightedSetTest, require_that_tensors_follow_attribute_changes_while_the_query_holds_a_read_guard) {
    // Enough values that freeing a quarter of them makes the enum store compact
    constexpr uint32_t num_docs = 12288;
    AVC                cfg(AVBT::STRING, AVCT::WSET);
    cfg.setCompactionStrategy(CompactionStrategy(1000.0f, 1000.0f));
    auto  attr = AttributeFactory::createAttribute("wsstr", cfg);
    auto& str_attr = dynamic_cast<StringAttribute&>(*attr);
    attr->addReservedDoc();
    attr->addDocs(num_docs);
    std::vector<DocLabels> docs(num_docs + 1);
    auto                   set_labels = [&](uint32_t docid, DocLabels labels) {
        attr->clearDoc(docid);
        for (const auto& [label, weight] : labels) {
            str_attr.append(docid, label, weight);
        }
        docs[docid] = std::move(labels);
    };
    for (uint32_t docid = 1; docid <= num_docs; ++docid) {
        set_labels(docid, {{"v" + std::to_string(docid * 10), 1},
                           {"v" + std::to_string((docid * 10) + 1), 2},
                           {"shared" + std::to_string(docid % 10), 3}});
    }
    attr->commit();
    for (uint32_t docid = 3; docid <= num_docs; docid += 4) {
        set_labels(docid, {});
    }
    attr->commit();
    // Reclaims the values freed by the previous commit, making them dead space in the enum store
    attr->commit();

    ManyDocsFixture f(attr->makeReadGuard(false));
    auto            verify_all_docs = [&]() {
        for (uint32_t docid = 1; docid <= num_docs; ++docid) {
            EXPECT_EQ(make_spec(docs[docid]), f.execute(docid)) << "docid " << docid;
        }
    };
    verify_all_docs();

    // Compaction moves the values to new enum handles, while the read guard keeps the old ones valid
    std::vector<std::vector<EnumHandle>> handles_before(num_docs + 1);
    for (uint32_t docid = 1; docid <= num_docs; ++docid) {
        handles_before[docid] = enum_handles(*attr, docid);
    }
    attr->update_config(AVC(AVBT::STRING, AVCT::WSET));
    uint32_t moved = 0;
    for (uint32_t docid = 1; docid <= num_docs; ++docid) {
        if (enum_handles(*attr, docid) != handles_before[docid]) {
            ++moved;
        }
    }
    EXPECT_LT(0u, moved);

    // Values freed while the query holds its read guard are held, and not reused for new values
    for (uint32_t docid = 1; docid <= num_docs; docid += 4) {
        set_labels(docid, {});
    }
    attr->commit();
    attr->commit();
    for (uint32_t docid = 1; docid <= num_docs; docid += 4) {
        set_labels(docid, {{"w" + std::to_string(docid), 4}});
    }
    attr->commit();
    verify_all_docs();
}

TEST(TensorFromWeightedSetTest,
     require_that_weighted_set_string_attribute_without_enum_store_can_be_converted_to_tensor) {
    // Streaming search uses attributes like this one
    auto attr = std::make_shared<search::WeightedSetStringExtAttribute>("wsstr");
    ASSERT_FALSE(attr->hasEnum());
    std::vector<DocLabels> docs = {{}, {{"a", 3}, {"b", 5}}, {{"b", 7}}, {}};
    for (uint32_t i = 0; i < docs.size(); ++i) {
        uint32_t docid = 0;
        ASSERT_TRUE(attr->addDoc(docid));
        ASSERT_EQ(i, docid);
        for (const auto& [label, weight] : docs[docid]) {
            attr->add(label.c_str(), weight);
        }
    }
    ManyDocsFixture f(attr);
    for (uint32_t docid : {1, 2, 3, 1}) {
        EXPECT_EQ(make_spec(docs[docid]), f.execute(docid)) << "docid " << docid;
    }
}

struct ExecutorRunner {
    NumberOrObject   output;
    FeatureExecutor& executor;
    explicit ExecutorRunner(FeatureExecutor& executor_in) : output(), executor(executor_in) {
        executor.bind_outputs(std::span<NumberOrObject>(&output, 1));
    }
    const Value& execute(uint32_t docid) {
        executor.lazy_execute(docid);
        return output.as_object.get();
    }
};

// Labels and cells in subspace order, which covers duplicate labels too
std::vector<std::pair<std::string, double>> subspaces(const Value& value) {
    std::vector<std::pair<std::string, double>> result;
    auto                                        cells = value.cells().typify<double>();
    auto                                        view = value.index().create_view({});
    vespalib::string_id                         label;
    vespalib::string_id*                        label_ptr = &label;
    size_t                                      subspace = 0;
    view->lookup({});
    while (view->next_result(std::span<vespalib::string_id* const>(&label_ptr, 1), subspace)) {
        result.emplace_back(SharedStringRepo::Handle::string_from_id(label), cells[subspace]);
    }
    return result;
}

void verify_same_tensors_as_generic_executor(const search::AttributeVector& attr, uint32_t num_docs) {
    ValueType       type = ValueType::from_spec("tensor(dim{})");
    vespalib::Stash stash;
    ExecutorRunner  expect(stash.create<TensorFromAttributeExecutor<WeightedConstCharContent>>(&attr, type));
    ExecutorRunner  actual(stash.create<TensorFromEnumAttributeExecutor>(attr, type));
    auto            docids = shuffled_docids(num_docs);
    auto            again = shuffled_docids(num_docs / 2);
    docids.insert(docids.end(), again.begin(), again.end());
    for (uint32_t docid : docids) {
        const Value& expect_value = expect.execute(docid);
        const Value& actual_value = actual.execute(docid);
        EXPECT_EQ(expect_value.type(), actual_value.type());
        EXPECT_EQ(subspaces(expect_value), subspaces(actual_value)) << "docid " << docid;
    }
}

// Documents with labels shared by many documents and labels only used once, more than the executor caches.
std::vector<DocLabels> make_mixed_docs(uint32_t num_docs, bool single_value, bool with_duplicates) {
    std::vector<DocLabels> docs(num_docs + 1);
    auto                   shared = make_docs_sharing_labels(num_docs, 4, 50);
    auto                   unique = make_docs_with_unique_labels(num_docs, 8);
    for (uint32_t docid = 1; docid <= num_docs; ++docid) {
        if (single_value) {
            if ((docid % 4) != 0) {
                docs[docid].push_back(((docid % 2) == 0) ? unique[docid][0] : std::make_pair(shared[1][0].first, 1));
            }
            continue;
        }
        docs[docid] = shared[docid];
        docs[docid].insert(docs[docid].end(), unique[docid].begin(), unique[docid].end());
        if (with_duplicates && !shared[docid].empty() && ((docid % 3) == 0)) {
            docs[docid].push_back(shared[docid][0]);
        }
    }
    return docs;
}

TEST(TensorFromWeightedSetTest, require_that_enum_executor_gives_same_tensors_as_generic_executor) {
    constexpr uint32_t num_docs = 2500;
    auto               wset = make_string_attribute(AVCT::WSET, make_mixed_docs(num_docs, false, false));
    auto               array = make_string_attribute(AVCT::ARRAY, make_mixed_docs(num_docs, false, true));
    auto               single = make_string_attribute(AVCT::SINGLE, make_mixed_docs(num_docs, true, false));
    ASSERT_TRUE(wset->hasEnum() && array->hasEnum() && single->hasEnum());
    verify_same_tensors_as_generic_executor(*wset, num_docs);
    verify_same_tensors_as_generic_executor(*array, num_docs);
    verify_same_tensors_as_generic_executor(*single, num_docs);
}

TEST(TensorFromWeightedSetTest, require_that_imported_weighted_set_string_attribute_can_be_converted_to_tensor) {
    using search::attribute::DocId;
    using search::attribute::dummy_gid;
    using WeightedString = search::attribute::IAttributeVector::WeightedString;
    search::attribute::ImportedAttributeFixture f;
    std::vector<WeightedString>                 doc7_values{{WeightedString("a", 3), WeightedString("b", 5)}};
    std::vector<WeightedString>                 doc8_values{{WeightedString("b", 7), WeightedString("c", 11)}};
    search::attribute::reset_with_wset_value_reference_mappings<StringAttribute, WeightedString>(
        f, AVBT::STRING,
        {{DocId(3), dummy_gid(7), DocId(7), doc7_values}, {DocId(4), dummy_gid(8), DocId(8), doc8_values}});
    BlueprintFactory factory;
    FtFeatureTest    test(factory, "tensorFromWeightedSet(attribute(imported),dim)");
    setup_search_features(factory);
    auto read_guard = f.imported_attr->makeReadGuard(false);
    ASSERT_TRUE(read_guard->attribute()->hasEnum());
    test.getIndexEnv().getAttributeMap().add(std::move(read_guard));
    ASSERT_TRUE(test.setup());
    for (uint32_t docid : {3, 4, 5, 4, 3}) {
        DocLabels labels;
        if (docid == 3) {
            labels = {{"a", 3}, {"b", 5}};
        } else if (docid == 4) {
            labels = {{"b", 7}, {"c", 11}};
        }
        EXPECT_EQ(make_spec(labels), TensorSpec::from_value(test.resolveObjectFeature(docid))) << "docid " << docid;
    }
}

GTEST_MAIN_RUN_ALL_TESTS()
