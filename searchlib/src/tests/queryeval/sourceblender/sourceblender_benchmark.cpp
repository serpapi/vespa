// Copyright Vespa.ai. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.

// Strict source blender benchmark: one large child plus k small children,
// with a source selector where a fraction of the large child's postings
// belong to newer sources (stale postings the blender has to skip).
//
// usage: searchlib_sourceblender_benchmark_app [--children=zc,array] [--k=1,3,5,10,15]
//        [--stale=0,0.02,0.1] [--unpack=0,1] [--impl=legacy,current] [--docs=50000000]
//        [--large=5000000] [--small=5000] [--reps=7] [--seed=1] [--dir=sourceblender_benchmark_index]
//
// With PERF_CTL_FD/PERF_ACK_FD (perf stat -D -1 --control fd:ctl,ack) or
// PERF_CTL_FIFO/PERF_ACK_FIFO (--control fifo:ctl,ack) set, only the timed
// loops are counted.

#include "legacy_strict_source_blender.h"

#include <vespa/searchcommon/common/schema.h>
#include <vespa/searchlib/attribute/fixedsourceselector.h>
#include <vespa/searchlib/common/tunefileinfo.h>
#include <vespa/searchlib/diskindex/diskindex.h>
#include <vespa/searchlib/diskindex/indexbuilder.h>
#include <vespa/searchlib/fef/termfieldmatchdata.h>
#include <vespa/searchlib/fef/termfieldmatchdataarray.h>
#include <vespa/searchlib/index/docidandfeatures.h>
#include <vespa/searchlib/index/dummyfileheadercontext.h>
#include <vespa/searchlib/index/postinglisthandle.h>
#include <vespa/searchlib/queryeval/sourceblendersearch.h>
#include <vespa/searchlib/test/index/mock_field_length_inspector.h>

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

using search::FixedSourceSelector;
using search::TuneFileIndexing;
using search::TuneFileSearch;
using search::diskindex::DiskIndex;
using search::fef::TermFieldMatchData;
using search::fef::TermFieldMatchDataArray;
using search::index::DocIdAndPosOccFeatures;
using search::index::DummyFileHeaderContext;
using search::index::PostingListHandle;
using search::index::Schema;
using search::index::test::MockFieldLengthInspector;
using search::queryeval::SearchIterator;
using search::queryeval::SourceBlenderSearch;
using search::queryeval::test::LegacyStrictSourceBlender;

namespace {

struct Options {
    std::vector<std::string> children{"zc"};
    std::vector<uint32_t>    k{1, 3, 5, 10, 15};
    std::vector<double>      stale{0.0, 0.02, 0.10};
    std::vector<uint32_t>    unpack{0, 1};
    std::vector<std::string> impl{"legacy", "current"};
    uint32_t                 docs = 50'000'000;
    uint32_t                 large = 5'000'000;
    uint32_t                 small = 5'000;
    uint32_t                 reps = 7;
    uint64_t                 seed = 1;
    std::string              dir = "sourceblender_benchmark_index";
    ~Options();
};

Options::~Options() = default;

std::vector<std::string> split(const std::string& value) {
    std::vector<std::string> result;
    size_t                   pos = 0;
    while (pos <= value.size()) {
        size_t next = value.find(',', pos);
        if (next == std::string::npos) {
            next = value.size();
        }
        result.push_back(value.substr(pos, next - pos));
        pos = next + 1;
    }
    return result;
}

Options parse_options(int argc, char** argv) {
    Options opts;
    for (int i = 1; i < argc; ++i) {
        std::string arg(argv[i]);
        auto        eq = arg.find('=');
        if (arg.rfind("--", 0) != 0 || eq == std::string::npos) {
            fprintf(stderr, "bad argument: %s\n", arg.c_str());
            exit(1);
        }
        std::string name = arg.substr(2, eq - 2);
        std::string value = arg.substr(eq + 1);
        if (name == "children") {
            opts.children = split(value);
        } else if (name == "k") {
            opts.k.clear();
            for (const auto& v : split(value)) {
                opts.k.push_back(std::stoul(v));
            }
        } else if (name == "stale") {
            opts.stale.clear();
            for (const auto& v : split(value)) {
                opts.stale.push_back(std::stod(v));
            }
        } else if (name == "unpack") {
            opts.unpack.clear();
            for (const auto& v : split(value)) {
                opts.unpack.push_back(std::stoul(v));
            }
        } else if (name == "impl") {
            opts.impl = split(value);
        } else if (name == "docs") {
            opts.docs = std::stoul(value);
        } else if (name == "large") {
            opts.large = std::stoul(value);
        } else if (name == "small") {
            opts.small = std::stoul(value);
        } else if (name == "reps") {
            opts.reps = std::stoul(value);
        } else if (name == "seed") {
            opts.seed = std::stoull(value);
        } else if (name == "dir") {
            opts.dir = value;
        } else {
            fprintf(stderr, "unknown option: %s\n", name.c_str());
            exit(1);
        }
    }
    return opts;
}

// Lets "perf stat -D -1 --control fd:ctl,ack" (PERF_CTL_FD/PERF_ACK_FD) or
// "--control fifo:ctl,ack" (PERF_CTL_FIFO/PERF_ACK_FIFO) count only the timed loops.
class PerfControl {
    int _ctl = -1;
    int _ack = -1;

