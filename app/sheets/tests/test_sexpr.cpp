#include <doctest/doctest.h>

#include <ostream>  // MSVC: doctest streams std::string_view and needs the full ostream type

#include <string>

#include "fulmetry/sheets/SExpr.h"

using namespace fulmetry::sheets;

TEST_CASE("sexpr: lists, atoms and strings") {
    auto r = parseSExpr("(kicad_sch (version 20231120)\n  (label \"a \\\"b\\\"\\n\" (at 1.5 -2 90)) hide)");
    REQUIRE(r.ok());
    const SNode& root = r.value();
    CHECK(root.head() == "kicad_sch");
    CHECK(root.find("version")->number(1) == 20231120.0);
    const SNode* label = root.find("label");
    REQUIRE(label);
    CHECK(label->line == 2);
    CHECK(label->at(1)->kind == SNode::Kind::String);
    CHECK(*label->str(1) == "a \"b\"\n");
    const SNode* at = label->find("at");
    CHECK(at->number(1) == 1.5);
    CHECK(at->number(2) == -2.0);
    CHECK(root.hasFlag("hide"));
    CHECK_FALSE(root.hasFlag("show"));
    CHECK(root.findAll("label").size() == 1);
    CHECK(root.at(99) == nullptr);
    CHECK_FALSE(root.str(99).has_value());
}

TEST_CASE("sexpr: (flag yes|no) form") {
    auto r = parseSExpr("(effects (font (size 1 1)) (hide yes))");
    REQUIRE(r.ok());
    CHECK(r->hasFlag("hide"));
    CHECK_FALSE(parseSExpr("(effects (hide no))")->hasFlag("hide"));
}

TEST_CASE("sexpr: numbers are locale independent and strict") {
    CHECK(parseNumber("12.7") == 12.7);
    CHECK(parseNumber("-0.254") == -0.254);
    CHECK(parseNumber("+3") == 3.0);
    CHECK(parseNumber("1e-3") == 0.001);
    CHECK(parseNumber(".5") == 0.5);
    CHECK_FALSE(parseNumber("").has_value());
    CHECK_FALSE(parseNumber("-").has_value());
    CHECK_FALSE(parseNumber("1,5").has_value());
    CHECK_FALSE(parseNumber("12abc").has_value());
    CHECK_FALSE(parseNumber("nan").has_value());
    CHECK_FALSE(parseNumber("1e999999").has_value());
    CHECK_FALSE(parseNumber("1e").has_value());
}

TEST_CASE("sexpr: malformed input is an error, never a crash") {
    const char* bad[] = {
        "",                 // empty
        "   \n ",           // only whitespace
        "(a (b c)",         // unclosed
        "(a))",             // extra close
        ")",                // stray close
        "(a \"unterminated)",
        "(a \"x\\",         // escape at end of input
        "atom",             // not a list
        "(a) (b)",          // two top-level lists
        "(a) junk",         // trailing atom
    };
    for (const char* text : bad) {
        CAPTURE(text);
        auto r = parseSExpr(text);
        CHECK_FALSE(r.ok());
        if (!r.ok()) CHECK(r.error().code == "parse_error");
    }
}

TEST_CASE("sexpr: nesting and size limits") {
    std::string deep(100000, '(');
    auto r = parseSExpr(deep);
    REQUIRE_FALSE(r.ok());
    CHECK(r.error().message.find("too deep") != std::string::npos);

    std::string ok = std::string(200, '(') + std::string(200, ')');
    CHECK(parseSExpr(ok).ok());

    SExprLimits small;
    small.maxBytes = 4;
    CHECK(parseSExpr("(abcdef)", small).error().code == "too_large");
}
