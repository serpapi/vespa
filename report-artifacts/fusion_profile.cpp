// Copyright Vespa.ai. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.

// Profiling aid for disk index fusion on real text.
//
//   fusion_profile build <index_dir> <docids_file> <wet.gz>...
//       Builds a disk index with one string index field "body" (word positions, and interleaved
//       features as with 'index: enable-bm25') from English Common Crawl WET records. Document i
//       (in file order) gets the i'th docid from docids_file (ascending, one per line); stops when
//       either runs out. Writes the docids actually used to <index_dir>/docids.txt.
//       FUSION_UNIQUE=1 keeps only the first occurrence of each word in a document (ablation),
//       FUSION_INTERLEAVED=0 disables interleaved features (also needed for fuse).
//   fusion_profile fuse <out_dir> <threads> <index_dir>...
//       Fuses the indexes like proton does (DIRECTIO read/write, FUSION_IO=normal for buffered IO);
//       a docid is owned by the last index listing it in docids.txt. Prints wall/cpu time and bytes.

#include <vespa/searchcommon/common/schema.h>
#include <vespa/searchlib/common/flush_token.h>
#include <vespa/searchlib/common/tunefileinfo.h>
#include <vespa/searchlib/diskindex/fusion.h>
#include <vespa/searchlib/diskindex/indexbuilder.h>
#include <vespa/searchlib/index/docidandfeatures.h>
#include <vespa/searchlib/index/dummyfileheadercontext.h>
#include <vespa/searchlib/test/index/mock_field_length_inspector.h>
#include <vespa/vespalib/util/threadstackexecutor.h>

#include <sys/resource.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

using search::diskindex::Fusion;
using search::diskindex::SelectorArray;
using search::index::DocIdAndPosOccFeatures;
using search::index::DummyFileHeaderContext;
using search::index::Schema;
using search::index::test::MockFieldLengthInspector;

namespace {

constexpr uint32_t max_occurrences = 10000; // vespa default max-occurrences per term and field
constexpr size_t   max_token_length = 1000; // vespa default max-token-length

bool env_flag(const char* name, bool default_value) {
    const char* value = getenv(name);
    return (value == nullptr) ? default_value : (std::string(value) == "1");
}

Schema make_schema() {
    Schema             schema;
    Schema::IndexField field("body", search::index::schema::DataType::STRING,
                             search::index::schema::CollectionType::SINGLE);
    field.set_interleaved_features(env_flag("FUSION_INTERLEAVED", true));
    schema.addIndexField(field);
    return schema;
}

// 'WARC-Type: conversion' records identified as English, from a gzipped WET file
class WetReader {
    FILE*  _pipe;
    char*  _line;
    size_t _cap;

