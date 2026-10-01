// Copyright Vespa.ai. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.

#pragma once

#include <vespa/searchlib/queryeval/isourceselector.h>
#include <vespa/searchlib/queryeval/sourceblendersearch.h>

#include <vespa/vespalib/util/array.hpp>

namespace search::queryeval::test {

/**
 * The strict source blender as implemented before child docids were
 * cached: every candidate docid scans all children. Kept as a
 * reference for differential testing and benchmarking.
 **/
class LegacyStrictSourceBlender : public SourceBlenderSearch {
private:
    vespalib::Array<SearchIterator*> _nextChildren;

    __attribute__((noinline)) void advance() {
        for (;;) {
            SearchIterator* search = getSearch(_children[0]);
            uint32_t        minNextId = search->getDocId();
            _nextChildren.clear();
            _nextChildren.push_back_fast(search);
            for (uint32_t i = 1; i < _children.size(); ++i) {
                search = getSearch(_children[i]);
                uint32_t nextId = search->getDocId();
                if (nextId < minNextId) {
                    minNextId = nextId;
                    _nextChildren.clear();
                    _nextChildren.push_back_fast(search);
                } else if (nextId == minNextId) {
                    _nextChildren.push_back_fast(search);
                }
            }
            if (isAtEnd(minNextId)) {
                setAtEnd();
                return;
            }
            if (minNextId >= _docIdLimit) {
                setAtEnd();
                return;
            }
            search = getSearch(_sourceSelector->getSource(minNextId));
            for (SearchIterator* child : _nextChildren) {
                if (child == search) {
                    _matchedChild = search;
                    setDocId(minNextId);
                    return;
                }
                child->seek(minNextId + 1);
            }
        }
    }

    void doSeek(uint32_t docid) override {
        if (docid >= _docIdLimit) {
            setDocId(endDocId);
            return;
        }
        _matchedChild = getSearch(_sourceSelector->getSource(docid));
        if (_matchedChild->seek(docid)) {
            setDocId(docid);
        } else {
            for (auto& child : _children) {
                getSearch(child)->seek(docid);
            }
            advance();
        }
    }

    Trinary is_strict() const override { return Trinary::True; }

public:
    LegacyStrictSourceBlender(std::unique_ptr<Iterator> sourceSelector, const Children& children)
        : SourceBlenderSearch(std::move(sourceSelector), children), _nextChildren() {
        _nextChildren.reserve(children.size());
    }
    static std::unique_ptr<SearchIterator> create(std::unique_ptr<Iterator> sourceSelector,
                                                  const Children&           children) {
        return std::make_unique<LegacyStrictSourceBlender>(std::move(sourceSelector), children);
    }
};

} // namespace search::queryeval::test
