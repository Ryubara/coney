// SPDX-License-Identifier: GPL-3.0-or-later
#include "debug/lua_console.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <format>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <utility>

#include "debug/native_caller.h"

namespace coney::debug {

namespace {

using script::Value;
using Values = std::vector<Value>;

// One token of a console line.
struct Token {
    enum class Kind : std::uint8_t { Name, Number, String, Symbol, End };
    Kind kind = Kind::End;
    std::string text; // the name, the symbol, or the string's contents
    double number = 0.0;
    std::size_t column = 0; // 1-based, for errors
};

// A syntax error at `column`.
std::unexpected<Error> syntaxError(std::size_t column, std::string_view what) {
    return fail(ErrorCode::Invalid, std::format("column {}: {}", column, what));
}

// Cuts `text` into tokens. The symbols are Lua's that the console's grammar uses.
std::expected<std::vector<Token>, Error> tokenize(std::string_view text) {
    static constexpr std::array<std::string_view, 21> kSymbols{
        "==", "~=", "<=", ">=", "..", "=", "<", ">", "+", "-", "*", "/", "(", ")", "{", "}", "[", "]", ".", ",", ":"};
    std::vector<Token> tokens;
    std::size_t i = 0;
    while (i < text.size()) {
        const char c = text[i];
        Token token;
        token.column = i + 1;
        if (c == ' ' || c == '\t' || c == '\r') {
            ++i;
            continue;
        }
        if (c == '\n' || c == ';') {
            token.kind = Token::Kind::Symbol;
            token.text = ";";
            tokens.push_back(std::move(token));
            ++i;
            continue;
        }
        if (c == '-' && i + 1 < text.size() && text[i + 1] == '-') {
            // A comment runs to the end of the line.
            while (i < text.size() && text[i] != '\n') {
                ++i;
            }
            continue;
        }
        if (std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_') {
            const std::size_t start = i;
            while (i < text.size() && (std::isalnum(static_cast<unsigned char>(text[i])) != 0 || text[i] == '_')) {
                ++i;
            }
            token.kind = Token::Kind::Name;
            token.text = std::string(text.substr(start, i - start));
        } else if (std::isdigit(static_cast<unsigned char>(c)) != 0 ||
                   (c == '.' && i + 1 < text.size() && std::isdigit(static_cast<unsigned char>(text[i + 1])) != 0)) {
            const std::size_t start = i;
            while (i < text.size() &&
                   (std::isalnum(static_cast<unsigned char>(text[i])) != 0 || text[i] == '.' ||
                    ((text[i] == '-' || text[i] == '+') && (text[i - 1] == 'e' || text[i - 1] == 'E')))) {
                ++i;
            }
            const auto number = script::parseLuaNumber(text.substr(start, i - start));
            if (!number) {
                return syntaxError(token.column, "malformed number");
            }
            token.kind = Token::Kind::Number;
            token.number = *number;
        } else if (c == '"' || c == '\'') {
            ++i;
            token.kind = Token::Kind::String;
            bool closed = false;
            while (i < text.size()) {
                const char s = text[i++];
                if (s == c) {
                    closed = true;
                    break;
                }
                if (s == '\\' && i < text.size()) {
                    const char e = text[i++];
                    token.text.push_back(e == 'n' ? '\n' : e == 't' ? '\t' : e);
                } else {
                    token.text.push_back(s);
                }
            }
            if (!closed) {
                return syntaxError(token.column, "unfinished string");
            }
        } else {
            const auto symbol =
                std::ranges::find_if(kSymbols, [&](std::string_view s) { return text.substr(i).starts_with(s); });
            if (symbol == kSymbols.end()) {
                return syntaxError(token.column, std::format("unexpected '{}'", c));
            }
            token.kind = Token::Kind::Symbol;
            token.text = std::string(*symbol);
            i += symbol->size();
        }
        tokens.push_back(std::move(token));
    }
    Token end;
    end.column = text.size() + 1;
    tokens.push_back(end);
    return tokens;
}

// Lua's truth: everything but nil is true (Lua 4.0 has no booleans).
bool truthy(const Value& value) { return !value.isNil(); }

// A value as a number for arithmetic: a number, or a string that reads as one.
std::optional<double> toNumber(const Value& value) {
    if (const auto number = value.number()) {
        return number;
    }
    if (const auto text = value.string()) {
        return script::parseLuaNumber(*text);
    }
    return std::nullopt;
}

// A value as a string for `..`: a string, or a number in Lua 4.0's `%.14g`.
std::optional<std::string> toText(const Value& value) {
    if (const auto text = value.string()) {
        return std::string(*text);
    }
    if (const auto number = value.number()) {
        return std::format("{:.14g}", *number);
    }
    return std::nullopt;
}

// The type name Lua's errors use.
std::string_view typeName(const Value& value) {
    static constexpr std::array<std::string_view, 5> kNames{"nil", "number", "string", "table", "function"};
    return kNames.at(static_cast<std::size_t>(value.type()));
}

// Where an assignment stores: a global, or a field of a table.
struct Target {
    std::string global;
    std::shared_ptr<script::Table> table;
    Value key;
};

// A recursive-descent parser that evaluates as it parses. With m_skip set it only parses (the untaken side of
// `and`/`or`, and the look-ahead that tells an assignment from an expression), so nothing runs twice.
class Evaluator {
  public:
    Evaluator(script::LuaVm& vm, std::vector<Token> tokens) : m_vm(vm), m_tokens(std::move(tokens)) {}