    void send(const char* cmd) {
        if (_ctl < 0) {
            return;
        }
        ssize_t len = strlen(cmd);
        if (write(_ctl, cmd, len) != len) {
            perror("perf control write");
            exit(1);
        }
        char buf[16];
        if (read(_ack, buf, sizeof(buf)) <= 0) {
            perror("perf control ack");
            exit(1);
        }
    }

public:
    PerfControl() {
        const char* ctl = getenv("PERF_CTL_FD");
        const char* ack = getenv("PERF_ACK_FD");
        const char* ctl_fifo = getenv("PERF_CTL_FIFO");
        const char* ack_fifo = getenv("PERF_ACK_FIFO");
        if (ctl != nullptr && ack != nullptr) {
            _ctl = atoi(ctl);
            _ack = atoi(ack);
        } else if (ctl_fifo != nullptr && ack_fifo != nullptr) {
            _ctl = open(ctl_fifo, O_WRONLY);
            _ack = open(ack_fifo, O_RDONLY);
            if (_ctl < 0 || _ack < 0) {
                perror("perf control fifo open");
                exit(1);
            }
        }
    }
    void enable() { send("enable\n"); }
    void disable() { send("disable\n"); }
};

struct Posting {
    uint32_t docid;
    uint32_t field_length;
    uint32_t num_occs;
};

// Positions are derived from (docid, source) so that unpacking the wrong
// child for a docid shows up in the checksum.
void make_features(const Posting& p, uint32_t source, DocIdAndPosOccFeatures& features) {
    features.clear(p.docid);
    uint64_t h = (uint64_t(p.docid) << 8 | source) * 0x9e3779b97f4a7c15ull;
    uint32_t step = p.field_length / p.num_occs;
    for (uint32_t i = 0; i < p.num_occs; ++i) {
        h = h * 6364136223846793005ull + 1442695040888963407ull;
        features.addNextOcc(0, i * step + (h >> 33) % step, 1, p.field_length);
    }
    features.set_field_length(p.field_length);
    features.set_num_occs(p.num_occs);
}

Posting make_posting(uint32_t docid, std::mt19937_64& rng) {
    std::geometric_distribution<uint32_t>   occs(0.45);
    uint32_t                                num_occs = std::min(1 + occs(rng), 32u);
    std::uniform_int_distribution<uint32_t> len(std::max(100u, num_occs * 8), 3000u);
    return {docid, len(rng), num_occs};
}

class ArrayChild : public SearchIterator {
private:
    const std::vector<Posting>& _postings;
    TermFieldMatchData&         _tfmd;
    uint32_t                    _pos;

public:
    ArrayChild(const std::vector<Posting>& postings, TermFieldMatchData& tfmd)
        : _postings(postings), _tfmd(tfmd), _pos(0) {}
    void initRange(uint32_t begin_id, uint32_t end_id) override {
        SearchIterator::initRange(begin_id, end_id);
        _pos = 0;
    }
    void doSeek(uint32_t docid) override {
        while (_pos < _postings.size() && _postings[_pos].docid < docid) {
            ++_pos;
        }
        setDocId(_pos < _postings.size() ? _postings[_pos].docid : search::endDocId);
    }
    void doUnpack(uint32_t docid) override {
        _tfmd.reset(docid);
        _tfmd.setFieldLength(_postings[_pos].field_length);
        _tfmd.setNumOccs(_postings[_pos].num_occs);
    }
    Trinary is_strict() const override { return Trinary::True; }
};

class Source {
private:
    std::vector<Posting>                  _postings;
    std::unique_ptr<DiskIndex>            _index;
    search::index::DictionaryLookupResult _lookup;
    PostingListHandle                     _handle;

public:
    explicit Source(std::vector<Posting> postings);
    ~Source();
    const std::vector<Posting>& postings() const noexcept { return _postings; }

