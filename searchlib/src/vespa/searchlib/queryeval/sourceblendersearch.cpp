// Copyright Vespa.ai. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.

#include "sourceblendersearch.h"

#include "isourceselector.h"

#include <vespa/vespalib/stllike/asciistream.h>

#include <vespa/vespalib/objects/visit.hpp>

#include <algorithm>
#include <limits>

namespace search::queryeval {

EmptySearch SourceBlenderSearch::_emptySearch;

class SourceBlenderSearchNonStrict : public SourceBlenderSearch {
public:
    SourceBlenderSearchNonStrict(std::unique_ptr<Iterator> sourceSelector, const Children& children)
        : SourceBlenderSearch(std::move(sourceSelector), children) {}
    ~SourceBlenderSearchNonStrict() override;
};

SourceBlenderSearchNonStrict::~SourceBlenderSearchNonStrict() = default;

/**
 * The current docid of each child is mirrored in a contiguous array,
 * updated only when the blender moves that child. The child at the
 * smallest docid is tracked together with the smallest docid of all
 * the other children. While the leading child stays below that bound
 * (typically one large source with a few small ones), producing the
 * next hit costs one child seek and one source selector lookup,
 * independent of the number of children.
 **/
class SourceBlenderSearchStrict : public SourceBlenderSearch {
public:
    SourceBlenderSearchStrict(std::unique_ptr<Iterator> sourceSelector, const Children& children);
    ~SourceBlenderSearchStrict() override;
    void initRange(uint32_t begin_id, uint32_t end_id) override;
    void transform_children(std::function<SearchIterator::UP(SearchIterator::UP)> f) override;

private:
    // value of the extra last _child_docid entry, so a blender without children is at end
    static constexpr uint32_t no_child_docid = std::numeric_limits<uint32_t>::max();

    std::vector<uint32_t> _child_docid; // in _children order
    uint32_t              _min_idx;     // child at the smallest docid
    uint32_t              _others_min;  // smallest docid of all other children
    SearchIterator*       _min_child;

    void sync_child_docids();
    void update_min() noexcept;
    // the child must be positioned before docid
    void seek_child(uint32_t idx, uint32_t docid) {
        SearchIterator* child = getSearch(_children[idx]);
        child->doSeek(docid);
        _child_docid[idx] = child->getDocId();
    }
    void seek_min_child(uint32_t docid) {
        _min_child->doSeek(docid);
        uint32_t child_docid = _min_child->getDocId();
        _child_docid[_min_idx] = child_docid;
        if (child_docid > _others_min) {
            update_min();
        }
    }
    // the child selected for candidate if it is positioned at candidate, otherwise nullptr
    SearchIterator* selected_child_at(uint32_t candidate) const {
        SearchIterator* selected = getSearch(_sourceSelector->getSource(candidate));
        if (selected == _min_child) {
            return selected;
        }
        if (candidate == _others_min) {
            for (uint32_t i = 0; i < _children.size(); ++i) {
                if (_child_docid[i] == candidate && getSearch(_children[i]) == selected) {
                    return selected;
                }
            }
        }
        return nullptr;
    }
    void advance(uint32_t docid);