    // Runs every statement; returns the printed lines.
    std::expected<std::vector<std::string>, Error> chunk() {
        std::vector<std::string> printed;
        while (peek().kind != Token::Kind::End) {
            if (accept(";")) {
                continue;
            }
            auto lines = statement();
            if (!lines) {
                return std::unexpected(lines.error());
            }
            printed.insert(printed.end(), lines->begin(), lines->end());
            if (peek().kind != Token::Kind::End && !accept(";")) {
                return syntaxError(peek().column, "expected the end of the statement");
            }
        }
        return printed;
    }

    // One expression and nothing after it.
    std::expected<Values, Error> wholeExpression() {
        auto values = expression();
        if (values && peek().kind != Token::Kind::End) {
            return syntaxError(peek().column, "expected the end of the expression");
        }
        return values;
    }

  private:
    // ---- Tokens ----

    // The next token.
    [[nodiscard]] const Token& peek() const { return m_tokens[m_at]; }
    // Whether the next token is the symbol or keyword `text`.
    [[nodiscard]] bool check(std::string_view text) const {
        return (peek().kind == Token::Kind::Symbol || peek().kind == Token::Kind::Name) && peek().text == text;
    }
    // Takes the next token when it is `text`.
    bool accept(std::string_view text) {
        if (!check(text)) {
            return false;
        }
        ++m_at;
        return true;
    }
    // Takes `text` or fails.
    std::expected<void, Error> expect(std::string_view text) {
        if (!accept(text)) {
            return syntaxError(peek().column, std::format("expected '{}'", text));
        }
        return {};
    }

    // ---- Statements ----

    // An assignment or an expression statement.
    std::expected<std::vector<std::string>, Error> statement() {
        // `= expr` prints the expression.
        if (accept("=")) {
            return printed(expressionList());
        }
        // Look ahead without running anything: a target followed by `=` is an assignment.
        const std::size_t start = m_at;
        bool isAssignment = false;
        if (peek().kind == Token::Kind::Name) {
            m_skip = true;
            const auto target = suffixed();
            m_skip = false;
            isAssignment = target.has_value() && check("=");
        }
        m_at = start;
        if (!isAssignment) {
            return printed(expressionList());
        }
        auto target = assignmentTarget();
        if (!target) {
            return std::unexpected(target.error());
        }
        if (auto equals = expect("="); !equals) {
            return std::unexpected(equals.error());
        }
        auto values = expression();
        if (!values) {
            return std::unexpected(values.error());
        }
        const Value value = values->empty() ? Value() : values->front();
        if (target->table) {
            if (auto set = target->table->set(target->key, value); !set) {
                return std::unexpected(set.error());
            }
        } else {
            m_vm.setGlobal(target->global, value);
        }
        return std::vector<std::string>{};
    }

    // The values of an expression statement as one printed line; nothing for no values.
    static std::expected<std::vector<std::string>, Error> printed(std::expected<Values, Error> values) {
        if (!values) {
            return std::unexpected(values.error());
        }
        if (values->empty()) {
            return std::vector<std::string>{};
        }
        std::string line = "=";
        for (std::size_t i = 0; i < values->size(); ++i) {
            line += (i == 0 ? " " : ", ") + formatValue((*values)[i]);
        }
        return std::vector<std::string>{line};
    }

