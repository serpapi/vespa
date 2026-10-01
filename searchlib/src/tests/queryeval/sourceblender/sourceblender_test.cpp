// Copyright Vespa.ai. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.

#include "legacy_strict_source_blender.h"

#include <vespa/searchlib/attribute/fixedsourceselector.h>
#include <vespa/searchlib/fef/matchdata.h>
#include <vespa/searchlib/queryeval/intermediate_blueprints.h>
#include <vespa/searchlib/queryeval/leaf_blueprints.h>
#include <vespa/searchlib/queryeval/simpleresult.h>
#include <vespa/searchlib/queryeval/simplesearch.h>
#include <vespa/searchlib/queryeval/sourceblendersearch.h>
#include <vespa/searchlib/test/docid_iterator.h>
#include <vespa/searchlib/test/searchiteratorverifier.h>
#include <vespa/vespalib/gtest/gtest.h>

#include <algorithm>
#include <array>
#include <numeric>
#include <random>

using namespace search::queryeval;
using namespace search;
using search::fef::MatchData;
using search::queryeval::test::LegacyStrictSourceBlender;
using std::make_unique;

/**
 * Proxy search used to verify unpack pattern
 **/
class UnpackChecker : public SearchIterator {
private:
    SearchIterator::UP _search;
    SimpleResult       _unpacked;

protected:
    void doSeek(uint32_t docid) override {
        _search->seek(docid);
        setDocId(_search->getDocId());
    }
    void doUnpack(uint32_t docid) override {
        _unpacked.addHit(docid);
        _search->unpack(docid);
    }

public:
    explicit UnpackChecker(SearchIterator* search) : _search(search), _unpacked() {}
    ~UnpackChecker() override;
    const SimpleResult& getUnpacked() const { return _unpacked; }
};

UnpackChecker::~UnpackChecker() = default;

class MySelector : public search::FixedSourceSelector {
public:
    explicit MySelector(int defaultSource) : search::FixedSourceSelector(defaultSource, "fs") {}
    MySelector& set(Source s, uint32_t docId) {
        setSource(s, docId);
        return *this;
    }
};

//-----------------------------------------------------------------------------

TEST(SourceBlenderTest, test_strictness) {
    MatchData::UP md(MatchData::makeTestInstance(100, 10));
    for (uint32_t i = 0; i < 2; ++i) {
        bool strict = (i == 0);

        SimpleResult a;
        SimpleResult b;

        a.addHit(2).addHit(5).addHit(6).addHit(8);
        b.addHit(3).addHit(5).addHit(6).addHit(7);

        auto* sel = new MySelector(5);
        sel->set(2, 1).set(3, 2).set(5, 2).set(7, 1);

        auto* blend_b = new SourceBlenderBlueprint(*sel);
        auto  a_b = std::make_unique<SimpleBlueprint>(a);
        auto  b_b = std::make_unique<SimpleBlueprint>(b);
        a_b->setSourceId(1);
        b_b->setSourceId(2);
        blend_b->addChild(std::move(a_b));
        blend_b->addChild(std::move(b_b));
        Blueprint::UP bp(blend_b);
        bp->basic_plan(strict, 100);
        bp->fetchPostings(ExecuteInfo::FULL);
        SearchIterator::UP search = bp->createSearch(*md);
        search->initFullRange();
        SearchIterator& blend = *search;

        EXPECT_TRUE(!blend.seek(1u));
        if (strict) {
            EXPECT_EQ(2u, blend.getDocId());
        } else {
            EXPECT_EQ(blend.beginId(), blend.getDocId());
        }
        EXPECT_TRUE(blend.seek(5));
        EXPECT_EQ(5u, blend.getDocId());
        EXPECT_TRUE(!blend.seek(6));
        if (strict) {
            EXPECT_TRUE(blend.isAtEnd());
        } else {
            EXPECT_EQ(5u, blend.getDocId());
        }
        delete sel;
    }
}