    void build_disk_index(const std::string& dir, uint32_t source_id, uint32_t docid_limit) {
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
        Schema             schema;
        Schema::IndexField field("body", search::index::schema::DataType::STRING,
                                 search::index::schema::CollectionType::SINGLE);
        field.set_interleaved_features(true);
        schema.addIndexField(field);
        MockFieldLengthInspector field_length_inspector;
        TuneFileIndexing         tune_file_indexing;
        DummyFileHeaderContext   file_header_context;
        {
            search::diskindex::IndexBuilder builder(schema, dir, docid_limit, 1, field_length_inspector,
                                                    tune_file_indexing, file_header_context);
            auto                            field_builder = builder.startField(0);
            DocIdAndPosOccFeatures          features;
            field_builder->startWord("apple");
            for (const auto& p : _postings) {
                make_features(p, source_id, features);
                field_builder->add_document(features);
            }
            field_builder->endWord();
        }
        TuneFileSearch tune_file_search;
        tune_file_search._read.setWantMemoryMap();
        _index = std::make_unique<DiskIndex>(dir, std::shared_ptr<search::diskindex::IPostingListCache>());
        bool ok = _index->setup(tune_file_search);
        assert(ok);
        const auto& field_index = _index->get_field_index(0);
        _lookup = field_index.lookup("apple");
        assert(_lookup.valid() && _lookup.counts._numDocs == _postings.size());
        _handle = field_index.read_posting_list(_lookup);
    }

