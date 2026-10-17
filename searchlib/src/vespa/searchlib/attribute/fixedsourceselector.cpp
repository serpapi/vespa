// Copyright Vespa.ai. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.

#include "fixedsourceselector.h"
#include <vespa/searchcommon/attribute/config.h>
#include <algorithm>

#include <vespa/log/log.h>
LOG_SETUP(".searchlib.attribute.fixed_source_selector");

namespace search {

namespace {

attribute::Config getConfig() { return attribute::Config(attribute::BasicType::INT8); }

uint32_t
capSelector(queryeval::sourceselector::Iterator::SourceStore &store, queryeval::Source defaultSource)
{
    uint32_t committedDocIdLimit = store.getCommittedDocIdLimit();
    uint32_t cappedSources = 0;
    for (uint32_t docId = 0; docId < committedDocIdLimit; ++docId) {
        queryeval::Source source = store.getFast(docId);
        if (source > defaultSource) {
            ++cappedSources;
            store.set(docId, defaultSource);
        }
    }
    return cappedSources;
}

// Same as SourceSelector::getNewSource() for every byte, written so that the compiler vectorises it;
// returns the largest new source.
queryeval::Source
subtractSources(std::span<const int8_t> sources, std::span<int8_t> result, uint32_t diff) noexcept
{
    const auto clampedDiff = static_cast<uint8_t>(std::min(diff, 255u));
    const auto * __restrict src = reinterpret_cast<const uint8_t *>(sources.data());
    auto * __restrict dst = reinterpret_cast<uint8_t *>(result.data());
    const size_t numDocs = sources.size();
    uint8_t maxSource = 0;
    for (size_t docId = 0; docId < numDocs; ++docId) {
        uint8_t source = src[docId];
        uint8_t newSource = source - std::min(source, clampedDiff);
        dst[docId] = newSource;
        maxSource = std::max(maxSource, newSource);
    }
    return maxSource;
}

}

FixedSourceSelector::Iterator::Iterator(const FixedSourceSelector & sourceSelector) :
    IIterator(sourceSelector._source),
    _attributeGuard(sourceSelector._realSource)
{ }

FixedSourceSelector::FixedSourceSelector(queryeval::Source defaultSource,
                                         const std::string & attrBaseFileName,
                                         uint32_t initialNumDocs) :
    SourceSelector(defaultSource, std::make_shared<SourceStore>(attrBaseFileName, getConfig())),
    _source(static_cast<SourceStore &>(*_realSource))
{
    if (initialNumDocs != std::numeric_limits<uint32_t>::max()) {
        const auto defaultValue = static_cast<int8_t>(getDefaultSource());
        _source.init_docs(initialNumDocs + 1, [defaultValue](std::span<int8_t> values) noexcept {
            std::fill(values.begin(), values.end(), defaultValue);
        });
        _source.commit();
    }
}

FixedSourceSelector::~FixedSourceSelector() = default;

FixedSourceSelector::UP
FixedSourceSelector::cloneAndSubtract(const std::string & attrBaseFileName, uint32_t diff)
{
    queryeval::Source newDefault = getNewSource(getDefaultSource(), diff);
    auto selector = std::make_unique< FixedSourceSelector>(newDefault, attrBaseFileName, std::numeric_limits<uint32_t>::max());
    std::span<const int8_t> sources = _source.get_values_for_writer();
    queryeval::Source maxSource = 0;
    selector->_source.init_docs(sources.size(), [sources, diff, &maxSource](std::span<int8_t> values) noexcept {
        maxSource = subtractSources(sources, values, diff);
    });
    assert(maxSource < SOURCE_LIMIT);
    (void) maxSource;
    selector->_source.commit();
    selector->setBaseId(getBaseId() + diff);
    selector->_source.setCommittedDocIdLimit(_source.getCommittedDocIdLimit());
    return selector;
}

FixedSourceSelector::UP
FixedSourceSelector::load(const std::string & baseFileName, uint32_t currentId)
{
    LoadInfo::UP info = extractLoadInfo(baseFileName);
    info->load();
    uint32_t defaultSource = currentId - info->header()._baseId;
    assert(defaultSource < SOURCE_LIMIT);
    if (defaultSource != info->header()._defaultSource) {
        LOG(info, "Default source mismatch: header says %u, should be %u selector %s",
            (uint32_t) info->header()._defaultSource, defaultSource,
            baseFileName.c_str());
    }
    auto selector = std::make_unique<FixedSourceSelector>(defaultSource,
                                                          info->header()._baseFileName,
                                                          std::numeric_limits<uint32_t>::max());
    selector->setBaseId(info->header()._baseId);
    selector->_source.load();
    uint32_t cappedSources = capSelector(selector->_source, selector->getDefaultSource());
    if (cappedSources > 0) {
        LOG(warning, "%u sources capped in source selector %s", cappedSources, baseFileName.c_str());
    }
    return selector;
}

void FixedSourceSelector::reserve(uint32_t numDocs)
{
    const uint32_t maxDoc(_source.getNumDocs());
    const uint32_t newMaxDocIdPlussOne(numDocs + 1);
    if (newMaxDocIdPlussOne > maxDoc) {
        uint32_t newDocId(0);
        for (_source.addDoc(newDocId); newDocId < numDocs; _source.addDoc(newDocId));
    }
    for (uint32_t i = _source.getCommittedDocIdLimit(); i < newMaxDocIdPlussOne; ++i) {
        _source.set(i, getDefaultSource());
    }
}

void
FixedSourceSelector::setSource(uint32_t docId, queryeval::Source source)
{
    assert(source < SOURCE_LIMIT);
    /**
     * Due to matchingloop advancing 1 past end, we need to initialize data that
     * far too.
     **/
    reserve(docId+1);
    _source.update(docId, source);
    _source.updateUncommittedDocIdLimit(docId + 1);
    _source.commit();
}

void
FixedSourceSelector::compactLidSpace(uint32_t lidLimit)
{
    if (lidLimit < _source.getCommittedDocIdLimit()) {
        _source.compactLidSpace(lidLimit + 1);
    }
}

} // namespace search
