/*
 * Copyright (C) 2020 Apple Inc. All rights reserved.
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

#include <WebCore/Document.h>
#include <WebCore/HTMLBodyElement.h>
#include <WebCore/HTMLDivElement.h>
#include <WebCore/HTMLHtmlElement.h>
#include <WebCore/HTMLTextAreaElement.h>
#include <WebCore/Position.h>
#include <WebCore/Settings.h>
#include <WebCore/ShadowRoot.h>
#include <WebCore/SimpleRange.h>
#include <WebCore/TextControlInnerElements.h>

// FIXME: Expose the functions tested here in WebKit internals object, then replace this test with one written in JavaScript.
// FIXME: When doing the above, don't forget to remove the many WEBCORE_EXPORT that were added so we could compile and link this test.

#define EXPECT_BOTH(a, b, forward, reversed) do { EXPECT_STREQ(string(documentOrder(a, b)), forward); EXPECT_STREQ(string(documentOrder(b, a)), reversed); } while (0)
#define EXPECT_EQUIVALENT(a, b) EXPECT_BOTH(a, b, "equivalent", "equivalent")
#define EXPECT_LESS(a, b) EXPECT_BOTH(a, b, "less", "greater")
#define EXPECT_UNORDERED(a, b) EXPECT_BOTH(a, b, "unordered", "unordered")

#define EXPECT_CONTAINS_SELF(a) EXPECT_TRUE(contains(a, a))
#define EXPECT_NESTED(a, b) do { EXPECT_TRUE(contains(a, b)); EXPECT_FALSE(contains(b, a)); } while (0)
#define EXPECT_NOT_CONTAINED(a, b) do { EXPECT_FALSE(contains(a, b)); EXPECT_FALSE(contains(b, a)); } while (0)

#define EXPECT_INTERSECTS_SELF(a) EXPECT_TRUE(intersects(a, a))
#define EXPECT_INTERSECTS_BOTH_WAYS(a, b) do { EXPECT_TRUE(intersects(a, b)); EXPECT_TRUE(intersects(b, a)); } while (0)
#define EXPECT_NOT_INTERSECTING_BOTH_WAYS(a, b) do { EXPECT_FALSE(intersects(a, b)); EXPECT_FALSE(intersects(b, a)); } while (0)

namespace TestWebKitAPI {

using namespace WebCore;

static Ref<Document> createDocument()
{
    HTMLNames::init();
    auto settings = Settings::create(nullptr);
    auto document = Document::create(settings.get(), aboutBlankURL());
    auto documentElement = HTMLHtmlElement::create(document);
    document->appendChild(documentElement);
    auto body = HTMLBodyElement::create(document);
    documentElement->appendChild(body);
    return document;
}

static BoundaryPoint makeBoundaryPoint(Node& node, unsigned offset)
{
    return { node, offset };
}

constexpr const char* string(PartialOrdering ordering)
{
    if (is_lt(ordering))
        return "less";
    if (is_gt(ordering))
        return "greater";
    if (is_eq(ordering))
        return "equivalent";
    return "unordered";
}

static PartialOrdering operator-(PartialOrdering ordering)
{
    if (is_lt(ordering))
        return PartialOrdering::greater;
    if (is_gt(ordering))
        return PartialOrdering::less;
    return ordering;
}

TEST(DocumentOrder, Node)
{
    HTMLNames::init();

    auto document = createDocument();
    auto& body = *document->body();

    EXPECT_EQUIVALENT(document, document);

    EXPECT_EQUIVALENT(body, body);
    EXPECT_LESS(document, body);
    EXPECT_LESS(document, body);

    auto a = HTMLDivElement::create(document);
    EXPECT_EQUIVALENT(a, a);
    EXPECT_UNORDERED(body, a);
    EXPECT_UNORDERED(a, body);

    body.appendChild(a);
    EXPECT_LESS(body, a);
    EXPECT_LESS(body, a);
    EXPECT_EQUIVALENT(a, a);

    auto b = HTMLDivElement::create(document);
    body.appendChild(b);
    EXPECT_LESS(body, b);
    EXPECT_LESS(body, b);
    EXPECT_LESS(a, b);
    EXPECT_LESS(a, b);

    auto c = HTMLDivElement::create(document);
    b->appendChild(c);
    EXPECT_LESS(body, c);
    EXPECT_LESS(body, c);
    EXPECT_LESS(a, c);
    EXPECT_LESS(b, c);
    EXPECT_LESS(a, c);
    EXPECT_LESS(b, c);

    auto d = HTMLDivElement::create(document);
    a->appendChild(d);
    EXPECT_LESS(body, d);
    EXPECT_LESS(body, d);
    EXPECT_LESS(a, d);
    EXPECT_LESS(d, b);
    EXPECT_LESS(d, c);
    EXPECT_LESS(a, d);
    EXPECT_LESS(d, b);
    EXPECT_LESS(d, c);

    auto e = HTMLDivElement::create(document);
    auto f = HTMLDivElement::create(document);
    e->appendChild(f);
    EXPECT_UNORDERED(body, f);
    EXPECT_UNORDERED(f, body);

    auto g = HTMLTextAreaElement::create(document);
    c->appendChild(g);
    EXPECT_LESS(body, g);

    auto& h = *g->innerTextElement();
    EXPECT_LESS(body, h);

    auto& i = a->attachShadow({ ShadowRootMode::Closed }).releaseReturnValue();
    EXPECT_LESS(body, i);
    EXPECT_LESS(i, d);
    EXPECT_LESS(i, b);

    // FIXME: Add tests that cover slots and assignment.
}

TEST(DocumentOrder, BoundaryPointOffsetZero)
{
    auto document = createDocument();
    auto& body = *document->body();

    EXPECT_EQUIVALENT(makeBoundaryPoint(document, 0), makeBoundaryPoint(document, 0));

    EXPECT_LESS(makeBoundaryPoint(document, 0), makeBoundaryPoint(body, 0));

    auto a = HTMLDivElement::create(document);
    EXPECT_EQUIVALENT(makeBoundaryPoint(a, 0), makeBoundaryPoint(a, 0));
    EXPECT_UNORDERED(makeBoundaryPoint(body, 0), makeBoundaryPoint(a, 0));

    body.appendChild(a);
    EXPECT_EQUIVALENT(makeBoundaryPoint(a, 0), makeBoundaryPoint(a, 0));
    EXPECT_LESS(makeBoundaryPoint(body, 0), makeBoundaryPoint(a, 0));

    auto b = HTMLDivElement::create(document);
    body.appendChild(b);
    EXPECT_LESS(makeBoundaryPoint(body, 0), makeBoundaryPoint(b, 0));
    EXPECT_LESS(makeBoundaryPoint(a, 0), makeBoundaryPoint(b, 0));

    auto c = HTMLDivElement::create(document);
    b->appendChild(c);
    EXPECT_LESS(makeBoundaryPoint(body, 0), makeBoundaryPoint(c, 0));
    EXPECT_LESS(makeBoundaryPoint(a, 0), makeBoundaryPoint(c, 0));
    EXPECT_LESS(makeBoundaryPoint(b, 0), makeBoundaryPoint(c, 0));

    auto d = HTMLDivElement::create(document);
    a->appendChild(d);
    EXPECT_LESS(makeBoundaryPoint(body, 0), makeBoundaryPoint(d, 0));
    EXPECT_LESS(makeBoundaryPoint(a, 0), makeBoundaryPoint(d, 0));
    EXPECT_LESS(makeBoundaryPoint(d, 0), makeBoundaryPoint(b, 0));
    EXPECT_LESS(makeBoundaryPoint(d, 0), makeBoundaryPoint(c, 0));

    auto e = HTMLDivElement::create(document);
    auto f = HTMLDivElement::create(document);
    e->appendChild(f);
    EXPECT_UNORDERED(makeBoundaryPoint(body, 0), makeBoundaryPoint(f, 0));

    auto g = HTMLTextAreaElement::create(document);
    auto h = g->innerTextElement();
    ASSERT_TRUE(h);
    c->appendChild(g);
    EXPECT_LESS(makeBoundaryPoint(body, 0), makeBoundaryPoint(g, 0));

    // FIXME: Add tests that cover shadow trees.
}

TEST(DocumentOrder, BoundaryPointOffsets)
{
    auto document = createDocument();
    auto& body = *document->body();

    EXPECT_LESS(makeBoundaryPoint(document, 0), makeBoundaryPoint(document, 1));
    EXPECT_LESS(makeBoundaryPoint(body, 0), makeBoundaryPoint(document, 1));
    EXPECT_LESS(makeBoundaryPoint(body, 1), makeBoundaryPoint(document, 1));

    auto a = HTMLDivElement::create(document);
    body.appendChild(a);
    EXPECT_LESS(makeBoundaryPoint(a, 0), makeBoundaryPoint(body, 1));

    auto b = HTMLDivElement::create(document);
    body.appendChild(b);
    EXPECT_LESS(makeBoundaryPoint(body, 1), makeBoundaryPoint(b, 0));
    EXPECT_LESS(makeBoundaryPoint(b, 0), makeBoundaryPoint(body, 2));

    auto c = HTMLDivElement::create(document);
    b->appendChild(c);
    EXPECT_LESS(makeBoundaryPoint(body, 1), makeBoundaryPoint(c, 0));
    EXPECT_LESS(makeBoundaryPoint(c, 0), makeBoundaryPoint(body, 2));
    EXPECT_LESS(makeBoundaryPoint(a, 1), makeBoundaryPoint(c, 0));
    EXPECT_LESS(makeBoundaryPoint(c, 0), makeBoundaryPoint(b, 1));

    auto d = HTMLDivElement::create(document);
    a->appendChild(d);
    EXPECT_LESS(makeBoundaryPoint(d, 0), makeBoundaryPoint(body, 1));
    EXPECT_LESS(makeBoundaryPoint(d, 0), makeBoundaryPoint(a, 1));
    EXPECT_LESS(makeBoundaryPoint(d, 0), makeBoundaryPoint(b, 1));
    EXPECT_LESS(makeBoundaryPoint(d, 0), makeBoundaryPoint(c, 1));

    auto e = HTMLDivElement::create(document);
    auto f = HTMLDivElement::create(document);
    e->appendChild(f);
    EXPECT_UNORDERED(makeBoundaryPoint(body, 1), makeBoundaryPoint(f, 0));

    auto g = HTMLTextAreaElement::create(document);
    c->appendChild(g);
    EXPECT_LESS(makeBoundaryPoint(body, 1), makeBoundaryPoint(g, 0));
    EXPECT_LESS(makeBoundaryPoint(c, 0), makeBoundaryPoint(c, 1));
    EXPECT_LESS(makeBoundaryPoint(c, 1), makeBoundaryPoint(c, 2));
    EXPECT_LESS(makeBoundaryPoint(c, 0), makeBoundaryPoint(c, 2));
    EXPECT_LESS(makeBoundaryPoint(c, 0), makeBoundaryPoint(g, 0));
    EXPECT_LESS(makeBoundaryPoint(g, 0), makeBoundaryPoint(c, 1));
    EXPECT_LESS(makeBoundaryPoint(g, 0), makeBoundaryPoint(c, 2));

    auto& h = *g->innerTextElement();
    EXPECT_LESS(makeBoundaryPoint(body, 1), makeBoundaryPoint(h, 0));
    EXPECT_LESS(makeBoundaryPoint(c, 0), makeBoundaryPoint(h, 0));
    EXPECT_LESS(makeBoundaryPoint(h, 0), makeBoundaryPoint(c, 1));
    EXPECT_LESS(makeBoundaryPoint(h, 0), makeBoundaryPoint(c, 2));

    // FIXME: Add tests that cover shadow trees.
}

static Vector<Position> allPositionTypes(Node* node, unsigned offset)
{
    Vector<Position> positions;
    bool canContainRangeEndpoint = !node || node->canContainRangeEndPoint();
    if (canContainRangeEndpoint)
        positions.append(makeContainerOffsetPosition(node, offset));
    if (!node && !offset) {
        positions.append({ nullptr, Position::PositionIsBeforeAnchor });
        positions.append({ nullptr, Position::PositionIsAfterAnchor });
    } else {
        if (canContainRangeEndpoint && !offset)
            positions.append({ node, Position::PositionIsBeforeChildren });
        if (canContainRangeEndpoint && offset == (node ? node->length() : 0))
            positions.append({ node, Position::PositionIsAfterChildren });
        if (node) {
            if (offset) {
                if (auto childBefore = node->traverseToChildAt(offset - 1))
                    positions.append({ childBefore, Position::PositionIsAfterAnchor });
            }
            if (auto childAfter = node->traverseToChildAt(offset))
                positions.append({ childAfter, Position::PositionIsBeforeAnchor });
        }
    }
    return positions;
}

constexpr const char* typeStringSuffix(const Position& position)
{
    switch (position.anchorType()) {
    case Position::PositionIsOffsetInAnchor:
        return "";
    case Position::PositionIsBeforeChildren:
        return "[before-children]";
    case Position::PositionIsAfterChildren:
        return "[after-children]";
    case Position::PositionIsBeforeAnchor:
        return "[before]";
    case Position::PositionIsAfterAnchor:
        return "[after]";
    }
    return "[invalid]";
}

static String join(const Vector<String>& vector, ASCIILiteral separator)
{
    StringBuilder builder;
    bool needSeparator = false;
    for (auto& string : vector) {
        if (needSeparator)
            builder.append(separator);
        builder.append(string);
        needSeparator = true;
    }
    return builder.toString();
}

static CString allPositionTypeFailures(const Position& a, Node* nodeB, unsigned offsetB, PartialOrdering expectedResult)
{
    Vector<String> failures;
    for (auto& b : allPositionTypes(nodeB, offsetB)) {
        auto result = string(documentOrder(a, b));
        if (strcmp(result, string(expectedResult)))
            failures.append(makeString("order(b", typeStringSuffix(b), ")=", result, "<expected:", string(expectedResult), '>'));
        result = string(documentOrder(b, a));
        if (strcmp(result, string(-expectedResult)))
            failures.append(makeString("order(b", typeStringSuffix(b), ")=", result, "<expected:", string(-expectedResult), '>'));
    }
    return join(failures, ", "_s).utf8();
}

static CString allPositionTypeFailures(Node* nodeA, unsigned offsetA, Node* nodeB, unsigned offsetB, PartialOrdering expectedResult)
{
    Vector<String> failures;
    for (auto& a : allPositionTypes(nodeA, offsetA)) {
        for (auto& b : allPositionTypes(nodeB, offsetB)) {
            auto result = string(documentOrder(a, b));
            if (strcmp(result, string(expectedResult)))
                failures.append(makeString("order(a", typeStringSuffix(a), ",b", typeStringSuffix(b), ")=", result, "<expected:", string(expectedResult), '>'));
            result = string(documentOrder(b, a));
            if (strcmp(result, string(-expectedResult)))
                failures.append(makeString("order(b", typeStringSuffix(b), ",a", typeStringSuffix(a), ")=", result, "<expected:", string(-expectedResult), '>'));
        }
    }
    return join(failures, " | "_s).utf8();
}

static CString allPositionTypeFailures(Node& nodeA, unsigned offsetA, Node& nodeB, unsigned offsetB, PartialOrdering expectedResult)
{
    return allPositionTypeFailures(&nodeA, offsetA, &nodeB, offsetB, expectedResult);
}

static CString allPositionTypeFailures(Node* nodeA, unsigned offsetA, Node& nodeB, unsigned offsetB, PartialOrdering expectedResult)
{
    return allPositionTypeFailures(nodeA, offsetA, &nodeB, offsetB, expectedResult);
}

static CString allPositionTypeFailures(const Position& a, Node& nodeB, unsigned offsetB, PartialOrdering expectedResult)
{
    return allPositionTypeFailures(a, &nodeB, offsetB, expectedResult);
}

#define TEST_ALL_POSITION_TYPES(nodeA, offsetA, nodeB, offsetB, expectedResult) \
    EXPECT_STREQ(allPositionTypeFailures(nodeA, offsetA, nodeB, offsetB, PartialOrdering::expectedResult).data(), "")
#define TEST_ALL_POSITION_TYPES_B(positionA, nodeB, offsetB, expectedResult) \
    EXPECT_STREQ(allPositionTypeFailures(positionA, nodeB, offsetB, PartialOrdering::expectedResult).data(), "")

static Position makePositionBefore(Node& node)
{
    return { &node, Position::PositionIsBeforeAnchor };
}

static Position makePositionAfter(Node& node)
{
    return { &node, Position::PositionIsAfterAnchor };
}

TEST(DocumentOrder, Positions)
{
    auto document = createDocument();
    auto& body = *document->body();

    TEST_ALL_POSITION_TYPES(nullptr, 0, nullptr, 0, equivalent);
    TEST_ALL_POSITION_TYPES(nullptr, 1, nullptr, 0, equivalent);
    TEST_ALL_POSITION_TYPES(nullptr, 1, nullptr, 1, equivalent);

    TEST_ALL_POSITION_TYPES(nullptr, 0, document, 0, unordered);
    TEST_ALL_POSITION_TYPES(nullptr, 0, document, 1, unordered);

    TEST_ALL_POSITION_TYPES(document, 0, document, 0, equivalent);
    TEST_ALL_POSITION_TYPES(document, 0, document, 1, less);

    TEST_ALL_POSITION_TYPES(document, 0, body, 0, less);
    TEST_ALL_POSITION_TYPES(body, 0, document, 1, less);

    auto a = HTMLDivElement::create(document);
    body.appendChild(a);
    TEST_ALL_POSITION_TYPES(body, 0, a, 0, less);
    TEST_ALL_POSITION_TYPES(a, 0, body, 1, less);

    auto b = HTMLDivElement::create(document);
    body.appendChild(b);
    TEST_ALL_POSITION_TYPES(body, 0, b, 0, less);
    TEST_ALL_POSITION_TYPES(body, 1, b, 0, less);
    TEST_ALL_POSITION_TYPES(b, 0, body, 2, less);

    auto c = HTMLDivElement::create(document);
    b->appendChild(c);
    TEST_ALL_POSITION_TYPES(body, 0, c, 0, less);
    TEST_ALL_POSITION_TYPES(body, 1, c, 0, less);
    TEST_ALL_POSITION_TYPES(c, 0, body, 2, less);
    TEST_ALL_POSITION_TYPES(a, 0, c, 0, less);
    TEST_ALL_POSITION_TYPES(a, 1, c, 0, less);
    TEST_ALL_POSITION_TYPES(b, 0, c, 0, less);
    TEST_ALL_POSITION_TYPES(c, 0, b, 1, less);

    auto d = HTMLDivElement::create(document);
    a->appendChild(d);
    TEST_ALL_POSITION_TYPES(body, 0, d, 0, less);
    TEST_ALL_POSITION_TYPES(d, 0, body, 1, less);
    TEST_ALL_POSITION_TYPES(a, 0, d, 0, less);
    TEST_ALL_POSITION_TYPES(d, 0, a, 1, less);
    TEST_ALL_POSITION_TYPES(d, 0, b, 0, less);
    TEST_ALL_POSITION_TYPES(d, 0, b, 1, less);
    TEST_ALL_POSITION_TYPES(d, 0, c, 0, less);
    TEST_ALL_POSITION_TYPES(d, 0, c, 1, less);

    auto e = HTMLDivElement::create(document);
    EXPECT_UNORDERED(makePositionBefore(document), makePositionBefore(e));
    EXPECT_UNORDERED(makePositionBefore(document), makePositionAfter(e));
    EXPECT_UNORDERED(makePositionAfter(document), makePositionBefore(e));
    EXPECT_UNORDERED(makePositionAfter(document), makePositionAfter(e));

    auto f = HTMLDivElement::create(document);
    e->appendChild(f);
    TEST_ALL_POSITION_TYPES(body, 1, f, 0, unordered);

    auto g = HTMLTextAreaElement::create(document);
    c->appendChild(g);
    TEST_ALL_POSITION_TYPES(body, 1, g, 0, less);
    TEST_ALL_POSITION_TYPES(c, 0, c, 1, less);
    TEST_ALL_POSITION_TYPES(c, 1, c, 2, less);
    TEST_ALL_POSITION_TYPES(c, 0, c, 2, less);
    TEST_ALL_POSITION_TYPES(c, 0, g, 0, less);
    TEST_ALL_POSITION_TYPES(g, 0, c, 1, less);
    TEST_ALL_POSITION_TYPES(g, 0, c, 2, less);

    auto& h = *g->innerTextElement();
    TEST_ALL_POSITION_TYPES(body, 1, h, 0, less);
    TEST_ALL_POSITION_TYPES(c, 0, h, 0, less);
    TEST_ALL_POSITION_TYPES(h, 0, c, 1, less);
    TEST_ALL_POSITION_TYPES(h, 0, c, 2, less);

    EXPECT_EQUIVALENT(makePositionBefore(document), makePositionBefore(document));
    EXPECT_EQUIVALENT(makePositionAfter(document), makePositionAfter(document));
    EXPECT_LESS(makePositionBefore(document), makePositionAfter(document));

    TEST_ALL_POSITION_TYPES_B(makePositionBefore(document), document, 0, less);
    TEST_ALL_POSITION_TYPES_B(makePositionBefore(document), body, 0, less);
    TEST_ALL_POSITION_TYPES_B(makePositionBefore(document), document, 1, less);
    TEST_ALL_POSITION_TYPES_B(makePositionBefore(document), body, 1, less);
    TEST_ALL_POSITION_TYPES_B(makePositionBefore(document), h, 0, less);

    TEST_ALL_POSITION_TYPES_B(makePositionAfter(document), document, 0, greater);
    TEST_ALL_POSITION_TYPES_B(makePositionAfter(document), document, 1, greater);
    TEST_ALL_POSITION_TYPES_B(makePositionAfter(document), body, 0, greater);
    TEST_ALL_POSITION_TYPES_B(makePositionAfter(document), body, 1, greater);
    TEST_ALL_POSITION_TYPES_B(makePositionAfter(document), h, 0, greater);

    // FIXME: Add tests that cover shadow trees.
}

// Disabled this test since isPointInRange is now a template function named contains.
// Keeping the code around to use as a reference when rewriting in JavaScript
#if 0

TEST(DocumentOrder, IsPointInRange)
{
    auto document = createDocument();
    auto& documentElement = *document->documentElement();
    auto& body = *document->body();

    EXPECT_TRUE(isPointInRange(makeRangeSelectingNodeContents(document), makeBoundaryPoint(document, 0)));
    EXPECT_TRUE(isPointInRange(makeRangeSelectingNodeContents(document), makeBoundaryPoint(document, 1)));
    EXPECT_FALSE(isPointInRange(makeRangeSelectingNodeContents(document), makeBoundaryPoint(document, 2)));
    EXPECT_TRUE(isPointInRange(makeRangeSelectingNodeContents(document), makeBoundaryPoint(body, 0)));
    EXPECT_TRUE(isPointInRange(makeRangeSelectingNodeContents(document), makeBoundaryPoint(body, 1)));
    EXPECT_TRUE(isPointInRange(makeRangeSelectingNodeContents(document), makeBoundaryPoint(body, 2)));
    EXPECT_FALSE(isPointInRange(makeSimpleRange(makeBoundaryPoint(document, 0), makeBoundaryPoint(documentElement, 0)), makeBoundaryPoint(body, 0)));
    EXPECT_TRUE(isPointInRange(makeSimpleRange(makeBoundaryPoint(document, 0), makeBoundaryPoint(body, 0)), makeBoundaryPoint(body, 0)));

    auto a = HTMLDivElement::create(document);
    EXPECT_FALSE(isPointInRange(makeRangeSelectingNodeContents(document), makeBoundaryPoint(a, 0)));
    EXPECT_FALSE(isPointInRange(makeRangeSelectingNodeContents(body), makeBoundaryPoint(a, 0)));

    body.appendChild(a);
    EXPECT_TRUE(isPointInRange(makeRangeSelectingNodeContents(document), makeBoundaryPoint(a, 0)));
    EXPECT_TRUE(isPointInRange(makeSimpleRange(makeBoundaryPoint(document, 0), makeBoundaryPoint(a, 0)), makeBoundaryPoint(a, 0)));
    EXPECT_FALSE(isPointInRange(makeSimpleRange(makeBoundaryPoint(document, 0), makeBoundaryPoint(body, 0)), makeBoundaryPoint(a, 0)));
    EXPECT_TRUE(isPointInRange(makeSimpleRange(makeBoundaryPoint(document, 0), makeBoundaryPoint(body, 1)), makeBoundaryPoint(a, 0)));
    EXPECT_FALSE(isPointInRange(makeSimpleRange(makeBoundaryPoint(body, 1), makeBoundaryPoint(document, 1)), makeBoundaryPoint(a, 0)));
    EXPECT_TRUE(isPointInRange(makeSimpleRange(makeBoundaryPoint(body, 0), makeBoundaryPoint(document, 1)), makeBoundaryPoint(a, 0)));

    auto b = HTMLDivElement::create(document);
    body.appendChild(b);

    auto c = HTMLDivElement::create(document);
    b->appendChild(c);

    auto d = HTMLDivElement::create(document);
    a->appendChild(d);

    auto e = HTMLDivElement::create(document);
    auto f = HTMLDivElement::create(document);
    e->appendChild(f);
    EXPECT_FALSE(isPointInRange(makeRangeSelectingNodeContents(body), makeBoundaryPoint(f, 0)));

    auto g = HTMLTextAreaElement::create(document);
    auto& h = *g->innerTextElement();
    c->appendChild(g);
    EXPECT_TRUE(isPointInRange(makeRangeSelectingNodeContents(body), makeBoundaryPoint(h, 0)));

    // FIXME: Add tests that cover shadow trees.
}

#endif

TEST(DocumentOrder, RangeContainsRange)
{
    auto document = createDocument();
    auto& documentElement = *document->documentElement();
    auto& body = *document->body();

    EXPECT_CONTAINS_SELF(makeRangeSelectingNodeContents(document));
    EXPECT_CONTAINS_SELF(makeRangeSelectingNodeContents(documentElement));
    EXPECT_CONTAINS_SELF(makeRangeSelectingNodeContents(body));

    EXPECT_NESTED(makeRangeSelectingNodeContents(document), makeRangeSelectingNodeContents(documentElement));
    EXPECT_NESTED(makeRangeSelectingNodeContents(document), makeRangeSelectingNodeContents(body));
    EXPECT_NESTED(makeRangeSelectingNodeContents(documentElement), makeRangeSelectingNodeContents(body));

    EXPECT_NESTED(makeRangeSelectingNodeContents(document), makeRangeSelectingNodeContents(documentElement));

    EXPECT_NESTED(makeRangeSelectingNodeContents(documentElement), makeRangeSelectingNodeContents(body));

    EXPECT_NESTED(makeRangeSelectingNodeContents(document), makeSimpleRange(makeBoundaryPoint(document, 0)));
    EXPECT_NESTED(makeRangeSelectingNodeContents(document), makeSimpleRange(makeBoundaryPoint(document, 1)));
    EXPECT_NOT_CONTAINED(makeRangeSelectingNodeContents(document), makeSimpleRange(makeBoundaryPoint(document, 2)));

    EXPECT_NESTED(makeSimpleRange(makeBoundaryPoint(document, 0), makeBoundaryPoint(document, 2)), makeRangeSelectingNodeContents(document));
    EXPECT_NOT_CONTAINED(makeRangeSelectingNodeContents(document), makeSimpleRange(makeBoundaryPoint(document, 1), makeBoundaryPoint(document, 2)));

    EXPECT_NOT_CONTAINED(makeSimpleRange(makeBoundaryPoint(document, 0), makeBoundaryPoint(documentElement, 0)), makeRangeSelectingNodeContents(body));
    EXPECT_NESTED(makeSimpleRange(makeBoundaryPoint(document, 0), makeBoundaryPoint(body, 0)), makeSimpleRange(makeBoundaryPoint(body, 0)));

    auto a = HTMLDivElement::create(document);
    EXPECT_NOT_CONTAINED(makeRangeSelectingNodeContents(document), makeRangeSelectingNodeContents(a));

    body.appendChild(a);

    auto b = HTMLDivElement::create(document);
    body.appendChild(b);

    auto c = HTMLDivElement::create(document);
    b->appendChild(c);

    auto d = HTMLDivElement::create(document);
    a->appendChild(d);

    auto e = HTMLDivElement::create(document);
    auto f = HTMLDivElement::create(document);
    e->appendChild(f);
    EXPECT_NOT_CONTAINED(makeRangeSelectingNodeContents(body), makeRangeSelectingNodeContents(f));

    auto g = HTMLTextAreaElement::create(document);
    auto& h = *g->innerTextElement();
    c->appendChild(g);
    EXPECT_NESTED(makeRangeSelectingNodeContents(body), makeRangeSelectingNodeContents(h));

    // FIXME: Add tests that cover shadow trees.
}

TEST(DocumentOrder, RangeIntersectsRange)
{
    auto document = createDocument();
    auto& documentElement = *document->documentElement();
    auto& body = *document->body();

    EXPECT_INTERSECTS_SELF(makeRangeSelectingNodeContents(document));
    EXPECT_INTERSECTS_SELF(makeRangeSelectingNodeContents(documentElement));
    EXPECT_INTERSECTS_SELF(makeRangeSelectingNodeContents(body));

    EXPECT_INTERSECTS_BOTH_WAYS(makeRangeSelectingNodeContents(document), makeRangeSelectingNodeContents(documentElement));
    EXPECT_INTERSECTS_BOTH_WAYS(makeRangeSelectingNodeContents(document), makeRangeSelectingNodeContents(body));
    EXPECT_INTERSECTS_BOTH_WAYS(makeRangeSelectingNodeContents(documentElement), makeRangeSelectingNodeContents(body));

    EXPECT_INTERSECTS_BOTH_WAYS(makeRangeSelectingNodeContents(document), makeRangeSelectingNodeContents(documentElement));

    EXPECT_INTERSECTS_BOTH_WAYS(makeRangeSelectingNodeContents(documentElement), makeRangeSelectingNodeContents(body));

    EXPECT_INTERSECTS_BOTH_WAYS(makeRangeSelectingNodeContents(document), makeSimpleRange(makeBoundaryPoint(document, 0)));
    EXPECT_INTERSECTS_BOTH_WAYS(makeRangeSelectingNodeContents(document), makeSimpleRange(makeBoundaryPoint(document, 1)));
    EXPECT_NOT_INTERSECTING_BOTH_WAYS(makeRangeSelectingNodeContents(document), makeSimpleRange(makeBoundaryPoint(document, 2)));

    EXPECT_INTERSECTS_BOTH_WAYS(makeSimpleRange(makeBoundaryPoint(document, 0), makeBoundaryPoint(document, 2)), makeRangeSelectingNodeContents(document));
    EXPECT_INTERSECTS_BOTH_WAYS(makeRangeSelectingNodeContents(document), makeSimpleRange(makeBoundaryPoint(document, 1), makeBoundaryPoint(document, 2)));

    EXPECT_NOT_INTERSECTING_BOTH_WAYS(makeSimpleRange(makeBoundaryPoint(document, 0), makeBoundaryPoint(documentElement, 0)), makeRangeSelectingNodeContents(body));
    EXPECT_INTERSECTS_BOTH_WAYS(makeSimpleRange(makeBoundaryPoint(document, 0), makeBoundaryPoint(body, 0)), makeSimpleRange(makeBoundaryPoint(body, 0)));

    auto a = HTMLDivElement::create(document);
    EXPECT_NOT_INTERSECTING_BOTH_WAYS(makeRangeSelectingNodeContents(document), makeRangeSelectingNodeContents(a));

    body.appendChild(a);

    auto b = HTMLDivElement::create(document);
    body.appendChild(b);

    auto c = HTMLDivElement::create(document);
    b->appendChild(c);

    auto d = HTMLDivElement::create(document);
    a->appendChild(d);

    auto e = HTMLDivElement::create(document);
    auto f = HTMLDivElement::create(document);
    e->appendChild(f);
    EXPECT_NOT_INTERSECTING_BOTH_WAYS(makeRangeSelectingNodeContents(body), makeRangeSelectingNodeContents(f));

    auto g = HTMLTextAreaElement::create(document);
    auto& h = *g->innerTextElement();
    c->appendChild(g);
    EXPECT_INTERSECTS_BOTH_WAYS(makeRangeSelectingNodeContents(body), makeRangeSelectingNodeContents(h));

    // FIXME: Add tests that cover shadow trees.
}

TEST(DocumentOrder, RangeContainsNode)
{
    auto document = createDocument();
    auto& documentElement = *document->documentElement();
    auto& body = *document->body();

    EXPECT_TRUE(contains(makeRangeSelectingNodeContents(document), documentElement));
    EXPECT_TRUE(contains(makeRangeSelectingNodeContents(document), body));
    EXPECT_TRUE(contains(makeRangeSelectingNodeContents(documentElement), body));

    EXPECT_FALSE(contains(makeRangeSelectingNodeContents(document), document));
    EXPECT_FALSE(contains(makeSimpleRange(makeBoundaryPoint(document, 0), makeBoundaryPoint(document, 2)), document));
    EXPECT_FALSE(contains(makeSimpleRange(makeBoundaryPoint(document, 0), makeBoundaryPoint(documentElement, 0)), body));
    EXPECT_FALSE(contains(makeSimpleRange(makeBoundaryPoint(document, 0), makeBoundaryPoint(body, 0)), body));
    EXPECT_FALSE(contains(makeSimpleRange(makeBoundaryPoint(document, 0), makeBoundaryPoint(body, 1)), body));
    EXPECT_TRUE(contains(makeSimpleRange(makeBoundaryPoint(document, 0), makeBoundaryPoint(documentElement, 1)), body));

    auto a = HTMLDivElement::create(document);
    EXPECT_FALSE(contains(makeRangeSelectingNodeContents(document), a));

    body.appendChild(a);

    auto b = HTMLDivElement::create(document);
    body.appendChild(b);

    auto c = HTMLDivElement::create(document);
    b->appendChild(c);

    auto d = HTMLDivElement::create(document);
    a->appendChild(d);

    auto e = HTMLDivElement::create(document);
    auto f = HTMLDivElement::create(document);
    e->appendChild(f);
    EXPECT_FALSE(contains(makeRangeSelectingNodeContents(body), f));

    auto g = HTMLTextAreaElement::create(document);
    auto& h = *g->innerTextElement();
    c->appendChild(g);
    EXPECT_TRUE(contains(makeRangeSelectingNodeContents(body), h));

    // FIXME: Add tests that cover shadow trees.
}

TEST(DocumentOrder, RangeIntersectsNode)
{
    auto document = createDocument();
    auto& documentElement = *document->documentElement();
    auto& body = *document->body();

    EXPECT_TRUE(intersects(makeRangeSelectingNodeContents(document), documentElement));
    EXPECT_TRUE(intersects(makeRangeSelectingNodeContents(document), body));
    EXPECT_TRUE(intersects(makeRangeSelectingNodeContents(documentElement), body));

    EXPECT_TRUE(intersects(makeRangeSelectingNodeContents(document), document));
    EXPECT_TRUE(intersects(makeSimpleRange(makeBoundaryPoint(document, 0), makeBoundaryPoint(document, 2)), document));
    EXPECT_FALSE(intersects(makeSimpleRange(makeBoundaryPoint(document, 0), makeBoundaryPoint(documentElement, 0)), body));
    EXPECT_FALSE(intersects(makeSimpleRange(makeBoundaryPoint(documentElement, 1), makeBoundaryPoint(document, 1)), body));
    EXPECT_TRUE(intersects(makeSimpleRange(makeBoundaryPoint(document, 0), makeBoundaryPoint(body, 0)), body));
    EXPECT_TRUE(intersects(makeSimpleRange(makeBoundaryPoint(document, 0), makeBoundaryPoint(body, 1)), body));
    EXPECT_TRUE(intersects(makeSimpleRange(makeBoundaryPoint(document, 0), makeBoundaryPoint(documentElement, 1)), body));

    auto a = HTMLDivElement::create(document);
    EXPECT_FALSE(intersects(makeRangeSelectingNodeContents(document), a));

    body.appendChild(a);

    auto b = HTMLDivElement::create(document);
    body.appendChild(b);

    auto c = HTMLDivElement::create(document);
    b->appendChild(c);

    auto d = HTMLDivElement::create(document);
    a->appendChild(d);

    auto e = HTMLDivElement::create(document);
    auto f = HTMLDivElement::create(document);
    e->appendChild(f);
    EXPECT_FALSE(intersects(makeRangeSelectingNodeContents(body), e));
    EXPECT_TRUE(intersects(makeRangeSelectingNodeContents(f), e));
    EXPECT_FALSE(intersects(makeRangeSelectingNodeContents(body), f));

    auto g = HTMLTextAreaElement::create(document);
    auto& h = *g->innerTextElement();
    c->appendChild(g);
    EXPECT_TRUE(intersects(makeRangeSelectingNodeContents(body), h));

    // FIXME: Add tests that cover shadow trees.
}

} // namespace TestWebKitAPI
