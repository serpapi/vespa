// Copyright Vespa.ai. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.

// Benchmark for tensorFromWeightedSet over a weightedset<string> attribute, alone and in a sparse
// dot product with a query tensor, compared with the dotProduct feature computing the same value.
// Every thread sets up its own rank program, like a match thread. In split mode the threads run
// one query and rank a share of the documents each; in queries mode every thread runs its own
// query over all documents. The time includes tearing down the rank programs, but not setting
// them up.
//
// usage: searchlib_tensor_from_weighted_set_benchmark_app [docs] [repeats] [threads] [resident labels]
//                                                         [split|queries] [query labels]
//
// Threads is a comma-separated list of thread counts. Resident labels are held in the
// SharedStringRepo while benchmarking, like the labels of mapped tensor attributes and other
// queries in a running search node. Voluntary context switches are counted per query; a thread
// blocking on a contended mutex causes them. Each label distribution is a test, so
// --gtest_filter selects them.

#include <vespa/eval/eval/fast_value.h>
#include <vespa/eval/eval/tensor_spec.h>
#include <vespa/eval/eval/value_codec.h>
#include <vespa/searchcommon/attribute/config.h>
#include <vespa/searchlib/attribute/attributefactory.h>
#include <vespa/searchlib/attribute/attributevector.h>
#include <vespa/searchlib/attribute/stringbase.h>
#include <vespa/searchlib/features/setup.h>
#include <vespa/searchlib/fef/blueprintfactory.h>
#include <vespa/searchlib/fef/blueprintresolver.h>
#include <vespa/searchlib/fef/feature_resolver.h>
#include <vespa/searchlib/fef/indexproperties.h>
#include <vespa/searchlib/fef/matchdatalayout.h>
#include <vespa/searchlib/fef/rank_program.h>
#include <vespa/searchlib/fef/test/indexenvironment.h>
#include <vespa/searchlib/fef/test/indexenvironmentbuilder.h>
#include <vespa/searchlib/fef/test/queryenvironment.h>
#include <vespa/vespalib/gtest/gtest.h>
#include <vespa/vespalib/objects/nbostream.h>
#include <vespa/vespalib/test/nexus.h>
#include <vespa/vespalib/util/shared_string_repo.h>
#include <vespa/vespalib/util/time.h>

#include <sys/resource.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <random>
#include <sstream>

using namespace search::fef;
using namespace search::fef::test;
using search::AttributeFactory;
using search::AttributeVector;
using search::StringAttribute;
using vespalib::steady_clock;
using vespalib::to_s;
using vespalib::eval::FastValueBuilderFactory;
using vespalib::eval::TensorSpec;
using vespalib::test::Nexus;

using AVC = search::attribute::Config;
using AVBT = search::attribute::BasicType;
using AVCT = search::attribute::CollectionType;

size_t              num_docs = 50000;
size_t              labels_per_doc = 40;
size_t              num_repeats = 7;
std::vector<size_t> thread_counts = {1, 4, 16};
size_t              resident_labels = 0;
bool                concurrent_queries = false;
size_t              num_query_labels = 40;

uint64_t voluntary_context_switches() {
#ifdef RUSAGE_THREAD
    struct rusage usage;
    getrusage(RUSAGE_THREAD, &usage);
    return usage.ru_nvcsw;
#else
    return 0;
#endif
}

std::string label_name(size_t id) {
    char buf[32];
    snprintf(buf, sizeof(buf), "label_%07zu", id);
    return buf;
}

using DocLabels = std::vector<std::pair<size_t, int32_t>>;
using LabelGenerator = std::function<DocLabels(uint32_t docid, std::mt19937& rnd)>;
using QueryLabels = std::function<size_t(size_t i)>;

// Labels of query(q), weight 1 each; label i is used by the first i+1 query labels
size_t spread_query_label(size_t i) noexcept {
    return (i * 7919) % 500000;
}

size_t most_common_query_label(size_t i) noexcept {
    return i;
}

size_t most_common_universe_label(size_t i) noexcept {
    return 5000 + i;
}

