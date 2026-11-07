#include "fulmetry/sheets/SExpr.h"

#include <cmath>

namespace fulmetry::sheets {
namespace {

Error parseError(int line, const std::string& what) {
    return Error{"parse_error", "line " + std::to_string(line) + ": " + what};
}

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }
bool isDelimiter(char c) { return isSpace(c) || c == '(' || c == ')' || c == '"'; }

}  // namespace

std::string_view SNode::head() const {
    if (!isList() || items.empty() || items[0].kind != Kind::Atom) return {};
    return items[0].text;
}

std::optional<std::string_view> SNode::str(std::size_t i) const {
    const SNode* n = at(i);
    if (!n || !n->isAtom()) return std::nullopt;
    return std::string_view(n->text);
}

std::optional<double> SNode::number(std::size_t i) const {
    auto t = str(i);
    if (!t) return std::nullopt;
    return parseNumber(*t);
}

const SNode* SNode::find(std::string_view name) const {
    for (const auto& c : items)
        if (c.head() == name) return &c;
    return nullptr;
}

std::vector<const SNode*> SNode::findAll(std::string_view name) const {
    std::vector<const SNode*> out;
    for (const auto& c : items)
        if (c.head() == name) out.push_back(&c);
    return out;
}

bool SNode::hasFlag(std::string_view flag) const {
    for (std::size_t i = 1; i < items.size(); ++i) {
        const auto& c = items[i];
        if (c.kind == Kind::Atom && c.text == flag) return true;
        if (c.head() == flag) {
            auto v = c.str(1);
            return !v || *v == "yes" || *v == "true";
        }
    }
    return false;
}

std::optional<double> parseNumber(std::string_view s) {
    std::size_t i = 0;
    bool neg = false;
    if (i < s.size() && (s[i] == '+' || s[i] == '-')) neg = s[i++] == '-';
    double mant = 0;
    int digits = 0, fracDigits = 0;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
        mant = mant * 10 + (s[i++] - '0');
        ++digits;
    }
    if (i < s.size() && s[i] == '.') {
        ++i;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
            mant = mant * 10 + (s[i++] - '0');
            ++digits;
            ++fracDigits;
        }
    }
    if (digits == 0 || digits > 30) return std::nullopt;
    int exp = 0;
    if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
        ++i;
        bool eneg = false;
        if (i < s.size() && (s[i] == '+' || s[i] == '-')) eneg = s[i++] == '-';
        int edigits = 0;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
            if (exp < 1000) exp = exp * 10 + (s[i] - '0');
            ++i;
            ++edigits;
        }
        if (edigits == 0) return std::nullopt;
        if (eneg) exp = -exp;
    }
    if (i != s.size()) return std::nullopt;
    int e10 = exp - fracDigits;
    double v = e10 < 0 ? mant / std::pow(10.0, -e10) : mant * std::pow(10.0, e10);
    if (!std::isfinite(v)) return std::nullopt;
    return neg ? -v : v;
}

Result<SNode> parseSExpr(std::string_view src, const SExprLimits& limits) {
    if (src.size() > limits.maxBytes) return Error{"too_large", "input exceeds the size limit"};

    std::vector<SNode> stack;  // open lists, innermost last
    std::optional<SNode> root;
    int line = 1;
    std::size_t i = 0;

    auto append = [&](SNode node) -> std::optional<Error> {
        if (stack.empty()) {
            if (root) return parseError(node.line, "more than one top-level expression");
            if (!node.isList()) return parseError(node.line, "expected '(' at top level");
            root = std::move(node);
            return std::nullopt;
        }
        stack.back().items.push_back(std::move(node));
        return std::nullopt;
    };

    while (i < src.size()) {
        char c = src[i];
        if (c == '\n') { ++line; ++i; continue; }
        if (isSpace(c)) { ++i; continue; }
        if (c == '(') {
            if (root) return parseError(line, "more than one top-level expression");
            if (stack.size() >= limits.maxDepth) return parseError(line, "nesting too deep");
            SNode n;
            n.kind = SNode::Kind::List;
            n.line = line;
            stack.push_back(std::move(n));
            ++i;
            continue;
        }
        if (c == ')') {
            if (stack.empty()) return parseError(line, "unexpected ')'");
            SNode done = std::move(stack.back());
            stack.pop_back();
            if (auto e = append(std::move(done))) return *e;
            ++i;
            continue;
        }
        if (c == '"') {
            SNode n;
            n.kind = SNode::Kind::String;
            n.line = line;
            ++i;
            bool closed = false;
            while (i < src.size()) {
                char d = src[i++];
                if (d == '"') { closed = true; break; }
                if (d == '\n') ++line;
                if (d == '\\') {
                    if (i >= src.size()) break;
                    char e = src[i++];
                    switch (e) {
                        case 'n': n.text += '\n'; break;
                        case 't': n.text += '\t'; break;
                        case 'r': n.text += '\r'; break;
                        case '\n': ++line; n.text += '\n'; break;
                        default: n.text += e; break;  // \" \\ and anything else literally
                    }
                    continue;
                }
                n.text += d;
            }
            if (!closed) return parseError(n.line, "unterminated string");
            if (auto e = append(std::move(n))) return *e;
            continue;
        }
        // Bare atom.
        SNode n;
        n.kind = SNode::Kind::Atom;
        n.line = line;
        std::size_t start = i;
        while (i < src.size() && !isDelimiter(src[i])) ++i;
        n.text.assign(src.substr(start, i - start));
        if (auto e = append(std::move(n))) return *e;
    }

    if (!stack.empty()) return parseError(stack.back().line, "unclosed '('");
    if (!root) return Error{"parse_error", "empty input"};
    return std::move(*root);
}

}  // namespace fulmetry::sheets
