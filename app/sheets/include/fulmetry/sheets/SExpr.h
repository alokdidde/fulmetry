// A small S-expression reader for KiCad files. Owner: S6 Sheets.
//
// Grammar: list = '(' item* ')'; item = list | "quoted string" | bare-atom.
// Strings support \" \\ \n \t \r escapes. Input never crashes the reader: malformed text, too-deep
// nesting and oversized input all come back as an Error.
#pragma once
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "fulmetry/core/Result.h"

namespace fulmetry::sheets {

struct SNode {
    enum class Kind { List, Atom, String };

    Kind kind = Kind::List;
    std::string text;          // Atom or String contents
    std::vector<SNode> items;  // List contents
    int line = 0;              // 1-based line where the node starts

    bool isList() const { return kind == Kind::List; }
    bool isAtom() const { return kind != Kind::List; }  // bare atom or quoted string

    // The first item's text when this is a list that starts with a bare atom, else "".
    std::string_view head() const;
    // The i-th item, or nullptr.
    const SNode* at(std::size_t i) const { return i < items.size() ? &items[i] : nullptr; }
    // The i-th item's text when it is an atom or string.
    std::optional<std::string_view> str(std::size_t i) const;
    // The i-th item as a number (locale independent).
    std::optional<double> number(std::size_t i) const;
    // First child list whose head is `name`, or nullptr.
    const SNode* find(std::string_view name) const;
    // Every child list whose head is `name`.
    std::vector<const SNode*> findAll(std::string_view name) const;
    // True if a bare atom `flag` appears among the items (e.g. `hide`), or a child list `(flag yes)`.
    bool hasFlag(std::string_view flag) const;
};

struct SExprLimits {
    std::size_t maxDepth = 256;
    std::size_t maxBytes = 64u * 1024u * 1024u;
};

// Parses exactly one top-level list (surrounding whitespace allowed).
Result<SNode> parseSExpr(std::string_view text, const SExprLimits& limits = {});

// Parses a decimal number like "-12.7", "3", "1e-3". No locale, no inf/nan.
std::optional<double> parseNumber(std::string_view s);

}  // namespace fulmetry::sheets