TEST(SourceBlenderTest, test_full_sourceblender_search) {
    SimpleResult a;
    SimpleResult b;
    SimpleResult c;

    a.addHit(2).addHit(11).addHit(21).addHit(34);
    b.addHit(3).addHit(11).addHit(21).addHit(33);
    c.addHit(4).addHit(11).addHit(21).addHit(32);

    // these are all handed over to the blender
    auto* ua = new UnpackChecker(new SimpleSearch(a));
    auto* ub = new UnpackChecker(new SimpleSearch(b));
    auto* uc = new UnpackChecker(new SimpleSearch(c));
    auto  sel = make_unique<MySelector>(5);

    sel->set(2, 1).set(3, 2).set(11, 2).set(21, 3).set(34, 1);
    SourceBlenderSearch::Children abc;
    abc.emplace_back(ua, 1);
    abc.emplace_back(ub, 2);
    abc.emplace_back(uc, 3);

    SearchIterator::UP blend(SourceBlenderSearch::create(sel->createIterator(), abc, true));
    SimpleResult       result;
    result.search(*blend, 100);

    SimpleResult expect_result;
    expect_result.addHit(2).addHit(3).addHit(11).addHit(21).addHit(34);

    SimpleResult expect_unpacked_a;
    expect_unpacked_a.addHit(2).addHit(34);

    SimpleResult expect_unpacked_b;
    expect_unpacked_b.addHit(3).addHit(11);

    SimpleResult expect_unpacked_c;
    expect_unpacked_c.addHit(21);

    EXPECT_EQ(expect_result, result);
    EXPECT_EQ(expect_unpacked_a, ua->getUnpacked());
    EXPECT_EQ(expect_unpacked_b, ub->getUnpacked());
    EXPECT_EQ(expect_unpacked_c, uc->getUnpacked());
}

using search::test::SearchIteratorVerifier;

class Verifier : public SearchIteratorVerifier {
public:
    Verifier();
    ~Verifier() override;
    SearchIterator::UP create(bool strict) const override {
        return SearchIterator::UP(
            SourceBlenderSearch::create(_selector.createIterator(), createChildren(strict), strict));
    }

private:
    SourceBlenderSearch::Children createChildren(bool strict) const {
        SourceBlenderSearch::Children children;
        for (size_t index(0); index < _indexes.size(); index++) {
            children.emplace_back(createIterator(_indexes[index], strict).release(), index);
        }
        return children;
    }
    std::vector<DocIds> _indexes;
    MySelector          _selector;
};

Verifier::Verifier() : _indexes(3), _selector(getDocIdLimit()) {
    for (uint32_t docId : getExpectedDocIds()) {
        const size_t indexId = docId % _indexes.size();
        _selector.set(docId, indexId);
        _indexes[indexId].push_back(docId);
    }
}
Verifier::~Verifier() = default;

TEST(SourceBlenderTest, test_that_source_blender_iterator_adheres_to_search_terator_requirements) {
    Verifier searchIteratorVerifier;
    searchIteratorVerifier.verify();
}

namespace {

template <typename Base> class RecordUnpack : public Base {
private:
    std::vector<uint32_t>& _unpacked;

public:
    template <typename... Args>
    explicit RecordUnpack(std::vector<uint32_t>& unpacked, Args&&... args)
        : Base(std::forward<Args>(args)...), _unpacked(unpacked) {}
    void doUnpack(uint32_t docid) override {
        _unpacked.push_back(docid);
        Base::doUnpack(docid);
    }
};

struct ChildSpec {
    uint32_t              source;
    std::vector<uint32_t> docids;
    // SimpleSearch reports hits beyond the end id, DocIdIterator does not
    bool simple;
};

struct BlendSetup {
    std::vector<ChildSpec>      children;
    std::unique_ptr<MySelector> selector;
    std::vector<uint32_t>       owner; // source per docid below the selector docid limit