    ssize_t read_line() { return getline(&_line, &_cap, _pipe); }
    bool starts_with(const char* prefix) const { return strncmp(_line, prefix, strlen(prefix)) == 0; }

public:
    explicit WetReader(const std::string& file)
        : _pipe(popen(("gzip -dc " + file).c_str(), "r")), _line(nullptr), _cap(0) {
        if (_pipe == nullptr) {
            perror("popen");
            exit(1);
        }
    }
    ~WetReader() {
        free(_line);
        pclose(_pipe);
    }
    bool next(std::string& text) {
        for (;;) {
            if (read_line() < 0) {
                return false;
            }
            if (!starts_with("WARC/1.0")) {
                continue;
            }
            bool    conversion = false;
            bool    english = false;
            size_t  length = 0;
            ssize_t len;
            while ((len = read_line()) > 0 && !(len <= 2 && (_line[0] == '\r' || _line[0] == '\n'))) {
                if (starts_with("WARC-Type: conversion")) {
                    conversion = true;
                } else if (starts_with("WARC-Identified-Content-Language: eng")) {
                    english = true;
                } else if (starts_with("Content-Length: ")) {
                    length = strtoul(_line + 16, nullptr, 10);
                }
            }
            text.resize(length);
            if (length > 0 && fread(text.data(), 1, length, _pipe) != length) {
                return false;
            }
            if (conversion && english && length > 0) {
                return true;
            }
        }
    }
};

bool is_word_char(uint8_t c) {
    return (c >= '0' && c <= '9') || ((c | 0x20) >= 'a' && (c | 0x20) <= 'z') || c >= 0x80;
}

class Inverter {
    std::unordered_map<std::string, uint32_t>  _word_ids;
    std::vector<std::vector<uint32_t>>         _postings;   // per word: docid, field_length, num_occs, positions...
    std::vector<std::pair<uint32_t, uint32_t>> _doc_tokens; // (word id, position)
    std::string                                _token;
    uint64_t                                   _tokens = 0;
    bool                                       _unique = env_flag("FUSION_UNIQUE", false); // only first occurrence

public:
    void add(uint32_t docid, const std::string& text) {
        _doc_tokens.clear();
        uint32_t pos = 0;
        size_t   i = 0;
        while (i < text.size()) {
            while (i < text.size() && !is_word_char(text[i])) {
                ++i;
            }
            _token.clear();
            while (i < text.size() && is_word_char(text[i])) {
                uint8_t c = text[i++];
                _token.push_back((c >= 'A' && c <= 'Z') ? char(c | 0x20) : char(c));
            }
            if (_token.empty() || _token.size() > max_token_length) {
                continue;
            }
            auto [it, inserted] = _word_ids.try_emplace(_token, uint32_t(_postings.size()));
            if (inserted) {
                _postings.emplace_back();
            }
            _doc_tokens.emplace_back(it->second, pos++);
        }
        _tokens += pos;
        std::sort(_doc_tokens.begin(), _doc_tokens.end());
        for (size_t b = 0; b < _doc_tokens.size();) {
            size_t e = b;
            while (e < _doc_tokens.size() && _doc_tokens[e].first == _doc_tokens[b].first) {
                ++e;
            }
            uint32_t num_occs = _unique ? 1u : std::min<size_t>(e - b, max_occurrences);
            auto&    data = _postings[_doc_tokens[b].first];
            data.push_back(docid);
            data.push_back(pos);
            data.push_back(num_occs);
            for (size_t j = b; j < b + num_occs; ++j) {
                data.push_back(_doc_tokens[j].second);
            }
            b = e;
        }
    }
    Inverter();
    ~Inverter();
    uint64_t tokens() const noexcept { return _tokens; }
    size_t words() const noexcept { return _word_ids.size(); }

    void write(const std::string& dir, uint32_t docid_limit) {
        std::vector<std::pair<std::string_view, uint32_t>> sorted;
        sorted.reserve(_word_ids.size());
        for (const auto& [word, id] : _word_ids) {
            sorted.emplace_back(word, id);
        }
        std::sort(sorted.begin(), sorted.end());
        Schema                   schema = make_schema();
        MockFieldLengthInspector field_length_inspector;
        search::TuneFileIndexing tune;
        tune._write.setWantDirectIO();
        DummyFileHeaderContext          file_header_context;
        search::diskindex::IndexBuilder builder(schema, dir, docid_limit, sorted.size(), field_length_inspector, tune,
                                                file_header_context);
        auto                            field_builder = builder.startField(0);
        DocIdAndPosOccFeatures          features;
        for (const auto& [word, id] : sorted) {
            field_builder->startWord(word);
            const auto& data = _postings[id];
            for (size_t i = 0; i < data.size();) {
                uint32_t field_length = data[i + 1];
                uint32_t num_occs = data[i + 2];
                features.clear(data[i]);
                for (uint32_t j = 0; j < num_occs; ++j) {
                    features.addNextOcc(0, data[i + 3 + j], 1, field_length);
                }
                features.set_field_length(field_length);
                features.set_num_occs(num_occs);
                field_builder->add_document(features);
                i += 3 + num_occs;
            }
            field_builder->endWord();
        }
    }
};

Inverter::Inverter() = default;
Inverter::~Inverter() = default;

double cpu_seconds() {
    rusage usage;
    getrusage(RUSAGE_SELF, &usage);
    return usage.ru_utime.tv_sec + usage.ru_utime.tv_usec * 1e-6 + usage.ru_stime.tv_sec +
           usage.ru_stime.tv_usec * 1e-6;
}

std::vector<uint32_t> read_docids(const std::string& file) {
    std::vector<uint32_t> docids;
    std::ifstream         in(file);
    uint32_t              docid;
    while (in >> docid) {
        docids.push_back(docid);
    }
    return docids;
}

uint64_t file_bytes(const std::string& index_dir, const char* name) {
    uint64_t bytes = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(index_dir)) {
        if (entry.is_regular_file() && (name == nullptr || entry.path().filename() == name)) {
            bytes += entry.file_size();
        }
    }
    return bytes;
}

