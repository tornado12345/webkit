/*
 * Copyright (C) 2019 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "InlineLineBuilder.h"

#if ENABLE(LAYOUT_FORMATTING_CONTEXT)

#include "FloatingContext.h"
#include "InlineFormattingContext.h"
#include "LayoutBox.h"
#include "LayoutBoxGeometry.h"
#include "LayoutState.h"
#include "TextUtil.h"
#include <wtf/unicode/CharacterNames.h>

namespace WebCore {
namespace Layout {

static inline bool endsWithSoftWrapOpportunity(const InlineTextItem& currentTextItem, const InlineTextItem& nextInlineTextItem)
{
    ASSERT(!nextInlineTextItem.isWhitespace());
    // We are at the position after a whitespace.
    if (currentTextItem.isWhitespace())
        return true;
    // When both these non-whitespace runs belong to the same layout box, it's guaranteed that
    // they are split at a soft breaking opportunity. See InlineTextItem::moveToNextBreakablePosition.
    if (&currentTextItem.inlineTextBox() == &nextInlineTextItem.inlineTextBox())
        return true;
    // Now we need to collect at least 3 adjacent characters to be able to make a decision whether the previous text item ends with breaking opportunity.
    // [ex-][ample] <- second to last[x] last[-] current[a]
    // We need at least 1 character in the current inline text item and 2 more from previous inline items.
    auto previousContent = currentTextItem.inlineTextBox().content();
    auto lineBreakIterator = LazyLineBreakIterator { nextInlineTextItem.inlineTextBox().content() };
    auto previousContentLength = previousContent.length();
    // FIXME: We should look into the entire uncommitted content for more text context.
    UChar lastCharacter = previousContentLength ? previousContent[previousContentLength - 1] : 0;
    if (lastCharacter == softHyphen && currentTextItem.style().hyphens() == Hyphens::None)
        return false;
    UChar secondToLastCharacter = previousContentLength > 1 ? previousContent[previousContentLength - 2] : 0;
    lineBreakIterator.setPriorContext(lastCharacter, secondToLastCharacter);
    // Now check if we can break right at the inline item boundary.
    // With the [ex-ample], findNextBreakablePosition should return the startPosition (0).
    // FIXME: Check if there's a more correct way of finding breaking opportunities.
    return !TextUtil::findNextBreakablePosition(lineBreakIterator, 0, nextInlineTextItem.style());
}

static inline bool isAtSoftWrapOpportunity(const InlineFormattingContext& inlineFormattingContext, const InlineItem& current, const InlineItem& next)
{
    // "is at" simple means that there's a soft wrap opportunity right after the [current].
    // [text][ ][text][container start]... (<div>text content<span>..</div>)
    // soft wrap indexes: 0 and 1 definitely, 2 depends on the content after the [container start].

    // https://drafts.csswg.org/css-text-3/#line-break-details
    // Figure out if the new incoming content puts the uncommitted content on a soft wrap opportunity.
    // e.g. [container start][prior_continuous_content][container end] (<span>prior_continuous_content</span>)
    // An incoming <img> box would enable us to commit the "<span>prior_continuous_content</span>" content
    // but an incoming text content would not necessarily.
    ASSERT(current.isText() || current.isBox());
    ASSERT(next.isText() || next.isBox());
    if (current.isText() && next.isText()) {
        auto& currentInlineTextItem = downcast<InlineTextItem>(current);
        auto& nextInlineTextItem = downcast<InlineTextItem>(next);
        if (currentInlineTextItem.isWhitespace()) {
            // [ ][text] : after [whitespace] position is a soft wrap opportunity.
            return true;
        }
        if (nextInlineTextItem.isWhitespace()) {
            // [text][ ] (<span>text</span> )
            // white-space: break-spaces: line breaking opportunity exists after every preserved white space character, but not before.
            return nextInlineTextItem.style().whiteSpace() != WhiteSpace::BreakSpaces;
        }
        if (current.style().lineBreak() == LineBreak::Anywhere || next.style().lineBreak() == LineBreak::Anywhere) {
            // There is a soft wrap opportunity around every typographic character unit, including around any punctuation character
            // or preserved white spaces, or in the middle of words.
            return true;
        }
        // Both current and next items are non-whitespace text.
        // [text][text] : is a continuous content.
        // [text-][text] : after [hyphen] position is a soft wrap opportunity.
        return endsWithSoftWrapOpportunity(currentInlineTextItem, nextInlineTextItem);
    }
    if (current.isBox() || next.isBox()) {
        auto isImageContent = current.layoutBox().isImage() || next.layoutBox().isImage();
        if (isImageContent)
            return inlineFormattingContext.quirks().hasSoftWrapOpportunityAtImage();
        // [text][container start][container end][inline box] (text<span></span><img>) : there's a soft wrap opportunity between the [text] and [img].
        // The line breaking behavior of a replaced element or other atomic inline is equivalent to an ideographic character.
        return true;
    }
    ASSERT_NOT_REACHED();
    return true;
}

struct LineCandidate {
    LineCandidate(bool ignoreTrailingLetterSpacing);

    void reset(InlineLayoutUnit contentLogicalLeft);

    struct InlineContent {
        InlineContent(bool ignoreTrailingLetterSpacing);

        const InlineContentBreaker::ContinuousContent& continuousContent() const { return m_continuousContent; }
        const InlineItem* trailingLineBreak() const { return m_trailingLineBreak; }
        const InlineItem* trailingWordBreakOpportunity() const { return m_trailingWordBreakOpportunity; }

        void appendInlineItem(const InlineItem&, InlineLayoutUnit logicalWidth);
        void appendTrailingLineBreak(const InlineItem& lineBreakItem) { m_trailingLineBreak = &lineBreakItem; }
        void appendtrailingWordBreakOpportunity(const InlineItem& wordBreakItem) { m_trailingWordBreakOpportunity = &wordBreakItem; }
        void reset(InlineLayoutUnit contentLogicalLeft);

    private:
        bool m_ignoreTrailingLetterSpacing { false };

        InlineContentBreaker::ContinuousContent m_continuousContent;
        const InlineItem* m_trailingLineBreak { nullptr };
        const InlineItem* m_trailingWordBreakOpportunity { nullptr };
    };

    struct FloatContent {
        void append(const InlineItem& floatItem, InlineLayoutUnit logicalWidth, bool isIntrusive);

        struct Float {
            const InlineItem* item { nullptr };
            InlineLayoutUnit logicalWidth { 0 };
            bool isIntrusive { true };
        };
        using FloatList = Vector<Float>;
        const FloatList& list() const { return m_floatList; }
        InlineLayoutUnit intrusiveWidth() const { return m_intrusiveWidth; }

        void reset();

    private:
        FloatList m_floatList;
        InlineLayoutUnit m_intrusiveWidth { 0 };
    };
    // Candidate content is a collection of inline items and/or float boxes.
    InlineContent inlineContent;
    FloatContent floatContent;
};

LineCandidate::LineCandidate(bool ignoreTrailingLetterSpacing)
    : inlineContent(ignoreTrailingLetterSpacing)
{
}

LineCandidate::InlineContent::InlineContent(bool ignoreTrailingLetterSpacing)
    : m_ignoreTrailingLetterSpacing(ignoreTrailingLetterSpacing)
{
}

inline void LineCandidate::InlineContent::appendInlineItem(const InlineItem& inlineItem, InlineLayoutUnit logicalWidth)
{
    ASSERT(inlineItem.isText() || inlineItem.isBox() || inlineItem.isContainerStart() || inlineItem.isContainerEnd());
    auto collapsibleWidth = [&]() -> Optional<InlineLayoutUnit> {
        if (!inlineItem.isText())
            return { };
        auto& inlineTextItem = downcast<InlineTextItem>(inlineItem);
        if (inlineTextItem.isWhitespace() && !TextUtil::shouldPreserveTrailingWhitespace(inlineTextItem.style())) {
            // Fully collapsible trailing content.
            return logicalWidth;
        }
        // Check for partially collapsible content.
        if (m_ignoreTrailingLetterSpacing)
            return { };
        auto letterSpacing = inlineItem.style().letterSpacing();
        if (letterSpacing <= 0)
            return { };
        ASSERT(logicalWidth > letterSpacing);
        return letterSpacing;
    };
    m_continuousContent.append(inlineItem, logicalWidth, collapsibleWidth());
}

inline void LineCandidate::InlineContent::reset(InlineLayoutUnit contentLogicalLeft)
{
    m_continuousContent.reset(contentLogicalLeft);
    m_trailingLineBreak = { };
    m_trailingWordBreakOpportunity = { };
}

inline void LineCandidate::FloatContent::append(const InlineItem& floatItem, InlineLayoutUnit logicalWidth, bool isIntrusive)
{
    if (isIntrusive)
        m_intrusiveWidth += logicalWidth;
    m_floatList.append({ &floatItem, logicalWidth, isIntrusive });
}

inline void LineCandidate::FloatContent::reset()
{
    m_floatList.clear();
    m_intrusiveWidth = { };
}

inline void LineCandidate::reset(InlineLayoutUnit contentLogicalLeft)
{
    floatContent.reset();
    inlineContent.reset(contentLogicalLeft);
}

InlineLayoutUnit LineBuilder::inlineItemWidth(const InlineItem& inlineItem, InlineLayoutUnit contentLogicalLeft) const
{
    if (is<InlineTextItem>(inlineItem)) {
        auto& inlineTextItem = downcast<InlineTextItem>(inlineItem);
        if (auto contentWidth = inlineTextItem.width())
            return *contentWidth;
        auto end = inlineTextItem.isCollapsible() ? inlineTextItem.start() + 1 : inlineTextItem.end();
        return TextUtil::width(inlineTextItem, inlineTextItem.start(), end, contentLogicalLeft);
    }

    if (inlineItem.isLineBreak() || inlineItem.isWordBreakOpportunity())
        return { };

    auto& layoutBox = inlineItem.layoutBox();
    auto& boxGeometry = m_inlineFormattingContext.geometryForBox(layoutBox);

    if (layoutBox.isFloatingPositioned())
        return boxGeometry.marginBoxWidth();

    if (layoutBox.isReplacedBox())
        return boxGeometry.marginBoxWidth();

    if (inlineItem.isContainerStart())
        return boxGeometry.marginStart() + boxGeometry.borderLeft() + boxGeometry.paddingLeft().valueOr(0);

    if (inlineItem.isContainerEnd())
        return boxGeometry.marginEnd() + boxGeometry.borderRight() + boxGeometry.paddingRight().valueOr(0);

    // Non-replaced inline box (e.g. inline-block)
    return boxGeometry.marginBoxWidth();
}

LineBuilder::LineBuilder(const InlineFormattingContext& inlineFormattingContext, const FloatingContext& floatingContext, const ContainerBox& formattingContextRoot, const InlineItems& inlineItems)
    : m_inlineFormattingContext(inlineFormattingContext)
    , m_floatingContext(floatingContext)
    , m_formattingContextRoot(formattingContextRoot)
    , m_line(inlineFormattingContext)
    , m_inlineItems(inlineItems)
{
}

LineBuilder::LineContent LineBuilder::layoutInlineContent(const InlineItemRange& needsLayoutRange, size_t partialLeadingContentLength, const InlineRect& lineLogicalConstraints, bool isFirstLine)
{
    auto usedConstraints = constraintsForLine(lineLogicalConstraints, isFirstLine);
    initialize(usedConstraints);

    auto committedContent = placeInlineContent(needsLayoutRange, partialLeadingContentLength);
    auto committedRange = close(needsLayoutRange, committedContent);

    auto lineLogicalTopLeft = InlineLayoutPoint { usedConstraints.logicalLeft, lineLogicalConstraints.top() };
    auto isLastLine = isLastLineWithInlineContent(committedRange, needsLayoutRange.end, committedContent.partialTrailingContentLength);
    return LineContent { committedRange, committedContent.partialTrailingContentLength, m_floats, m_contentIsConstrainedByFloat
        , lineLogicalTopLeft
        , m_line.horizontalConstraint()
        , m_line.contentLogicalWidth()
        , m_line.isVisuallyEmpty()
        , isLastLine
        , m_line.runs()};
}

LineBuilder::IntrinsicContent LineBuilder::computedIntrinsicWidth(const InlineItemRange& needsLayoutRange, InlineLayoutUnit availableWidth)
{
    initialize({ { }, availableWidth, false });
    auto committedContent = placeInlineContent(needsLayoutRange, { });
    auto committedRange = close(needsLayoutRange, committedContent);
    return { committedRange, m_line.contentLogicalWidth() };
}

void LineBuilder::initialize(const UsedConstraints& lineConstraints)
{
    m_floats.clear();
    m_partialLeadingTextItem = { };
    m_wrapOpportunityList.clear();

    m_line.initialize(lineConstraints.availableLogicalWidth);
    m_contentIsConstrainedByFloat = lineConstraints.isConstrainedByFloat;
}

LineBuilder::CommittedContent LineBuilder::placeInlineContent(const InlineItemRange& needsLayoutRange, size_t partialLeadingContentLength)
{
    auto lineCandidate = LineCandidate { layoutState().shouldIgnoreTrailingLetterSpacing() };
    auto inlineContentBreaker = InlineContentBreaker { };

    auto currentItemIndex = needsLayoutRange.start;
    size_t committedInlineItemCount = 0;
    while (currentItemIndex < needsLayoutRange.end) {
        // 1. Collect the set of runs that we can commit to the line as one entity e.g. <span>text_and_span_start_span_end</span>.
        // 2. Apply floats and shrink the available horizontal space e.g. <span>intru_<div style="float: left"></div>sive_float</span>.
        // 3. Check if the content fits the line and commit the content accordingly (full, partial or not commit at all).
        // 4. Return if we are at the end of the line either by not being able to fit more content or because of an explicit line break.
        nextContentForLine(lineCandidate, currentItemIndex, needsLayoutRange, partialLeadingContentLength, m_line.availableWidth() + m_line.trimmableTrailingWidth(), m_line.contentLogicalWidth());
        // Now check if we can put this content on the current line.
        auto result = handleFloatsAndInlineContent(inlineContentBreaker, needsLayoutRange, lineCandidate);
        committedInlineItemCount = result.committedCount.isRevert ? result.committedCount.value : committedInlineItemCount + result.committedCount.value;
        auto& inlineContent = lineCandidate.inlineContent;
        auto inlineContentIsFullyCommitted = inlineContent.continuousContent().runs().size() == result.committedCount.value && !result.partialTrailingContentLength;
        auto isEndOfLine = result.isEndOfLine == InlineContentBreaker::IsEndOfLine::Yes;

        if (inlineContentIsFullyCommitted) {
            if (auto* wordBreakOpportunity = inlineContent.trailingWordBreakOpportunity()) {
                // <wbr> needs to be on the line as an empty run so that we can construct an inline box and compute basic geometry.
                ++committedInlineItemCount;
                m_line.append(*wordBreakOpportunity, { });
            }
            if (inlineContent.trailingLineBreak()) {
                // Fully committed (or empty) content followed by a line break means "end of line".
                m_line.append(*inlineContent.trailingLineBreak(), { });
                ++committedInlineItemCount;
                isEndOfLine = true;
            }
        }
        if (isEndOfLine) {
            // We can't place any more items on the current line.
            return { committedInlineItemCount, result.partialTrailingContentLength };
        }
        currentItemIndex = needsLayoutRange.start + committedInlineItemCount + m_floats.size();
        partialLeadingContentLength = { };
    }
    // Looks like we've run out of runs.
    return { committedInlineItemCount, { } };
}

LineBuilder::InlineItemRange LineBuilder::close(const InlineItemRange& needsLayoutRange, const CommittedContent& committedContent)
{
    ASSERT(committedContent.inlineItemCount || !m_floats.isEmpty() || m_contentIsConstrainedByFloat);
    auto numberOfCommittedItems = committedContent.inlineItemCount + m_floats.size();
    auto trailingInlineItemIndex = needsLayoutRange.start + numberOfCommittedItems - 1;
    auto lineRange = InlineItemRange { needsLayoutRange.start, trailingInlineItemIndex + 1 };
    ASSERT(lineRange.end <= needsLayoutRange.end);
    if (!committedContent.inlineItemCount) {
        // Line is empty, we only managed to place float boxes.
        return lineRange;
    }
    m_line.removeCollapsibleContent();
    auto horizontalAlignment = root().style().textAlign();
    auto runsExpandHorizontally = horizontalAlignment == TextAlignMode::Justify && !isLastLineWithInlineContent(lineRange, needsLayoutRange.end, committedContent.partialTrailingContentLength);
    if (runsExpandHorizontally)
        m_line.applyRunExpansion();
    auto lineEndsWithHyphen = false;
    if (!m_line.isVisuallyEmpty()) {
        ASSERT(!m_line.runs().isEmpty());
        auto& lastTextContent = m_line.runs().last().textContent();
        lineEndsWithHyphen = lastTextContent && lastTextContent->needsHyphen();
    }
    m_successiveHyphenatedLineCount = lineEndsWithHyphen ? m_successiveHyphenatedLineCount + 1 : 0;
    return lineRange;
}

LineBuilder::UsedConstraints LineBuilder::constraintsForLine(const InlineRect& lineLogicalConstraints, bool isFirstLine)
{
    auto lineLogicalLeft = lineLogicalConstraints.left();
    auto lineLogicalTop = lineLogicalConstraints.top();
    auto lineLogicalRight = lineLogicalLeft + lineLogicalConstraints.width();
    auto lineIsConstrainedByFloat = false;

    // Check for intruding floats and adjust logical left/available width for this line accordingly.
    if (!m_floatingContext.isEmpty()) {
        // FIXME: Add support for variable line height, where the intrusive floats should be probed as the line height grows.
        auto floatConstraints = m_floatingContext.constraints(toLayoutUnit(lineLogicalTop), toLayoutUnit(lineLogicalTop + lineLogicalConstraints.height()));
        // Check if these values actually constrain the line.
        if (floatConstraints.left && floatConstraints.left->x <= lineLogicalLeft)
            floatConstraints.left = { };

        if (floatConstraints.right && floatConstraints.right->x >= lineLogicalRight)
            floatConstraints.right = { };

        lineIsConstrainedByFloat = floatConstraints.left || floatConstraints.right;

        if (floatConstraints.left && floatConstraints.right) {
            ASSERT(floatConstraints.left->x <= floatConstraints.right->x);
            lineLogicalRight = floatConstraints.right->x;
            lineLogicalLeft = floatConstraints.left->x;
        } else if (floatConstraints.left) {
            ASSERT(floatConstraints.left->x >= lineLogicalLeft);
            lineLogicalLeft = floatConstraints.left->x;
        } else if (floatConstraints.right) {
            // Right float boxes may overflow the containing block on the left.
            lineLogicalRight = std::max<InlineLayoutUnit>(lineLogicalLeft, floatConstraints.right->x);
        }
    }

    auto computedTextIndent = [&]() -> InlineLayoutUnit {
        // text-indent property specifies the indentation applied to lines of inline content in a block.
        // The indent is treated as a margin applied to the start edge of the line box.
        // Unless otherwise specified, only lines that are the first formatted line of an element are affected.
        // For example, the first line of an anonymous block box is only affected if it is the first child of its parent element.
        // FIXME: Add support for each-line.
        // [Integration] root()->parent() would normally produce a valid layout box.
        auto& root = this->root();
        auto isFormattingContextRootCandidateToTextIndent = !root.isAnonymous();
        if (root.isAnonymous()) {
            // Unless otherwise specified by the each-line and/or hanging keywords, only lines that are the first formatted line
            // of an element are affected.
            // For example, the first line of an anonymous block box is only affected if it is the first child of its parent element.
            auto isIntegratedRootBoxFirstChild = layoutState().isIntegratedRootBoxFirstChild();
            if (isIntegratedRootBoxFirstChild == LayoutState::IsIntegratedRootBoxFirstChild::NotApplicable)
                isFormattingContextRootCandidateToTextIndent = root.parent().firstInFlowChild() == &root;
            else
                isFormattingContextRootCandidateToTextIndent = isIntegratedRootBoxFirstChild == LayoutState::IsIntegratedRootBoxFirstChild::Yes;
        }
        if (!isFormattingContextRootCandidateToTextIndent)
            return { };
        auto invertLineRange = false;
#if ENABLE(CSS3_TEXT)
        invertLineRange = root.style().textIndentType() == TextIndentType::Hanging;
#endif
        // text-indent: hanging inverts which lines are affected.
        // inverted line range -> all the lines except the first one.
        // !inverted line range -> first line gets the indent.
        auto shouldIndent = invertLineRange != isFirstLine;
        if (!shouldIndent)
            return { };

        auto textIndent = root.style().textIndent();
        if (textIndent == RenderStyle::initialTextIndent())
            return { };
        return { minimumValueForLength(textIndent, lineLogicalConstraints.width()) };
    };
    lineLogicalLeft += computedTextIndent();
    return UsedConstraints { lineLogicalLeft, lineLogicalRight - lineLogicalLeft, lineIsConstrainedByFloat };
}

void LineBuilder::nextContentForLine(LineCandidate& lineCandidate, size_t currentInlineItemIndex, const InlineItemRange& layoutRange, size_t partialLeadingContentLength, InlineLayoutUnit availableLineWidth, InlineLayoutUnit currentLogicalRight)
{
    ASSERT(currentInlineItemIndex < layoutRange.end);
    lineCandidate.reset(currentLogicalRight);
    // 1. Simply add any overflow content from the previous line to the candidate content. It's always a text content.
    // 2. Find the next soft wrap position or explicit line break.
    // 3. Collect floats between the inline content.
    auto softWrapOpportunityIndex = nextWrapOpportunity(currentInlineItemIndex, layoutRange);
    // softWrapOpportunityIndex == layoutRange.end means we don't have any wrap opportunity in this content.
    ASSERT(softWrapOpportunityIndex <= layoutRange.end);

    if (partialLeadingContentLength) {
        // Handle leading partial content first (overflowing text from the previous line).
        // Construct a partial leading inline item.
        m_partialLeadingTextItem = downcast<InlineTextItem>(m_inlineItems[currentInlineItemIndex]).right(partialLeadingContentLength);
        auto itemWidth = inlineItemWidth(*m_partialLeadingTextItem, currentLogicalRight);
        lineCandidate.inlineContent.appendInlineItem(*m_partialLeadingTextItem, itemWidth);
        currentLogicalRight += itemWidth;
        ++currentInlineItemIndex;
    }

    auto accumulatedWidth = InlineLayoutUnit { };
    for (auto index = currentInlineItemIndex; index < softWrapOpportunityIndex; ++index) {
        auto& inlineItem = m_inlineItems[index];
        if (inlineItem.isFloat()) {
            // Floats are not part of the line context.
            auto floatWidth = inlineItemWidth(inlineItem, { });
            lineCandidate.floatContent.append(inlineItem, floatWidth, floatWidth <= (availableLineWidth - accumulatedWidth));
            accumulatedWidth += floatWidth;
            continue;
        }
        if (inlineItem.isText() || inlineItem.isContainerStart() || inlineItem.isContainerEnd() || inlineItem.isBox()) {
            auto logicalWidth = inlineItemWidth(inlineItem, currentLogicalRight);
            lineCandidate.inlineContent.appendInlineItem(inlineItem, logicalWidth);
            currentLogicalRight += logicalWidth;
            accumulatedWidth += logicalWidth;
            continue;
        }
        if (inlineItem.isWordBreakOpportunity()) {
            // Since <wbr> is an explicit word break opportunity it has to be a trailing item in this candidate run list.
            ASSERT(index == softWrapOpportunityIndex - 1);
            lineCandidate.inlineContent.appendtrailingWordBreakOpportunity(inlineItem);
            continue;
        }
        if (inlineItem.isLineBreak()) {
            // Since <br> is an forced break opportunity it has to be a trailing item in this candidate run list.
            ASSERT(index == softWrapOpportunityIndex - 1);
            lineCandidate.inlineContent.appendTrailingLineBreak(inlineItem);
            continue;
        }
        ASSERT_NOT_REACHED();
    }
}

size_t LineBuilder::nextWrapOpportunity(size_t startIndex, const LineBuilder::InlineItemRange& layoutRange) const
{
    // 1. Find the start candidate by skipping leading non-content items e.g "<span><span>start". Opportunity is after "<span><span>".
    // 2. Find the end candidate by skipping non-content items inbetween e.g. "<span><span>start</span>end". Opportunity is after "</span>".
    // 3. Check if there's a soft wrap opportunity between the 2 candidate inline items and repeat.
    // 4. Any force line break/explicit wrap content inbetween is considered as wrap opportunity.

    // [ex-][container start][container end][float][ample] (ex-<span></span><div style="float:left"></div>ample). Wrap index is at [ex-].
    // [ex][container start][amp-][container start][le] (ex<span>amp-<span>ample). Wrap index is at [amp-].
    // [ex-][container start][line break][ample] (ex-<span><br>ample). Wrap index is after [br].
    auto previousInlineItemIndex = Optional<size_t> { };
    for (auto index = startIndex; index < layoutRange.end; ++index) {
        auto& inlineItem = m_inlineItems[index];
        if (inlineItem.isLineBreak() || inlineItem.isWordBreakOpportunity()) {
            // We always stop at explicit wrapping opportunities e.g. <br>. The wrap position is after the opportunity position.
            return ++index;
        }
        if (inlineItem.isFloat()) {
            // Floats are not part of the inline content. We ignore them as far as wrap opportunities are concerned.
            // [text][float box][text] is essentially just [text][text]
            continue;
        }
        if (inlineItem.isContainerStart() || inlineItem.isContainerEnd()) {
            // There's no wrapping opportunity between <span>text, <span></span> or </span>text. 
            continue;
        }
        ASSERT(inlineItem.isText() || inlineItem.isBox());
        if (!previousInlineItemIndex) {
            previousInlineItemIndex = index;
            continue;
        }
        if (isAtSoftWrapOpportunity(m_inlineFormattingContext, m_inlineItems[*previousInlineItemIndex], m_inlineItems[index])) {
            // There's a soft wrap opportunity between 'previousInlineItemIndex' and 'index'.
            // Now forward-find from the start position to see where we can actually wrap.
            // [ex-][ample] vs. [ex-][container start][container end][ample]
            // where [ex-] is startContent and [ample] is the nextContent.
            for (auto candidateIndex = *previousInlineItemIndex + 1; candidateIndex < index; ++candidateIndex) {
                if (m_inlineItems[candidateIndex].isContainerStart()) {
                    // inline content and [container start] and [container end] form unbreakable content.
                    // ex-<span></span>ample  : wrap opportunity is after "ex-".
                    // ex-</span></span>ample : wrap opportunity is after "ex-</span></span>".
                    // ex-</span><span>ample</span> : wrap opportunity is after "ex-</span>".
                    // ex-<span><span>ample</span></span> : wrap opportunity is after "ex-".
                    return candidateIndex;
                }
            }
            return index;
        }
        previousInlineItemIndex = index;
    }
    return layoutRange.end;
}

void LineBuilder::commitFloats(const LineCandidate& lineCandidate, CommitIntrusiveFloatsOnly commitIntrusiveOnly)
{
    auto& floatContent = lineCandidate.floatContent;
    auto leftIntrusiveFloatsWidth = InlineLayoutUnit { };
    auto rightIntrusiveFloatsWidth = InlineLayoutUnit { };

    for (auto& floatCandidate : floatContent.list()) {
        if (!floatCandidate.isIntrusive && commitIntrusiveOnly == CommitIntrusiveFloatsOnly::Yes)
            continue;
        m_floats.append({ floatCandidate.isIntrusive, floatCandidate.item });
        if (floatCandidate.isIntrusive) {
            m_contentIsConstrainedByFloat = true;
            // This float is intrusive and it shrinks the current line.
            // Shrink available space for current line.
            if (floatCandidate.item->layoutBox().isLeftFloatingPositioned())
                leftIntrusiveFloatsWidth += floatCandidate.logicalWidth;
            else
                rightIntrusiveFloatsWidth += floatCandidate.logicalWidth;
        }
    }
    if (leftIntrusiveFloatsWidth || rightIntrusiveFloatsWidth) {
        if (leftIntrusiveFloatsWidth)
            m_line.moveLogicalLeft(leftIntrusiveFloatsWidth);
        if (rightIntrusiveFloatsWidth)
            m_line.moveLogicalRight(rightIntrusiveFloatsWidth);
    }
}

LineBuilder::Result LineBuilder::handleFloatsAndInlineContent(InlineContentBreaker& inlineContentBreaker, const InlineItemRange& layoutRange, const LineCandidate& lineCandidate)
{
    auto& continuousInlineContent = lineCandidate.inlineContent.continuousContent();
    if (continuousInlineContent.runs().isEmpty()) {
        commitFloats(lineCandidate);
        return { InlineContentBreaker::IsEndOfLine::No };
    }

    auto shouldDisableHyphenation = [&] {
        auto& style = root().style();
        unsigned limitLines = style.hyphenationLimitLines() == RenderStyle::initialHyphenationLimitLines() ? std::numeric_limits<unsigned>::max() : style.hyphenationLimitLines();
        return m_successiveHyphenatedLineCount >= limitLines;
    };
    if (shouldDisableHyphenation())
        inlineContentBreaker.setHyphenationDisabled();

    auto& floatContent = lineCandidate.floatContent;
    // Check if this new content fits.
    auto availableWidth = m_line.availableWidth() - floatContent.intrusiveWidth();
    auto isLineConsideredEmpty = m_line.isVisuallyEmpty() && !m_contentIsConstrainedByFloat;
    auto lineStatus = InlineContentBreaker::LineStatus { availableWidth, m_line.trimmableTrailingWidth(), m_line.trailingSoftHyphenWidth(), m_line.isTrailingRunFullyTrimmable(), isLineConsideredEmpty };
    auto result = inlineContentBreaker.processInlineContent(continuousInlineContent, lineStatus);
    if (result.lastWrapOpportunityItem)
        m_wrapOpportunityList.append(result.lastWrapOpportunityItem);
    auto& candidateRuns = continuousInlineContent.runs();
    if (result.action == InlineContentBreaker::Result::Action::Keep) {
        // This continuous content can be fully placed on the current line including non-intrusive floats.
        for (auto& run : candidateRuns)
            m_line.append(run.inlineItem, run.logicalWidth);
        commitFloats(lineCandidate);
        return { result.isEndOfLine, { candidateRuns.size(), false } };
    }
    if (result.action == InlineContentBreaker::Result::Action::Wrap) {
        ASSERT(result.isEndOfLine == InlineContentBreaker::IsEndOfLine::Yes);
        // This continuous content can't be placed on the current line. Nothing to commit at this time.
        return { InlineContentBreaker::IsEndOfLine::Yes };
    }
    if (result.action == InlineContentBreaker::Result::Action::WrapWithHyphen) {
        ASSERT(result.isEndOfLine == InlineContentBreaker::IsEndOfLine::Yes);
        // This continuous content can't be placed on the current line, nothing to commit.
        // However we need to make sure that the current line gains a trailing hyphen.
        ASSERT(m_line.trailingSoftHyphenWidth());
        m_line.addTrailingHyphen(*m_line.trailingSoftHyphenWidth());
        return { InlineContentBreaker::IsEndOfLine::Yes };
    }
    if (result.action == InlineContentBreaker::Result::Action::RevertToLastWrapOpportunity) {
        ASSERT(result.isEndOfLine == InlineContentBreaker::IsEndOfLine::Yes);
        // Not only this content can't be placed on the current line, but we even need to revert the line back to an earlier position.
        ASSERT(!m_wrapOpportunityList.isEmpty());
        return { InlineContentBreaker::IsEndOfLine::Yes, { rebuildLine(layoutRange, *m_wrapOpportunityList.last()), true } };
    }
    if (result.action == InlineContentBreaker::Result::Action::RevertToLastNonOverflowingWrapOpportunity) {
        ASSERT(result.isEndOfLine == InlineContentBreaker::IsEndOfLine::Yes);
        ASSERT(!m_wrapOpportunityList.isEmpty());
        return { InlineContentBreaker::IsEndOfLine::Yes, { rebuildLineForTrailingSoftHyphen(layoutRange), true } };
    }
    if (result.action == InlineContentBreaker::Result::Action::Break) {
        ASSERT(result.isEndOfLine == InlineContentBreaker::IsEndOfLine::Yes);
        // Commit the combination of full and partial content on the current line.
        commitFloats(lineCandidate, CommitIntrusiveFloatsOnly::Yes);
        ASSERT(result.partialTrailingContent);
        commitPartialContent(candidateRuns, *result.partialTrailingContent);
        // When breaking multiple runs <span style="word-break: break-all">text</span><span>content</span>, we might end up breaking them at run boundary.
        // It simply means we don't really have a partial run. Partial content yes, but not partial run.
        auto trailingRunIndex = result.partialTrailingContent->trailingRunIndex;
        auto committedInlineItemCount = trailingRunIndex + 1;
        if (!result.partialTrailingContent->partialRun)
            return { InlineContentBreaker::IsEndOfLine::Yes, { committedInlineItemCount, false } };

        auto partialRun = *result.partialTrailingContent->partialRun;
        auto& trailingInlineTextItem = downcast<InlineTextItem>(candidateRuns[trailingRunIndex].inlineItem);
        ASSERT(partialRun.length < trailingInlineTextItem.length());
        auto overflowLength = trailingInlineTextItem.length() - partialRun.length;
        return { InlineContentBreaker::IsEndOfLine::Yes, { committedInlineItemCount, false }, overflowLength };
    }
    ASSERT_NOT_REACHED();
    return { InlineContentBreaker::IsEndOfLine::No };
}

void LineBuilder::commitPartialContent(const InlineContentBreaker::ContinuousContent::RunList& runs, const InlineContentBreaker::Result::PartialTrailingContent& partialTrailingContent)
{
    for (size_t index = 0; index < runs.size(); ++index) {
        auto& run = runs[index];
        if (partialTrailingContent.trailingRunIndex == index) {
            ASSERT(run.inlineItem.isText());
            // Create and commit partial trailing item.
            if (auto partialRun = partialTrailingContent.partialRun) {
                auto& trailingInlineTextItem = downcast<InlineTextItem>(runs[partialTrailingContent.trailingRunIndex].inlineItem);
                auto partialTrailingTextItem = trailingInlineTextItem.left(partialRun->length);
                m_line.append(partialTrailingTextItem, partialRun->logicalWidth);
                if (auto hyphenWidth = partialRun->hyphenWidth)
                    m_line.addTrailingHyphen(*hyphenWidth);
                return;
            }
            // The partial run is the last content to commit.
            m_line.append(run.inlineItem, run.logicalWidth);
            return;
        }
        m_line.append(run.inlineItem, run.logicalWidth);
    }
}

size_t LineBuilder::rebuildLine(const InlineItemRange& layoutRange, const InlineItem& lastInlineItemToAdd)
{
    ASSERT(!m_wrapOpportunityList.isEmpty());
    // We might already have added intrusive floats. They shrink the avilable horizontal space for the line.
    // Let's just reuse what the line has at this point.
    m_line.initialize(m_line.horizontalConstraint());
    auto currentItemIndex = layoutRange.start;
    if (m_partialLeadingTextItem) {
        m_line.append(*m_partialLeadingTextItem, inlineItemWidth(*m_partialLeadingTextItem, { }));
        if (&m_partialLeadingTextItem.value() == &lastInlineItemToAdd)
            return 1;
        ++currentItemIndex;
    }
    for (; currentItemIndex < layoutRange.end; ++currentItemIndex) {
        auto& inlineItem = m_inlineItems[currentItemIndex];
        m_line.append(inlineItem, inlineItemWidth(inlineItem, m_line.contentLogicalWidth()));
        if (&inlineItem == &lastInlineItemToAdd)
            return currentItemIndex - layoutRange.start + 1;
    }
    return layoutRange.size();
}

size_t LineBuilder::rebuildLineForTrailingSoftHyphen(const InlineItemRange& layoutRange)
{
    ASSERT(!m_wrapOpportunityList.isEmpty());
    // Revert all the way back to a wrap opportunity when either a soft hyphen fits or no hyphen is required.
    for (auto i = m_wrapOpportunityList.size(); i-- > 1;) {
        auto& softWrapOpportunityItem = *m_wrapOpportunityList[i];
        // FIXME: If this turns out to be a perf issue, we could also traverse the wrap list and keep adding the items
        // while watching the available width very closely.
        auto committedCount = rebuildLine(layoutRange, softWrapOpportunityItem);
        auto trailingSoftHyphenWidth = m_line.trailingSoftHyphenWidth();
        // Check if the trailing hyphen now fits the line (or we don't need hyhen anymore).
        if (!trailingSoftHyphenWidth || trailingSoftHyphenWidth <= m_line.availableWidth()) {
            if (trailingSoftHyphenWidth)
                m_line.addTrailingHyphen(*trailingSoftHyphenWidth);
            return committedCount;
        }
    }
    // Have at least some content on the line.
    auto committedCount = rebuildLine(layoutRange, *m_wrapOpportunityList.first());
    if (auto trailingSoftHyphenWidth = m_line.trailingSoftHyphenWidth())
        m_line.addTrailingHyphen(*trailingSoftHyphenWidth);
    return committedCount;
}

bool LineBuilder::isLastLineWithInlineContent(const InlineItemRange& lineRange, size_t lastInlineItemIndex, bool hasPartialTrailingContent) const
{
    if (hasPartialTrailingContent)
        return false;
    if (lineRange.end == lastInlineItemIndex)
        return true;
    // Omit floats to see if this is the last line with inline content.
    for (auto i = lastInlineItemIndex; i--;) {
        if (!m_inlineItems[i].isFloat())
            return i == lineRange.end - 1;
    }
    // There has to be at least one non-float item.
    ASSERT_NOT_REACHED();
    return false;
}

const LayoutState& LineBuilder::layoutState() const
{
    return formattingContext().layoutState();
}

}
}

#endif