// Draws label ids 0..(size-1) with probability proportional to 1/(id+1)^exponent
class ZipfLabels {
    std::vector<double> _cdf;

public:
    ZipfLabels(size_t size, double exponent) : _cdf(size) {
        double sum = 0.0;
        for (size_t i = 0; i < size; ++i) {
            sum += std::pow(double(i + 1), -exponent);
            _cdf[i] = sum;
        }
        for (double& x : _cdf) {
            x /= sum;
        }
    }
    size_t operator()(std::mt19937& rnd) const {
        std::uniform_real_distribution<double> uniform(0.0, 1.0);
        size_t id = std::lower_bound(_cdf.begin(), _cdf.end(), uniform(rnd)) - _cdf.begin();
        return std::min(id, _cdf.size() - 1);
    }
};

// Same formula as a uniform synthetic corpus with a 500k label vocabulary; labels rarely repeat.
DocLabels uniform_labels(uint32_t docid, std::mt19937& rnd) {
    constexpr size_t                      vocab = 500000;
    DocLabels                             result;
    size_t                                start = (docid * 37) % vocab;
    std::uniform_int_distribution<size_t> offset(0, 996);
    for (size_t j = 0; j < labels_per_doc; ++j) {
        size_t id = (start + j * 7919 + offset(rnd)) % vocab;
        result.emplace_back(id, 1 + ((docid + j) % 7));
    }
    return result;
}

// Zipf(1.0) over 500k labels; some labels are shared by many documents.
LabelGenerator zipf_labels() {
    constexpr size_t    vocab = 500000;
    std::vector<double> cdf(vocab);
    double              sum = 0.0;
    for (size_t i = 0; i < vocab; ++i) {
        sum += 1.0 / double(i + 1);
        cdf[i] = sum;
    }
    for (double& x : cdf) {
        x /= sum;
    }
    return [cdf = std::move(cdf)](uint32_t docid, std::mt19937& rnd) {
        std::uniform_real_distribution<double> uniform(0.0, 1.0);
        DocLabels                              result;
        while (result.size() < labels_per_doc) {
            size_t id = std::lower_bound(cdf.begin(), cdf.end(), uniform(rnd)) - cdf.begin();
            id = std::min(id, cdf.size() - 1);
            if (std::none_of(result.begin(), result.end(), [id](const auto& p) { return p.first == id; })) {
                result.emplace_back(id, 1 + ((docid + result.size()) % 7));
            }
        }
        return result;
    };
}

// All labels come from a vocabulary of 1000 labels.
DocLabels small_vocab_labels(uint32_t docid, std::mt19937& rnd) {
    std::uniform_int_distribution<size_t> pick(0, 999);
    DocLabels                             result;
    while (result.size() < labels_per_doc) {
        size_t id = pick(rnd);
        if (std::none_of(result.begin(), result.end(), [id](const auto& p) { return p.first == id; })) {
            result.emplace_back(id, 1 + ((docid + result.size()) % 7));
        }
    }
    return result;
}

// Heavy-tailed label counts: 21% of the documents have no labels, the median is 4, the 90th
// percentile 78 and the maximum 1904. With probability topical a label is drawn from a pool of
// 5000 labels shared by the documents a query ranks, otherwise from a universe of 10M labels.
LabelGenerator heavy_tailed_labels(double topical) {
    auto topic = std::make_shared<ZipfLabels>(5000, 0.6);
    auto universe = std::make_shared<ZipfLabels>(10000000, 0.85);
    return [topic, universe, topical](uint32_t docid, std::mt19937& rnd) {
        std::bernoulli_distribution   no_labels(0.21);
        std::lognormal_distribution<> count(2.068, 2.006);
        std::bernoulli_distribution   from_topic(topical);
        std::vector<size_t>           ids;
        size_t wanted = no_labels(rnd) ? 0 : std::clamp<size_t>(std::lround(count(rnd)), 1, 1904);
        while (ids.size() < wanted) {
            while (ids.size() < wanted) {
                ids.push_back(from_topic(rnd) ? (*topic)(rnd) : 5000 + (*universe)(rnd));
            }
            std::sort(ids.begin(), ids.end());
            ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
        }
        std::shuffle(ids.begin(), ids.end(), rnd);
        DocLabels result;
        for (size_t id : ids) {
            result.emplace_back(id, 1 + ((docid + result.size()) % 7));
        }
        return result;
    };
}