int build(int argc, char** argv) {
    std::string           dir = argv[2];
    std::vector<uint32_t> docids = read_docids(argv[3]);
    auto                  start = std::chrono::steady_clock::now();
    Inverter              inverter;
    std::vector<uint32_t> used;
    std::string           text;
    uint64_t              text_bytes = 0;
    for (int i = 4; i < argc && used.size() < docids.size(); ++i) {
        WetReader reader(argv[i]);
        while (used.size() < docids.size() && reader.next(text)) {
            inverter.add(docids[used.size()], text);
            text_bytes += text.size();
            used.push_back(docids[used.size()]);
        }
    }
    if (used.empty()) {
        fprintf(stderr, "no documents\n");
        return 1;
    }
    std::filesystem::create_directories(dir);
    inverter.write(dir, used.back() + 1);
    std::ofstream out(dir + "/docids.txt");
    for (uint32_t docid : used) {
        out << docid << '\n';
    }
    out.close();
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    printf("build %s docs=%zu text_bytes=%lu tokens=%lu words=%zu posting_bytes=%lu index_bytes=%lu seconds=%.1f\n",
           dir.c_str(), used.size(), text_bytes, inverter.tokens(), inverter.words(),
           file_bytes(dir, "posocc.dat.compressed"), file_bytes(dir, nullptr), secs);
    return 0;
}

int fuse(int argc, char** argv) {
    std::string                        out_dir = argv[2];
    uint32_t                           threads = std::stoul(argv[3]);
    std::vector<std::string>           sources;
    std::vector<std::vector<uint32_t>> docids;
    uint32_t                           docid_limit = 0;
    uint64_t                           in_postings = 0;
    uint64_t                           in_bytes = 0;
    for (int i = 4; i < argc; ++i) {
        sources.emplace_back(argv[i]);
        docids.push_back(read_docids(sources.back() + "/docids.txt"));
        docid_limit = std::max(docid_limit, docids.back().back() + 1);
        in_postings += file_bytes(sources.back(), "posocc.dat.compressed");
        in_bytes += file_bytes(sources.back(), nullptr);
    }
    SelectorArray selector(docid_limit, 0);
    for (size_t i = 0; i < docids.size(); ++i) {
        for (uint32_t docid : docids[i]) {
            selector[docid] = i;
        }
    }
    Schema                   schema = make_schema();
    search::TuneFileIndexing tune;
    const char*              io = getenv("FUSION_IO");
    bool                     direct_io = (io == nullptr || std::string(io) != "normal");
    if (direct_io) {
        tune._read.setWantDirectIO();
        tune._write.setWantDirectIO();
    }
    DummyFileHeaderContext        file_header_context;
    vespalib::ThreadStackExecutor executor(threads);
    std::filesystem::remove_all(out_dir);
    auto   start = std::chrono::steady_clock::now();
    double cpu_start = cpu_seconds();
    {
        Fusion fusion(schema, out_dir, sources, selector, tune, file_header_context);
        if (!fusion.merge(executor, std::make_shared<search::FlushToken>())) {
            fprintf(stderr, "fusion failed\n");
            return 1;
        }
    }
    double        secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    double        cpu = cpu_seconds() - cpu_start;
    std::ofstream out(out_dir + "/docids.txt");
    for (uint32_t docid = 1; docid < docid_limit; ++docid) {
        const auto& owner = docids[selector[docid]];
        if (std::binary_search(owner.begin(), owner.end(), docid)) {
            out << docid << '\n';
        }
    }
    out.close();
    uint64_t out_postings = file_bytes(out_dir, "posocc.dat.compressed");
    uint64_t out_bytes = file_bytes(out_dir, nullptr);
    printf("fuse %s io=%s sources=%zu docid_limit=%u in_posting_bytes=%lu in_bytes=%lu out_posting_bytes=%lu "
           "out_bytes=%lu wall_s=%.2f cpu_s=%.2f in_MBps_wall=%.1f in_MBps_cpu=%.1f out_MBps_wall=%.1f\n",
           out_dir.c_str(), direct_io ? "direct" : "normal", sources.size(), docid_limit, in_postings, in_bytes,
           out_postings, out_bytes, secs, cpu, in_bytes / secs / 1e6, in_bytes / cpu / 1e6, out_bytes / secs / 1e6);
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc >= 5 && std::string(argv[1]) == "build") {
        return build(argc, argv);
    }
    if (argc >= 5 && std::string(argv[1]) == "fuse") {
        return fuse(argc, argv);
    }
    fprintf(stderr,
            "usage: %s build <index_dir> <docids_file> <wet.gz>... | fuse <out_dir> <threads> <index_dir>...\n",
            argv[0]);
    return 1;
}