    // The target of an assignment: a name, then fields; the last field is where the value goes.
    std::expected<Target, Error> assignmentTarget() {
        Target target;
        target.global = peek().text;
        ++m_at;
        Value current = m_vm.global(target.global);
        while (check(".") || check("[")) {
            auto key = fieldKey();
            if (!key) {
                return std::unexpected(key.error());
            }
            if (check(".") || check("[")) {
                auto next = index(current, *key);
                if (!next) {
                    return std::unexpected(next.error());
                }
                current = *next;
                continue;
            }
            if (!current.table()) {
                return fail(ErrorCode::Invalid, std::format("attempt to index a {} value", typeName(current)));
            }
            target.table = current.table();
            target.key = *key;
        }
        return target;
    }

    // ---- Expressions, lowest precedence first ----

    // `a, b, c`: the first value of each expression but the last, and all of the last's, as Lua passes them.
    std::expected<Values, Error> expressionList() {
        Values all;
        while (true) {
            auto values = expression();
            if (!values) {
                return values;
            }
            if (!accept(",")) {
                all.insert(all.end(), values->begin(), values->end());
                return all;
            }
            all.push_back(first(*values));
        }
    }

    // An expression's values: all of a call's results, one value otherwise.
    std::expected<Values, Error> expression() { return orExpression(); }

    // a or b.
    std::expected<Values, Error> orExpression() {
        auto left = andExpression();
        while (left && accept("or")) {
            const bool taken = !truthy(first(*left));
            auto right = withSkip(!taken, [this] { return andExpression(); });
            if (!right) {
                return right;
            }
            if (taken) {
                left = std::move(right);
            } else {
                left = Values{first(*left)};
            }
        }
        return left;
    }

    // a and b.
    std::expected<Values, Error> andExpression() {
        auto left = comparison();
        while (left && accept("and")) {
            const bool taken = truthy(first(*left));
            auto right = withSkip(!taken, [this] { return comparison(); });
            if (!right) {
                return right;
            }
            if (taken) {
                left = std::move(right);
            } else {
                left = Values{first(*left)};
            }
        }
        return left;
    }

    // == ~= < > <= >=.
    std::expected<Values, Error> comparison() {
        auto left = concatenation();
        static constexpr std::array<std::string_view, 6> kOps{"==", "~=", "<", ">", "<=", ">="};
        while (left) {
            const auto op = std::ranges::find_if(kOps, [this](std::string_view s) { return check(s); });
            if (op == kOps.end()) {
                break;
            }
            ++m_at;
            auto right = concatenation();
            if (!right) {
                return right;
            }
            if (m_skip) {
                continue;
            }
            auto result = compare(*op, first(*left), first(*right));
            if (!result) {
                return std::unexpected(result.error());
            }
            left = Values{*result ? Value(1.0) : Value()};
        }
        return left;
    }

    // a .. b (right-associative; the order does not change a concatenation's result).
    std::expected<Values, Error> concatenation() {
        auto left = additive();
        if (!left || !accept("..")) {
            return left;
        }
        auto right = concatenation();
        if (!right || m_skip) {
            return right;
        }
        const auto a = toText(first(*left));
        const auto b = toText(first(*right));
        if (!a || !b) {
            return fail(ErrorCode::Invalid, "attempt to concatenate a non-string value");
        }
        return Values{Value(*a + *b)};
    }

    // + and -.
    std::expected<Values, Error> additive() {
        auto left = multiplicative();
        while (left && (check("+") || check("-"))) {
            const char op = peek().text[0];
            ++m_at;
            auto right = multiplicative();
            if (!right) {
                return right;
            }
            left = arithmetic(op, *left, *right);
        }
        return left;
    }

    // * and /.
    std::expected<Values, Error> multiplicative() {
        auto left = unary();
        while (left && (check("*") || check("/"))) {
            const char op = peek().text[0];
            ++m_at;
            auto right = unary();
            if (!right) {
                return right;
            }
            left = arithmetic(op, *left, *right);
        }
        return left;
    }

    // -x and not x.
    std::expected<Values, Error> unary() {
        if (accept("-")) {
            auto operand = unary();
            if (!operand || m_skip) {
                return operand;
            }
            const auto number = toNumber(first(*operand));
            if (!number) {
                return fail(ErrorCode::Invalid, "attempt to negate a non-number value");
            }
            return Values{Value(-*number)};
        }
        if (accept("not")) {
            auto operand = unary();
            if (!operand || m_skip) {
                return operand;
            }
            return Values{truthy(first(*operand)) ? Value() : Value(1.0)};
        }
        return simple();
    }