    void doSeek(uint32_t docid) override;
    Trinary is_strict() const override { return Trinary::True; }
};

SourceBlenderSearchStrict::SourceBlenderSearchStrict(std::unique_ptr<Iterator> sourceSelector,
                                                     const Children&           children)
    : SourceBlenderSearch(std::move(sourceSelector), children),
      _child_docid(_children.size() + 1, no_child_docid),
      _min_idx(0),
      _others_min(no_child_docid),
      _min_child(nullptr) {
    sync_child_docids();
}

SourceBlenderSearchStrict::~SourceBlenderSearchStrict() = default;

void SourceBlenderSearchStrict::initRange(uint32_t begin_id, uint32_t end_id) {
    SourceBlenderSearch::initRange(begin_id, end_id);
    sync_child_docids();
}

void SourceBlenderSearchStrict::transform_children(std::function<SearchIterator::UP(SearchIterator::UP)> f) {
    SourceBlenderSearch::transform_children(std::move(f));
    sync_child_docids();
}

void SourceBlenderSearchStrict::sync_child_docids() {
    for (uint32_t i = 0; i < _children.size(); ++i) {
        _child_docid[i] = getSearch(_children[i])->getDocId();
    }
    update_min();
}

void SourceBlenderSearchStrict::update_min() noexcept {
    uint32_t min_idx = 0;
    uint32_t min_docid = no_child_docid;
    uint32_t others_min = no_child_docid;
    for (uint32_t i = 0; i < _children.size(); ++i) {
        uint32_t docid = _child_docid[i];
        bool     less = (docid < min_docid);
        others_min = less ? min_docid : std::min(others_min, docid);
        min_idx = less ? i : min_idx;
        min_docid = less ? docid : min_docid;
    }
    _min_idx = min_idx;
    _others_min = others_min;
    _min_child = _children.empty() ? nullptr : getSearch(_children[min_idx]);
}

void SourceBlenderSearch::doSeek(uint32_t docid) {
    if (docid >= _docIdLimit) {
        setDocId(endDocId);
        return;
    }
    _matchedChild = getSearch(_sourceSelector->getSource(docid));
    if (_matchedChild->seek(docid)) {
        setDocId(docid);
    }
}

void SourceBlenderSearchStrict::doSeek(uint32_t docid) {
    if (docid >= _docIdLimit) {
        setAtEnd();
        return;
    }
    if (docid <= _others_min) {
        if (_child_docid[_min_idx] < docid) {
            seek_min_child(docid);
        }
    } else {
        for (uint32_t i = 0; i < _children.size(); ++i) {
            if (_child_docid[i] < docid) {
                seek_child(i, docid);
            }
        }
        update_min();
    }
    advance(docid);
}

void SourceBlenderSearchStrict::advance(uint32_t docid) {
    const uint32_t limit = std::min(_docIdLimit, getEndId());
    for (;;) {
        const uint32_t candidate = _child_docid[_min_idx];
        if (__builtin_expect(candidate >= limit, false)) {
            // seeking exactly to a hit beyond the end id still reports it, like the non-strict blender does
            SearchIterator* child = (candidate == docid) ? selected_child_at(candidate) : nullptr;
            if (child != nullptr) {
                _matchedChild = child;
                setDocId(candidate);
            } else {
                setAtEnd();
            }
            return;
        }
        if (SearchIterator* child = selected_child_at(candidate)) {
            _matchedChild = child;
            setDocId(candidate);
            return;
        }
        // a newer source owns candidate; move every child positioned at it
        if (candidate < _others_min) {
            seek_min_child(candidate + 1);
        } else {
            for (uint32_t i = 0; i < _children.size(); ++i) {
                if (_child_docid[i] == candidate) {
                    seek_child(i, candidate + 1);
                }
            }
            update_min();
        }
    }
}

void SourceBlenderSearch::doUnpack(uint32_t docid) {
    _matchedChild->doUnpack(docid);
}

SourceBlenderSearch::SourceBlenderSearch(std::unique_ptr<sourceselector::Iterator> sourceSelector,
                                         const Children&                           children)
    : _matchedChild(nullptr),
      _sourceSelector(std::move(sourceSelector)),
      _children(),
      _docIdLimit(_sourceSelector->getDocIdLimit()) {
    for (size_t i(0); i < sizeof(_sources) / sizeof(_sources[0]); i++) {
        _sources[i] = &_emptySearch;
    }
    for (auto& child : children) {
        Source sid(child.sourceId);
        _children.push_back(sid);
        _sources[sid] = child.search;
    }
}

void SourceBlenderSearch::initRange(uint32_t beginid, uint32_t endid) {
    SearchIterator::initRange(beginid, endid);
    for (auto& child : _children) {
        getSearch(child)->initRange(beginid, endid);
    }
}

void SourceBlenderSearch::visitMembers(vespalib::ObjectVisitor& visitor) const {
    visit(visitor, "children", _children);
    for (const auto& child : _children) {
        vespalib::asciistream os;
        os << "Source " << uint32_t(child);
        visit(visitor, os.str(), *getSearch(child));
    }
}

SourceBlenderSearch::~SourceBlenderSearch() {
    for (auto& child : _children) {
        delete getSearch(child);
    }
}

void SourceBlenderSearch::transform_children(std::function<SearchIterator::UP(SearchIterator::UP)> f) {
    for (size_t i = 0; i < _children.size(); ++i) {
        SearchIterator::UP ptr(_sources[_children[i]]);
        _sources[_children[i]] = nullptr;
        ptr = f(std::move(ptr));
        _sources[_children[i]] = ptr.release();
    }
}

SearchIterator::UP SourceBlenderSearch::create(std::unique_ptr<sourceselector::Iterator> sourceSelector,
                                               const Children& children, bool strict) {
    if (strict) {
        return std::make_unique<SourceBlenderSearchStrict>(std::move(sourceSelector), children);
    } else {
        return std::make_unique<SourceBlenderSearchNonStrict>(std::move(sourceSelector), children);
    }
}

void SourceBlenderSearch::get_element_ids(uint32_t docid, std::vector<uint32_t>& element_ids) {
    if (seek(docid)) {
        _matchedChild->get_element_ids(docid, element_ids);
    }
}

void SourceBlenderSearch::and_element_ids_into(uint32_t docid, std::vector<uint32_t>& element_ids) {
    if (seek(docid)) {
        _matchedChild->and_element_ids_into(docid, element_ids);
    } else {
        element_ids.clear();
    }
}

} // namespace search::queryeval

void visit(vespalib::ObjectVisitor& self, const std::string& name,
           const search::queryeval::SourceBlenderSearch::Child& obj) {
    self.openStruct(name, "search::queryeval::SourceBlenderSearch::Child");
    visit(self, "search", obj.search);
    visit(self, "sourceId", obj.sourceId);
    self.closeStruct();
}