    BlendSetup(std::vector<ChildSpec> children_in, std::vector<uint32_t> owner_in);
    ~BlendSetup();
    uint32_t docid_limit() const { return owner.size(); }
    // child index holding docid as a hit, or -1
    int hit_child(uint32_t docid) const {
        if (docid >= docid_limit()) {
            return -1;
        }
        for (size_t i = 0; i < children.size(); ++i) {
            const auto& c = children[i];
            if (c.source == owner[docid] && std::binary_search(c.docids.begin(), c.docids.end(), docid)) {
                return i;
            }
        }
        return -1;
    }
    // docid a strict blender over [begin, end_id) is positioned at after seeking to target
    uint32_t expected_seek(uint32_t target, uint32_t end_id) const {
        if (target >= docid_limit()) {
            return endDocId;
        }
        int child = hit_child(target);
        if (child >= 0 && (target < end_id || children[child].simple)) {
            return target;
        }
        for (uint32_t docid = target + 1; docid < std::min(end_id, docid_limit()); ++docid) {
            if (hit_child(docid) >= 0) {
                return docid;
            }
        }
        return endDocId;
    }
};

BlendSetup::BlendSetup(std::vector<ChildSpec> children_in, std::vector<uint32_t> owner_in)
    : children(std::move(children_in)), selector(std::make_unique<MySelector>(0)), owner(std::move(owner_in)) {
    for (uint32_t docid = 0; docid < owner.size(); ++docid) {
        selector->setSource(docid, owner[docid]);
    }
    // the selector also covers the docid after the last one set, with the default source (0)
    owner.resize(selector->createIterator()->getDocIdLimit(), 0);
}

BlendSetup::~BlendSetup() = default;

using UnpackLog = std::vector<std::vector<uint32_t>>;

SearchIterator::UP make_strict_blender(const BlendSetup& setup, bool legacy, UnpackLog& unpacked) {
    unpacked.assign(setup.children.size(), {});
    SourceBlenderSearch::Children children;
    for (size_t i = 0; i < setup.children.size(); ++i) {
        const auto&     spec = setup.children[i];
        SearchIterator* child = nullptr;
        if (spec.simple) {
            child = new RecordUnpack<SimpleSearch>(unpacked[i], SimpleResult(spec.docids));
        } else {
            child = new RecordUnpack<search::test::DocIdIterator>(unpacked[i], spec.docids, true);
        }
        children.emplace_back(child, spec.source);
    }
    if (legacy) {
        return LegacyStrictSourceBlender::create(setup.selector->createIterator(), children);
    }
    return SourceBlenderSearch::create(setup.selector->createIterator(), children, true);
}

struct Blended {
    std::vector<uint32_t> hits;
    UnpackLog             unpacked;
    Blended();
    ~Blended();
};

Blended::Blended() = default;
Blended::~Blended() = default;

Blended blend(const BlendSetup& setup, bool legacy, uint32_t begin_id, uint32_t end_id) {
    Blended result;
    auto    search = make_strict_blender(setup, legacy, result.unpacked);
    search->initRange(begin_id, end_id);
    for (uint32_t docid = search->seekFirst(begin_id); docid < end_id; docid = search->seekNext(docid + 1)) {
        result.hits.push_back(docid);
        search->unpack(docid);
    }
    return result;
}

void expect_blend(const BlendSetup& setup, const std::vector<uint32_t>& hits, const UnpackLog& unpacked,
                  uint32_t begin_id = 1, uint32_t end_id = endDocId) {
    for (bool legacy : {true, false}) {
        SCOPED_TRACE(legacy ? "legacy" : "current");
        auto result = blend(setup, legacy, begin_id, end_id);
        EXPECT_EQ(hits, result.hits);
        EXPECT_EQ(unpacked, result.unpacked);
    }
}

} // namespace

TEST(SourceBlenderTest, strict_blender_handles_exhausted_children) {
    // source:     0  1  2  3  4  5  6  7  8  9 10 11 12
    BlendSetup setup({{1, {2, 4}, false}, {2, {3, 9, 11}, true}, {3, {}, false}},
                     {0, 0, 1, 2, 1, 0, 0, 0, 0, 2, 0, 2, 0});
    expect_blend(setup, {2, 3, 4, 9, 11}, {{2, 4}, {3, 9, 11}, {}});
}

TEST(SourceBlenderTest, strict_blender_handles_equal_docids_across_children) {
    // source:     0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21
    BlendSetup setup({{1, {5, 10, 15, 20}, false}, {2, {5, 10, 20}, true}, {3, {10, 15, 20}, false}},
                     {0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 3, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0});
    expect_blend(setup, {5, 10, 15, 20}, {{15, 20}, {5}, {10}});
}

TEST(SourceBlenderTest, strict_blender_skips_docids_owned_by_another_or_missing_source) {
    // docid 3 belongs to source 2 which does not have it, docids 6 and 7 to source 9 which has no child
    // source:     0  1  2  3  4  5  6  7  8
    BlendSetup setup({{1, {2, 3, 6, 7, 8}, false}, {2, {4, 7}, true}}, {0, 0, 1, 2, 2, 0, 9, 9, 1});
    expect_blend(setup, {2, 4, 8}, {{2, 8}, {4}});
}