    // A literal, a table constructor or a suffixed expression.
    std::expected<Values, Error> simple() {
        const Token& token = peek();
        switch (token.kind) {
        case Token::Kind::Number:
            ++m_at;
            return Values{Value(token.number)};
        case Token::Kind::String:
            ++m_at;
            return Values{Value(token.text)};
        case Token::Kind::Name:
            if (token.text == "nil") {
                ++m_at;
                return Values{Value()};
            }
            return suffixed();
        case Token::Kind::Symbol:
            if (token.text == "{") {
                return tableConstructor();
            }
            if (token.text == "(") {
                return suffixed();
            }
            break;
        case Token::Kind::End:
            break;
        }
        return syntaxError(token.column, "expected an expression");
    }

    // A name or a parenthesised expression, then any fields, calls and method calls.
    std::expected<Values, Error> suffixed() {
        Values current;
        if (accept("(")) {
            auto inner = expression();
            if (!inner) {
                return inner;
            }
            if (auto close = expect(")"); !close) {
                return std::unexpected(close.error());
            }
            current = Values{first(*inner)};
        } else if (peek().kind == Token::Kind::Name) {
            current = Values{m_skip ? Value() : m_vm.global(peek().text)};
            ++m_at;
        } else {
            return syntaxError(peek().column, "expected a name");
        }
        while (true) {
            if (check(".") || check("[")) {
                auto key = fieldKey();
                if (!key) {
                    return std::unexpected(key.error());
                }
                if (m_skip) {
                    continue;
                }
                auto value = index(first(current), *key);
                if (!value) {
                    return std::unexpected(value.error());
                }
                current = Values{*value};
            } else if (check("(")) {
                auto called = call(first(current), {});
                if (!called) {
                    return called;
                }
                current = std::move(*called);
            } else if (accept(":")) {
                if (peek().kind != Token::Kind::Name) {
                    return syntaxError(peek().column, "expected a method name");
                }
                const Value self = first(current);
                const Value method = Value(peek().text);
                ++m_at;
                Value function;
                if (!m_skip) {
                    auto found = index(self, method);
                    if (!found) {
                        return std::unexpected(found.error());
                    }
                    function = *found;
                }
                auto called = call(function, self);
                if (!called) {
                    return called;
                }
                current = std::move(*called);
            } else {
                return current;
            }
        }
    }

    // `.name` or `[expr]`: the key.
    std::expected<Value, Error> fieldKey() {
        if (accept(".")) {
            if (peek().kind != Token::Kind::Name) {
                return syntaxError(peek().column, "expected a field name");
            }
            Value key(peek().text);
            ++m_at;
            return key;
        }
        if (auto open = expect("["); !open) {
            return std::unexpected(open.error());
        }
        auto key = expression();
        if (!key) {
            return std::unexpected(key.error());
        }
        if (auto close = expect("]"); !close) {
            return std::unexpected(close.error());
        }
        return first(*key);
    }

    // `(args)` after a function value: calls it through the VM with `self` first when it is a method call.
    std::expected<Values, Error> call(const Value& function, std::optional<Value> self) {
        if (auto open = expect("("); !open) {
            return std::unexpected(open.error());
        }
        Values args;
        if (self) {
            args.push_back(*self);
        }
        while (!check(")")) {
            auto values = expression();
            if (!values) {
                return values;
            }
            // Only the last argument passes all its values, as in Lua.
            if (check(",")) {
                args.push_back(first(*values));
                ++m_at;
            } else {
                args.insert(args.end(), values->begin(), values->end());
                break;
            }
        }
        if (auto close = expect(")"); !close) {
            return std::unexpected(close.error());
        }
        if (m_skip) {
            return Values{Value()};
        }
        if (function.function() == nullptr) {
            return fail(ErrorCode::Invalid, std::format("attempt to call a {} value", typeName(function)));
        }
        return m_vm.call(function, args);
    }

    // `{a, b, k = v, [e] = v}`.
    std::expected<Values, Error> tableConstructor() {
        (void)accept("{");
        auto table = std::make_shared<script::Table>();
        double next = 1;
        while (!check("}")) {
            Value key;
            if (peek().kind == Token::Kind::Name && m_tokens[m_at + 1].kind == Token::Kind::Symbol &&
                m_tokens[m_at + 1].text == "=") {
                key = Value(peek().text);
                m_at += 2;
            } else if (check("[")) {
                auto bracketed = fieldKey();
                if (!bracketed) {
                    return std::unexpected(bracketed.error());
                }
                key = *bracketed;
                if (auto equals = expect("="); !equals) {
                    return std::unexpected(equals.error());
                }
            } else {
                key = Value(next++);
            }
            auto value = expression();
            if (!value) {
                return value;
            }
            if (!m_skip) {
                if (auto set = table->set(key, first(*value)); !set) {
                    return std::unexpected(set.error());
                }
            }
            if (!accept(",") && !accept(";")) {
                break;
            }
        }
        if (auto close = expect("}"); !close) {
            return std::unexpected(close.error());
        }
        return Values{Value(std::move(table))};
    }