    std::unique_ptr<SearchIterator> create(bool zc, TermFieldMatchData& tfmd) const {
        if (zc) {
            TermFieldMatchDataArray tfmda;
            tfmda.add(&tfmd);
            return _index->get_field_index(0).create_iterator(_lookup, _handle, tfmda);
        }
        return std::make_unique<ArrayChild>(_postings, tfmd);
    }
};

Source::Source(std::vector<Posting> postings) : _postings(std::move(postings)), _index(), _lookup(), _handle() {
}

Source::~Source() = default;

struct Scenario {
    std::unique_ptr<FixedSourceSelector> selector;
    std::vector<const Source*>           sources; // index == source id
    std::vector<std::unique_ptr<Source>> small;
    uint64_t                             expected_hits = 0;
    Scenario();
    ~Scenario();
};

Scenario::Scenario() = default;
Scenario::~Scenario() = default;

std::vector<Posting> make_large(const Options& opts) {
    std::mt19937_64 rng(opts.seed);
    std::bernoulli_distribution pick(double(opts.large) / double(opts.docs - 1));
    std::vector<Posting> postings;
    postings.reserve(opts.large + opts.large / 50);
    for (uint32_t docid = 1; docid < opts.docs; ++docid) {
        if (pick(rng)) {
            postings.push_back(make_posting(docid, rng));
        }
    }
    return postings;
}

// Source 0 is the large (fused) index, sources 1..k are newer (flushed and memory) indexes.
// A fraction 'stale' of the large child's documents were re-fed into a newer source. Up to
// half of each small child are such re-fed documents that still contain the term (equal
// docids across children), the rest are new documents.
void make_scenario(Scenario& s, const Options& opts, const Source& large, uint32_t k, double stale,
                   const std::string& dir, bool zc) {
    std::mt19937_64 rng(opts.seed * 1000003 + k * 101 + uint64_t(stale * 1e6));
    s.selector = std::make_unique<FixedSourceSelector>(0, dir + "/selector", opts.docs);
    std::vector<uint8_t> owner(opts.docs, 0);
    std::vector<bool>    used(opts.docs, false);
    for (const auto& p : large.postings()) {
        used[p.docid] = true;
    }
    std::vector<std::vector<uint32_t>>      stale_docids(k + 1);
    std::bernoulli_distribution             is_stale(stale);
    std::uniform_int_distribution<uint32_t> newer(1, k);
    for (const auto& p : large.postings()) {
        if (is_stale(rng)) {
            uint32_t src = newer(rng);
            owner[p.docid] = src;
            stale_docids[src].push_back(p.docid);
        }
    }
    std::uniform_int_distribution<uint32_t> any_docid(1, opts.docs - 1);
    std::vector<std::vector<uint32_t>>      small_docids(k + 1);
    for (uint32_t src = 1; src <= k; ++src) {
        auto& docids = small_docids[src];
        std::shuffle(stale_docids[src].begin(), stale_docids[src].end(), rng);
        for (uint32_t docid : stale_docids[src]) {
            if (docids.size() >= opts.small / 2 || (docid & 1)) {
                continue;
            }
            docids.push_back(docid);
        }
        while (docids.size() < opts.small) {
            uint32_t docid = any_docid(rng);
            if (!used[docid]) {
                used[docid] = true;
                owner[docid] = src;
                docids.push_back(docid);
            }
        }
        std::sort(docids.begin(), docids.end());
    }
    for (uint32_t docid = 1; docid < opts.docs; ++docid) {
        if (owner[docid] != 0) {
            s.selector->setSource(docid, owner[docid]);
        }
    }
    s.selector->setSource(opts.docs - 1, owner[opts.docs - 1]);
    s.sources.push_back(&large);
    for (uint32_t src = 1; src <= k; ++src) {
        std::vector<Posting> postings;
        for (uint32_t docid : small_docids[src]) {
            postings.push_back(make_posting(docid, rng));
        }
        s.expected_hits += postings.size();
        s.small.push_back(std::make_unique<Source>(std::move(postings)));
        if (zc) {
            s.small.back()->build_disk_index(dir + "/small" + std::to_string(src), src, opts.docs);
        }
        s.sources.push_back(s.small.back().get());
    }
    for (const auto& p : large.postings()) {
        if (owner[p.docid] == 0) {
            ++s.expected_hits;
        }
    }
}

std::unique_ptr<SearchIterator> make_blender(const Scenario& s, const std::string& impl, bool zc,
                                             TermFieldMatchData& tfmd) {
    SourceBlenderSearch::Children children;
    for (uint32_t src = 0; src < s.sources.size(); ++src) {
        children.emplace_back(s.sources[src]->create(zc, tfmd).release(), src);
    }
    if (impl == "legacy") {
        return LegacyStrictSourceBlender::create(s.selector->createIterator(), children);
    }
    return SourceBlenderSearch::create(s.selector->createIterator(), children, true);
}

struct RunResult {
    uint64_t hits = 0;
    uint64_t checksum = 0;
    double   seconds = 0.0;
};

RunResult run_once(SearchIterator& search, uint32_t docid_limit, bool unpack, const TermFieldMatchData& tfmd,
                   PerfControl* perf) {
    RunResult result;
    uint64_t  h = 0xcbf29ce484222325ull;
    if (perf != nullptr) {
        perf->enable();
    }
    auto before = std::chrono::steady_clock::now();
    search.initRange(1, docid_limit);
    uint32_t docid = search.seekFirst(1);
    while (docid < docid_limit) {
        if (unpack) {
            search.unpack(docid);
            uint32_t first = tfmd.size() > 0 ? tfmd.begin()->getPosition() : 0;
            h = (h ^ (uint64_t(tfmd.getFieldLength()) << 32 | uint64_t(tfmd.getNumOccs()) << 16 | first)) *
                0x100000001b3ull;
        }
        h = (h ^ docid) * 0x100000001b3ull;
        ++result.hits;
        docid = search.seekNext(docid + 1);
    }
    auto after = std::chrono::steady_clock::now();
    if (perf != nullptr) {
        perf->disable();
    }
    result.checksum = h;
    result.seconds = std::chrono::duration<double>(after - before).count();
    return result;
}

double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

} // namespace