TEST(SourceBlenderTest, strict_blender_respects_docid_limit_and_end_id) {
    // source:     0  1  2  3  4  5 (6 gets the default source 0)
    BlendSetup setup({{1, {2, 4, 6, 7}, true}, {2, {3, 5, 7}, false}, {0, {6, 7, 8}, false}}, {0, 0, 1, 2, 1, 2});
    ASSERT_EQ(7u, setup.docid_limit());
    expect_blend(setup, {2, 3, 4, 5, 6}, {{2, 4}, {3, 5}, {6}});
    expect_blend(setup, {3}, {{}, {3}, {}}, 3, 4);
    for (bool legacy : {true, false}) {
        SCOPED_TRACE(legacy ? "legacy" : "current");
        UnpackLog unpacked;
        auto      search = make_strict_blender(setup, legacy, unpacked);
        search->initRange(1, 4);
        EXPECT_EQ(2u, search->seekFirst(1));
        // seeking exactly to a hit beyond the end id reports it (SimpleSearch child), as the non-strict blender does
        EXPECT_TRUE(search->seek(4));
        EXPECT_TRUE(search->isAtEnd());
        // the DocIdIterator child does not report hits beyond the end id
        EXPECT_FALSE(search->seek(5));
        EXPECT_EQ(endDocId, search->getDocId());
        search->initRange(1, endDocId);
        EXPECT_EQ(5u, search->seekFirst(5));
        EXPECT_TRUE(search->seek(6));
        // every child has docid 7, but it is beyond the selector docid limit
        EXPECT_FALSE(search->seek(7));
        EXPECT_TRUE(search->isAtEnd());
    }
}

TEST(SourceBlenderTest, strict_blender_matches_legacy_implementation_and_model_on_random_input) {
    std::mt19937 rnd(42);
    auto uniform = [&rnd](uint32_t lo, uint32_t hi) { return std::uniform_int_distribution<uint32_t>(lo, hi)(rnd); };
    size_t checked_seeks = 0;
    for (uint32_t round = 0; round < 1000; ++round) {
        uint32_t              docid_limit = uniform(2, 400);
        uint32_t              num_children = uniform(1, 7);
        std::vector<uint32_t> sources(12);
        std::iota(sources.begin(), sources.end(), 0);
        std::shuffle(sources.begin(), sources.end(), rnd);
        std::vector<ChildSpec> children;
        for (uint32_t i = 0; i < num_children; ++i) {
            ChildSpec spec{sources[i], {}, uniform(0, 1) == 1};
            double    density = std::array<double, 5>{0.0, 0.02, 0.1, 0.5, 0.95}[uniform(0, 4)];
            for (uint32_t docid = 1; docid < docid_limit + 20; ++docid) {
                if (std::bernoulli_distribution(density)(rnd)) {
                    spec.docids.push_back(docid);
                }
            }
            children.push_back(std::move(spec));
        }
        std::vector<uint32_t> owner(docid_limit);
        for (auto& source : owner) {
            // mostly sources with a child, sometimes a source without one
            source = (uniform(0, 9) < 8) ? children[uniform(0, num_children - 1)].source : uniform(0, 11);
        }
        BlendSetup setup(std::move(children), std::move(owner));
        UnpackLog  legacy_unpacked;
        UnpackLog  current_unpacked;
        auto       legacy = make_strict_blender(setup, true, legacy_unpacked);
        auto       current = make_strict_blender(setup, false, current_unpacked);
        for (uint32_t range = 0; range < 3; ++range) {
            uint32_t begin_id = uniform(1, docid_limit + 5);
            uint32_t end_id = (uniform(0, 3) == 0) ? endDocId : uniform(begin_id + 1, docid_limit + 10);
            legacy->initRange(begin_id, end_id);
            current->initRange(begin_id, end_id);
            uint32_t docid = begin_id - 1;
            for (uint32_t step = 0; step < 200 && !current->isAtEnd(); ++step) {
                uint32_t target = docid + 1;
                uint32_t op = uniform(0, 9);
                if (op < 3) {
                    target += uniform(0, 15);
                } else if (op == 3) {
                    target = uniform(begin_id, docid_limit + 10);
                }
                bool legacy_hit = legacy->seek(target);
                bool current_hit = current->seek(target);
                ASSERT_EQ(legacy_hit, current_hit);
                ASSERT_EQ(legacy->getDocId(), current->getDocId());
                if (target > docid) {
                    ASSERT_EQ(setup.expected_seek(target, end_id), current->getDocId())
                        << "round " << round << " target " << target << " range [" << begin_id << "," << end_id
                        << ")";
                    ++checked_seeks;
                }
                docid = current->getDocId();
                if (current_hit && uniform(0, 1) == 1) {
                    legacy->unpack(target);
                    current->unpack(target);
                }
            }
        }
        ASSERT_EQ(legacy_unpacked, current_unpacked);
        for (size_t i = 0; i < current_unpacked.size(); ++i) {
            for (uint32_t docid : current_unpacked[i]) {
                EXPECT_EQ(int(i), setup.hit_child(docid)) << "round " << round << " docid " << docid;
            }
        }
    }
    EXPECT_GT(checked_seeks, 10000u);
}

GTEST_MAIN_RUN_ALL_TESTS()