AttributeVector::SP make_attribute(const LabelGenerator& gen) {
    auto attr = AttributeFactory::createAttribute("labels", AVC(AVBT::STRING, AVCT::WSET));
    attr->addReservedDoc();
    attr->addDocs(num_docs);
    auto&        str_attr = dynamic_cast<StringAttribute&>(*attr);
    std::mt19937 rnd(7);
    for (uint32_t docid = 1; docid <= num_docs; ++docid) {
        for (const auto& [id, weight] : gen(docid, rnd)) {
            str_attr.append(docid, label_name(id), weight);
        }
        if ((docid % 1000) == 0) {
            attr->commit();
        }
    }
    attr->commit();
    return attr;
}

// query(q) with weight 1 for each label, as a tensor and as a vector for the dotProduct feature
std::string make_query_tensor(const QueryLabels& query_label) {
    TensorSpec spec("tensor(k{})");
    for (size_t i = 0; i < num_query_labels; ++i) {
        spec.add({{"k", label_name(query_label(i))}}, 1.0);
    }
    auto                value = vespalib::eval::value_from_spec(spec, FastValueBuilderFactory::get());
    vespalib::nbostream stream;
    vespalib::eval::encode_value(*value, stream);
    return {stream.peek(), stream.size()};
}

std::string make_query_vector(const QueryLabels& query_label) {
    std::string result("{");
    for (size_t i = 0; i < num_query_labels; ++i) {
        result.append(i > 0 ? "," : "").append(label_name(query_label(i))).append(":1");
    }
    return result.append("}");
}

struct Setup {
    BlueprintFactory      factory;
    IndexEnvironment      index_env;
    BlueprintResolver::SP resolver;
    std::string           query_tensor;
    std::string           query_vector;
    Setup(AttributeVector::SP attr, const std::string& feature, const QueryLabels& query_label)
        : factory(), index_env(), resolver(), query_tensor(), query_vector() {
        search::features::setup_search_features(factory);
        IndexEnvironmentBuilder(index_env).addField(FieldType::ATTRIBUTE, FieldInfo::CollectionType::WEIGHTEDSET,
                                                    FieldInfo::DataType::STRING, "labels");
        index_env.getAttributeMap().add(std::move(attr));
        indexproperties::type::QueryFeature::set(index_env.getProperties(), "q", "tensor(k{})");
        index_env.getProperties().add("rankingExpression(first_phase).rankingScript",
                                      "reduce(tensorFromWeightedSet(attribute(labels),k)*query(q),sum)");
        resolver = std::make_shared<BlueprintResolver>(factory, index_env);
        resolver->addSeed(feature);
        EXPECT_TRUE(resolver->compile());
        query_tensor = make_query_tensor(query_label);
        query_vector = make_query_vector(query_label);
    }
    ~Setup();
};
Setup::~Setup() = default;

struct Query {
    QueryEnvironment env;
    MatchDataLayout  layout;
    MatchData::UP    match_data;
    RankProgram      program;
    Query(Setup& setup)
        : env(&setup.index_env), layout(), match_data(layout.createMatchData()), program(setup.resolver) {
        env.getProperties().add("query(q)", setup.query_tensor);
        env.getProperties().add("dotProduct.q", setup.query_vector);
        for (const auto& spec : setup.resolver->getExecutorSpecs()) {
            spec.blueprint->prepareSharedState(env, env.getObjectStore());
        }
        program.setup(*match_data, env);
    }
    ~Query();
};
Query::~Query() = default;

struct Result {
    double   seconds = 0.0;
    double   checksum = 0.0;
    uint64_t context_switches = 0;
};