int main(int argc, char** argv) {
    Options     opts = parse_options(argc, argv);
    PerfControl perf;
    std::filesystem::create_directories(opts.dir);
    Source large(make_large(opts));
    bool   need_zc = std::find(opts.children.begin(), opts.children.end(), "zc") != opts.children.end();
    if (need_zc) {
        large.build_disk_index(opts.dir + "/large", 0, opts.docs);
    }
    printf("# docs=%u large_postings=%zu small_postings=%u reps=%u seed=%" PRIu64 "\n", opts.docs,
           large.postings().size(), opts.small, opts.reps, opts.seed);
    printf("CSV,children,k,stale,unpack,impl,hits,reps,min_ns_per_hit,median_ns_per_hit\n");
    fflush(stdout);
    int failures = 0;
    for (const auto& kind : opts.children) {
        bool zc = (kind == "zc");
        for (uint32_t k : opts.k) {
            for (double stale : opts.stale) {
                Scenario s;
                make_scenario(s, opts, large, k, stale, opts.dir, zc);
                for (uint32_t unpack : opts.unpack) {
                    TermFieldMatchData tfmd;
                    // uncounted verification run per implementation
                    std::vector<RunResult> verified;
                    for (const auto& impl : opts.impl) {
                        auto      search = make_blender(s, impl, zc, tfmd);
                        RunResult r = run_once(*search, opts.docs, unpack != 0, tfmd, nullptr);
                        bool      ok =
                            (r.hits == s.expected_hits) &&
                            (verified.empty() || (r.hits == verified[0].hits && r.checksum == verified[0].checksum));
                        if (!ok) {
                            ++failures;
                            printf("MISMATCH children=%s k=%u stale=%g unpack=%u impl=%s hits=%" PRIu64
                                   " expected=%" PRIu64 " checksum=%016" PRIx64 "\n",
                                   kind.c_str(), k, stale, unpack, impl.c_str(), r.hits, s.expected_hits, r.checksum);
                        }
                        verified.push_back(r);
                    }
                    std::vector<std::vector<double>> ns_per_hit(opts.impl.size());
                    for (uint32_t rep = 0; rep < opts.reps; ++rep) {
                        for (size_t i = 0; i < opts.impl.size(); ++i) {
                            size_t    idx = (i + rep) % opts.impl.size();
                            auto      search = make_blender(s, opts.impl[idx], zc, tfmd);
                            RunResult r = run_once(*search, opts.docs, unpack != 0, tfmd, &perf);
                            ns_per_hit[idx].push_back(r.seconds * 1e9 / double(r.hits));
                        }
                    }
                    for (size_t idx = 0; idx < opts.impl.size(); ++idx) {
                        const auto& v = ns_per_hit[idx];
                        printf("CSV,%s,%u,%g,%u,%s,%" PRIu64 ",%u,%.3f,%.3f\n", kind.c_str(), k, stale, unpack,
                               opts.impl[idx].c_str(), verified[idx].hits, opts.reps,
                               *std::min_element(v.begin(), v.end()), median(v));
                    }
                    fflush(stdout);
                }
            }
        }
    }
    std::filesystem::remove_all(opts.dir);
    if (failures != 0) {
        printf("FAILED: %d mismatches\n", failures);
        return 1;
    }
    return 0;
}