    // ---- Helpers ----

    // The first value, nil for none.
    static Value first(const Values& values) { return values.empty() ? Value() : values.front(); }

    // Runs `parse` with skipping set as asked, restoring it after.
    template <typename Parse> std::expected<Values, Error> withSkip(bool skip, Parse parse) {
        const bool before = m_skip;
        m_skip = m_skip || skip;
        auto result = parse();
        m_skip = before;
        return result;
    }

    // `container[key]`.
    static std::expected<Value, Error> index(const Value& container, const Value& key) {
        if (!container.table()) {
            return fail(ErrorCode::Invalid, std::format("attempt to index a {} value", typeName(container)));
        }
        return container.table()->get(key);
    }

    // One arithmetic operation on the first values.
    std::expected<Values, Error> arithmetic(char op, const Values& left, const Values& right) const {
        if (m_skip) {
            return Values{Value()};
        }
        const auto a = toNumber(first(left));
        const auto b = toNumber(first(right));
        if (!a || !b) {
            return fail(ErrorCode::Invalid, "attempt to perform arithmetic on a non-number value");
        }
        switch (op) {
        case '+':
            return Values{Value(*a + *b)};
        case '-':
            return Values{Value(*a - *b)};
        case '*':
            return Values{Value(*a * *b)};
        default:
            return Values{Value(*a / *b)};
        }
    }

    // One comparison: equality of any values, order of two numbers or two strings.
    static std::expected<bool, Error> compare(std::string_view op, const Value& a, const Value& b) {
        if (op == "==") {
            return a == b;
        }
        if (op == "~=") {
            return !(a == b);
        }
        int order = 0;
        const std::optional<double> x = a.number();
        const std::optional<double> y = b.number();
        const std::optional<std::string_view> s = a.string();
        const std::optional<std::string_view> t = b.string();
        if (x && y) {
            order = *x < *y ? -1 : (*x > *y ? 1 : 0);
        } else if (s && t) {
            order = s->compare(*t);
        } else {
            return fail(ErrorCode::Invalid, std::format("attempt to compare {} with {}", typeName(a), typeName(b)));
        }
        if (op == "<") {
            return order < 0;
        }
        if (op == ">") {
            return order > 0;
        }
        return op == "<=" ? order <= 0 : order >= 0;
    }

    script::LuaVm& m_vm;
    std::vector<Token> m_tokens;
    std::size_t m_at = 0;
    bool m_skip = false;
};

} // namespace

LuaConsole::LuaConsole() : m_history(std::make_shared<std::vector<std::string>>()) {}

void LuaConsole::remember(std::string_view line) {
    if (line.empty() || (!m_history->empty() && m_history->back() == line)) {
        return;
    }
    m_history->emplace_back(line);
    if (m_history->size() > kHistoryLength) {
        m_history->erase(m_history->begin());
    }
}

std::expected<std::vector<std::string>, Error> LuaConsole::run(script::LuaVm& vm, std::string_view line) {
    remember(line);
    auto tokens = tokenize(line);
    if (!tokens) {
        return std::unexpected(tokens.error());
    }
    Evaluator evaluator(vm, std::move(*tokens));
    return evaluator.chunk();
}

std::expected<std::vector<std::string>, Error> LuaConsole::runFile(script::LuaVm& vm, const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return fail(ErrorCode::NotFound, std::format("{}: cannot be read", path));
    }
    const std::string text{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    // A compiled chunk starts with ESC "Lua".
    if (text.starts_with("\x1bLua")) {
        auto chunk = script::loadLuaChunk(std::as_bytes(std::span(text)));
        if (!chunk) {
            return std::unexpected(chunk.error());
        }
        auto results = vm.run(*chunk);
        if (!results) {
            return std::unexpected(results.error());
        }
        return std::vector<std::string>{std::format("{}: ran, {} results", path, results->size())};
    }
    auto tokens = tokenize(text);
    if (!tokens) {
        return std::unexpected(tokens.error());
    }
    Evaluator evaluator(vm, std::move(*tokens));
    return evaluator.chunk();
}

std::expected<std::vector<script::Value>, Error> evaluateLua(script::LuaVm& vm, std::string_view text) {
    auto tokens = tokenize(text);
    if (!tokens) {
        return std::unexpected(tokens.error());
    }
    Evaluator evaluator(vm, std::move(*tokens));
    return evaluator.wholeExpression();
}

} // namespace coney::debug