Result rank_all_documents(Setup& setup, size_t num_threads) {
    auto task = [&](Nexus& ctx) {
        auto     query = std::make_unique<Query>(setup);
        auto     seeds = query->program.get_seeds(false);
        uint32_t begin = concurrent_queries ? 1 : 1 + (num_docs * ctx.thread_id()) / num_threads;
        uint32_t end = concurrent_queries ? num_docs + 1 : 1 + (num_docs * (ctx.thread_id() + 1)) / num_threads;
        Result   result;
        ctx.barrier();
        uint64_t switches_before = voluntary_context_switches();
        auto     start = steady_clock::now();
        for (uint32_t docid = begin; docid < end; ++docid) {
            if (seeds.is_object(0)) {
                result.checksum += seeds.resolve(0).as_object(docid).get().index().size();
            } else {
                result.checksum += seeds.resolve(0).as_number(docid);
            }
        }
        query.reset();
        result.seconds = to_s(steady_clock::now() - start);
        result.context_switches = voluntary_context_switches() - switches_before;
        return result;
    };
    auto merge = [](Result a, Result b) {
        return Result{std::max(a.seconds, b.seconds), a.checksum + b.checksum,
                      a.context_switches + b.context_switches};
    };
    return Nexus::run(num_threads, task, merge);
}

void benchmark(const std::string& name, const LabelGenerator& gen, const QueryLabels& query_label) {
    auto attr = make_attribute(gen);
    for (const std::string feature :
         {"tensorFromWeightedSet(attribute(labels),k)", "rankingExpression(first_phase)", "dotProduct(labels,q)"})
    {
        Setup setup(attr, feature, query_label);
        for (size_t num_threads : thread_counts) {
            std::vector<double>   times;
            std::vector<uint64_t> switches;
            double                checksum = 0.0;
            for (size_t i = 0; i < num_repeats; ++i) {
                auto result = rank_all_documents(setup, num_threads);
                times.push_back(result.seconds);
                switches.push_back(result.context_switches);
                checksum = result.checksum;
            }
            std::sort(times.begin(), times.end());
            std::sort(switches.begin(), switches.end());
            double median = times[times.size() / 2];
            size_t num_queries = concurrent_queries ? num_threads : 1;
            double docs_per_thread = concurrent_queries ? num_docs : double(num_docs) / num_threads;
            fprintf(stderr,
                    "%-12s %-7s resident: %7zu %-43s query labels: %2zu threads: %2zu, ms per query: %7.2f "
                    "(min %7.2f), ns per doc and thread: %6.0f, context switches per query: %7.0f, checksum: %g\n",
                    name.c_str(), concurrent_queries ? "queries" : "split", resident_labels, feature.c_str(),
                    num_query_labels, num_threads, median * 1000.0, times[0] * 1000.0, median * 1e9 / docs_per_thread,
                    double(switches[switches.size() / 2]) / num_queries, checksum);
        }
    }
}

TEST(TensorFromWeightedSetBenchmark, uniform_labels) {
    benchmark("uniform", uniform_labels, spread_query_label);
}

TEST(TensorFromWeightedSetBenchmark, zipf_labels) {
    benchmark("zipf", zipf_labels(), spread_query_label);
}

TEST(TensorFromWeightedSetBenchmark, small_vocabulary) {
    benchmark("vocab_1000", small_vocab_labels, spread_query_label);
}

TEST(TensorFromWeightedSetBenchmark, heavy_tailed_topical_labels) {
    benchmark("tail_topical", heavy_tailed_labels(0.75), most_common_query_label);
}

TEST(TensorFromWeightedSetBenchmark, heavy_tailed_scattered_labels) {
    benchmark("tail_scatter", heavy_tailed_labels(0.0), most_common_universe_label);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    if (argc > 1) {
        num_docs = std::stoul(argv[1]);
    }
    if (argc > 2) {
        num_repeats = std::stoul(argv[2]);
    }
    if (argc > 3) {
        thread_counts.clear();
        std::istringstream list(argv[3]);
        for (std::string count; std::getline(list, count, ',');) {
            thread_counts.push_back(std::stoul(count));
        }
    }
    if (argc > 4) {
        resident_labels = std::stoul(argv[4]);
    }
    if (argc > 5) {
        concurrent_queries = (std::string(argv[5]) == "queries");
    }
    if (argc > 6) {
        num_query_labels = std::stoul(argv[6]);
    }
    vespalib::SharedStringRepo::Handles resident;
    for (size_t i = 0; i < resident_labels; ++i) {
        resident.add("resident_" + std::to_string(i));
    }
    return RUN_ALL_TESTS();
}
