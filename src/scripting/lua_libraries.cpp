// SPDX-License-Identifier: GPL-3.0-or-later
#include "scripting/lua_libraries.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <memory>
#include <optional>
#include <utility>

#include "core/assert.h"

namespace coney::script {

namespace {

// What a library function returns: its results, or an error that stops the script.
using Results = std::expected<std::vector<Value>, Error>;

// No results.
Results none() { return std::vector<Value>{}; }
// One result.
Results one(Value value) { return std::vector<Value>{std::move(value)}; }

// Argument `i` (0-based), nil when it was not passed.
Value argAt(std::span<const Value> args, std::size_t i) { return i < args.size() ? args[i] : Value(); }

// A value as a number the way Lua's library reads one: a number, or a string that reads as one.
std::optional<double> asNumber(const Value& value) {
    if (const std::optional<double> number = value.number()) {
        return number;
    }
    if (const std::optional<std::string_view> text = value.string()) {
        return parseLuaNumber(*text);
    }
    return std::nullopt;
}

// A value as a string the way Lua's library reads one: a string, or a number in `%.16g`.
std::optional<std::string> asString(const Value& value) {
    if (const std::optional<std::string_view> text = value.string()) {
        return std::string(*text);
    }
    if (const std::optional<double> number = value.number()) {
        return std::format("{:.16g}", *number);
    }
    return std::nullopt;
}

// The error a library function reports for a bad argument, in Lua's words.
std::unexpected<Error> badArgument(std::string_view function, std::size_t i, std::string_view expected) {
    return fail(ErrorCode::Invalid,
                std::format("Lua: bad argument #{} to `{}' ({} expected)", i + 1, function, expected));
}

// Argument `i` as a number; a bad argument error otherwise.
std::expected<double, Error> checkNumber(std::span<const Value> args, std::size_t i, std::string_view function) {
    const std::optional<double> number = asNumber(argAt(args, i));
    if (!number) {
        return badArgument(function, i, "number");
    }
    return *number;
}

// Argument `i` as a number, or `fallback` when it is nil or absent.
std::expected<double, Error> optNumber(std::span<const Value> args, std::size_t i, double fallback,
                                       std::string_view function) {
    return argAt(args, i).isNil() ? std::expected<double, Error>(fallback) : checkNumber(args, i, function);
}

// Argument `i` as a string; a bad argument error otherwise.
std::expected<std::string, Error> checkString(std::span<const Value> args, std::size_t i, std::string_view function) {
    std::optional<std::string> text = asString(argAt(args, i));
    if (!text) {
        return badArgument(function, i, "string");
    }
    return std::move(*text);
}

// Argument `i` as a table; a bad argument error otherwise.
std::expected<std::shared_ptr<Table>, Error> checkTable(std::span<const Value> args, std::size_t i,
                                                        std::string_view function) {
    // A copy: argAt returns the argument by value, so a reference into it would dangle.
    std::shared_ptr<Table> table = argAt(args, i).table();
    if (!table) {
        return badArgument(function, i, "table");
    }
    return table;
}

// A double truncated to an integer as C's cast does, clamped so the conversion is defined.
std::int64_t toInteger(double number) {
    constexpr double kLimit = 9.0e18;
    if (std::isnan(number)) {
        return 0;
    }
    return static_cast<std::int64_t>(std::clamp(number, -kLimit, kLimit));
}

// Lua 4.0's `getn`: the field `n` when it is a number, else the largest positive numeric key with a value.
std::int64_t tableLength(const Table& table) {
    if (const std::optional<double> n = table.field("n").number()) {
        return toInteger(*n);
    }
    double largest = 0.0;
    for (std::optional<std::pair<Value, Value>> entry = table.next(Value()); entry; entry = table.next(entry->first)) {
        if (const std::optional<double> key = entry->first.number(); key && *key > largest) {
            largest = *key;
        }
    }
    return toInteger(largest);
}

// Sets t[i] for a whole-number i; never fails, since such a key is never nil or NaN.
void setIndex(Table& table, std::int64_t i, Value value) {
    const bool stored = table.set(Value(static_cast<double>(i)), std::move(value)).has_value();
    CONEY_ASSERT(stored);
}

// Sets t.n.
void setLength(Table& table, std::int64_t n) {
    const bool stored = table.set(Value(std::string("n")), Value(static_cast<double>(n))).has_value();
    CONEY_ASSERT(stored);
}

// ---- The base library ----

// The type tags of Lua 4.0 (`tag(v)` for a value without a tag of its own).
constexpr double kTagNil = 1;
constexpr double kTagNumber = 2;
constexpr double kTagString = 3;
constexpr double kTagTable = 4;
constexpr double kTagFunction = 5;
// The first tag `newtag` gives: Lua 4.0 reserves the type tags below it.
constexpr double kFirstUserTag = 7;

// `type(v)`'s answer.
std::string_view typeName(const Value& value) {
    switch (value.type()) {
    case Value::Type::Nil:
        return "nil";
    case Value::Type::Number:
        return "number";
    case Value::Type::String:
        return "string";
    case Value::Type::Table:
        return "table";
    case Value::Type::Function:
        return "function";
    }
    return "userdata";
}

// `tag(v)`'s answer: the type tag, since no value carries a tag of its own in Coney.
double typeTag(const Value& value) {
    switch (value.type()) {
    case Value::Type::Nil:
        return kTagNil;
    case Value::Type::Number:
        return kTagNumber;
    case Value::Type::String:
        return kTagString;
    case Value::Type::Table:
        return kTagTable;
    case Value::Type::Function:
        return kTagFunction;
    }
    return 0;
}

// `tonumber(e [, base])`: the number e reads as, in base 10 (Lua's notation) or another base from 2 to 36; nil if none.
Results toNumber(std::span<const Value> args) {
    const Value& value = argAt(args, 0);
    auto base = optNumber(args, 1, 10.0, "tonumber");
    if (!base) {
        return std::unexpected(std::move(base.error()));
    }
    if (*base == 10.0) {
        const std::optional<double> number = asNumber(value);
        return number ? one(Value(*number)) : one(Value());
    }
    const std::int64_t radix = toInteger(*base);
    if (radix < 2 || radix > 36) {
        return fail(ErrorCode::Invalid, "Lua: bad argument #2 to `tonumber' (base out of range)");
    }
    const std::optional<std::string> text = asString(value);
    if (!text) {
        return one(Value());
    }
    // Digits of the base, optional spaces around them, nothing else (C's strtoul as Lua 4.0 uses it).
    std::string_view digits(*text);
    while (!digits.empty() && (digits.front() == ' ' || digits.front() == '\t' || digits.front() == '\n')) {
        digits.remove_prefix(1);
    }
    while (!digits.empty() && (digits.back() == ' ' || digits.back() == '\t' || digits.back() == '\n')) {
        digits.remove_suffix(1);
    }
    if (digits.empty()) {
        return one(Value());
    }
    double number = 0.0;
    for (const char c : digits) {
        int digit = 36;
        if (c >= '0' && c <= '9') {
            digit = c - '0';
        } else if (c >= 'a' && c <= 'z') {
            digit = c - 'a' + 10;
        } else if (c >= 'A' && c <= 'Z') {
            digit = c - 'A' + 10;
        }
        if (digit >= radix) {
            return one(Value());
        }
        number = number * static_cast<double>(radix) + digit;
    }
    return one(Value(number));
}

// `sort(t [, comp])`: sorts t[1..getn(t)] with `<` or `comp(a, b)`. The first error a comparison raises is returned
// once the sort is done; a stable merge sort stays safe when a script's comparison is inconsistent.
Results sortTable(LuaVm& vm, std::span<const Value> args) {
    auto table = checkTable(args, 0, "sort");
    if (!table) {
        return std::unexpected(std::move(table.error()));
    }
    const Value comparison = argAt(args, 1);
    if (!comparison.isNil() && !comparison.function()) {
        return badArgument("sort", 1, "function");
    }
    const std::int64_t n = tableLength(**table);
    std::vector<Value> items;
    items.reserve(static_cast<std::size_t>(std::max<std::int64_t>(n, 0)));
    for (std::int64_t i = 1; i <= n; ++i) {
        items.push_back((*table)->get(Value(static_cast<double>(i))));
    }
    std::optional<Error> failure;
    // a < b: the script's comparison, or Lua's `<` for two numbers or two strings.
    const auto less = [&](const Value& a, const Value& b) {
        if (failure) {
            return false;
        }
        if (!comparison.isNil()) {
            const std::array<Value, 2> pair{a, b};
            auto result = vm.call(comparison, pair);
            if (!result) {
                failure = std::move(result.error());
                return false;
            }
            return !result->empty() && !result->front().isNil();
        }
        const std::optional<double> an = a.number();
        const std::optional<double> bn = b.number();
        if (an && bn) {
            return *an < *bn;
        }
        const std::optional<std::string_view> as = a.string();
        const std::optional<std::string_view> bs = b.string();
        if (as && bs) {
            return *as < *bs;
        }
        failure = Error{ErrorCode::Invalid, "Lua: attempt to compare two values of different types in `sort'"};
        return false;
    };
    std::ranges::stable_sort(items, less);
    if (failure) {
        return std::unexpected(std::move(*failure));
    }
    for (std::size_t i = 0; i < items.size(); ++i) {
        setIndex(**table, static_cast<std::int64_t>(i) + 1, std::move(items[i]));
    }
    return none();
}

// `tinsert(t, [pos,] v)`: inserts v at pos (the end by default), moving the items above up; sets t.n.
Results tableInsert(std::span<const Value> args) {
    auto table = checkTable(args, 0, "tinsert");
    if (!table) {
        return std::unexpected(std::move(table.error()));
    }
    std::int64_t n = tableLength(**table);
    std::int64_t position = n + 1;
    if (args.size() > 2) {
        auto at = checkNumber(args, 1, "tinsert");
        if (!at) {
            return std::unexpected(std::move(at.error()));
        }
        position = toInteger(*at);
    }
    setLength(**table, n + 1);
    for (; n >= position; --n) {
        setIndex(**table, n + 1, (*table)->get(Value(static_cast<double>(n))));
    }
    setIndex(**table, position, args.empty() ? Value() : args.back());
    return none();
}

// `tremove(t [, pos])`: removes and returns t[pos] (the last item by default), moving the items above down; sets t.n.
Results tableRemove(std::span<const Value> args) {
    auto table = checkTable(args, 0, "tremove");
    if (!table) {
        return std::unexpected(std::move(table.error()));
    }
    const std::int64_t n = tableLength(**table);
    auto at = optNumber(args, 1, static_cast<double>(n), "tremove");
    if (!at) {
        return std::unexpected(std::move(at.error()));
    }
    if (n <= 0) {
        return none();
    }
    std::int64_t position = toInteger(*at);
    Value removed = (*table)->get(Value(static_cast<double>(position)));
    for (; position < n; ++position) {
        setIndex(**table, position, (*table)->get(Value(static_cast<double>(position + 1))));
    }
    setLength(**table, n - 1);
    setIndex(**table, n, Value());
    return one(std::move(removed));
}

// `foreach(t, f)` and `foreachi(t, f)`: calls f(k, v) for every entry (or every index 1..getn) until f returns
// something other than nil, and returns that.
Results forEach(LuaVm& vm, std::span<const Value> args, bool indexed) {
    const std::string_view name = indexed ? "foreachi" : "foreach";
    auto table = checkTable(args, 0, name);
    if (!table) {
        return std::unexpected(std::move(table.error()));
    }
    const Value function = argAt(args, 1);
    if (!function.function()) {
        return badArgument(name, 1, "function");
    }
    // Calls f(k, v); the first result that is not nil ends the loop.
    const auto visit = [&vm, &function](const Value& key, const Value& value) -> std::optional<Results> {
        const std::array<Value, 2> pair{key, value};
        auto result = vm.call(function, pair);
        if (!result) {
            return Results(std::unexpected(std::move(result.error())));
        }
        if (!result->empty() && !result->front().isNil()) {
            return one(result->front());
        }
        return std::nullopt;
    };
    if (indexed) {
        const std::int64_t n = tableLength(**table);
        for (std::int64_t i = 1; i <= n; ++i) {
            const Value key(static_cast<double>(i));
            if (std::optional<Results> done = visit(key, (*table)->get(key))) {
                return std::move(*done);
            }
        }
        return none();
    }
    for (std::optional<std::pair<Value, Value>> entry = (*table)->next(Value()); entry;
         entry = (*table)->next(entry->first)) {
        if (std::optional<Results> done = visit(entry->first, entry->second)) {
            return std::move(*done);
        }
    }
    return none();
}

// `call(f, args [, mode])`: calls f with the items of the table args; with "x" in mode an error returns nil instead.
Results callFunction(LuaVm& vm, std::span<const Value> args) {
    const Value function = argAt(args, 0);
    auto list = checkTable(args, 1, "call");
    if (!list) {
        return std::unexpected(std::move(list.error()));
    }
    const std::optional<std::string> mode = asString(argAt(args, 2));
    const bool protectedCall = mode && mode->find('x') != std::string::npos;
    std::vector<Value> callArgs;
    const std::int64_t n = tableLength(**list);
    for (std::int64_t i = 1; i <= n; ++i) {
        callArgs.push_back((*list)->get(Value(static_cast<double>(i))));
    }
    auto result = vm.call(function, callArgs);
    if (!result && protectedCall) {
        return one(Value());
    }
    return result;
}

// `next(t [, k])`: the entry after k, as two results; nil at the end.
Results nextEntry(std::span<const Value> args, std::string_view name) {
    auto table = checkTable(args, 0, name);
    if (!table) {
        return std::unexpected(std::move(table.error()));
    }
    std::optional<std::pair<Value, Value>> entry = (*table)->next(argAt(args, 1));
    if (!entry) {
        return one(Value());
    }
    return std::vector<Value>{std::move(entry->first), std::move(entry->second)};
}

// `rawget(t, k)`: t[k] without tag methods.
Results rawGet(std::span<const Value> args) {
    auto table = checkTable(args, 0, "rawget");
    if (!table) {
        return std::unexpected(std::move(table.error()));
    }
    return one((*table)->get(argAt(args, 1)));
}

// `rawset(t, k, v)`: t[k] = v without tag methods; returns t.
Results rawSet(std::span<const Value> args) {
    auto table = checkTable(args, 0, "rawset");
    if (!table) {
        return std::unexpected(std::move(table.error()));
    }
    if (auto set = (*table)->set(argAt(args, 1), argAt(args, 2)); !set) {
        return std::unexpected(std::move(set.error()));
    }
    return one(Value(*table));
}

// Registers a library function under `name`.
void add(LuaVm& vm, std::string_view name, NativeFunction function) { vm.registerFunction(name, std::move(function)); }

// ---- The string library ----

// Lua's relative string position: negative counts from the end (-1 the last character).
std::int64_t relativePosition(std::int64_t position, std::size_t length) {
    return position >= 0 ? position : static_cast<std::int64_t>(length) + position + 1;
}

// The pattern characters that make strfind use the matcher instead of a plain search.
constexpr std::string_view kPatternSpecials = "^$*+?.([%-";
// Most captures a pattern may have.
constexpr int kMaxCaptures = 32;
// A capture's length while it is open, and for a position capture `()`.
constexpr std::ptrdiff_t kCaptureOpen = -1;
constexpr std::ptrdiff_t kCapturePosition = -2;

// Lua's pattern matcher over one subject and one pattern: written anew from Lua's documented pattern language
// (character classes, sets, the four repetitions, anchors, captures, back references and %b).
class PatternMatcher {
  public:
    PatternMatcher(std::string_view subject, std::string_view pattern) : m_src(subject), m_pat(pattern) {}

    // Tries the pattern from pattern position `p` against the subject from `s`: the end of the match, or nothing.
    std::optional<std::size_t> match(std::size_t s, std::size_t p) {
        m_level = 0;
        return doMatch(s, p);
    }

    // The error a malformed pattern raised, if any.
    [[nodiscard]] const std::optional<Error>& error() const { return m_error; }
    // How many captures the last match made.
    [[nodiscard]] int level() const { return m_level; }

    // Capture `i` of the match [s, e): a string, a position, or the whole match for i = 0 with no captures.
    [[nodiscard]] Value capture(int i, std::size_t s, std::size_t e) {
        if (i >= m_level) {
            if (i == 0) {
                return Value(std::string(m_src.substr(s, e - s)));
            }
            setError("invalid capture index");
            return {};
        }
        const auto [start, length] = m_captures.at(static_cast<std::size_t>(i));
        if (length == kCapturePosition) {
            return Value(static_cast<double>(start + 1));
        }
        return Value(std::string(m_src.substr(start, static_cast<std::size_t>(length))));
    }

    // Every capture of the match [s, e), or the whole match when the pattern has none (and `wholeIfNone`).
    [[nodiscard]] std::vector<Value> captures(std::size_t s, std::size_t e, bool wholeIfNone) {
        std::vector<Value> values;
        const int count = (m_level == 0 && wholeIfNone) ? 1 : m_level;
        values.reserve(static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i) {
            values.push_back(capture(i, s, e));
        }
        return values;
    }

  private:
    // Records the first error; matching then fails everywhere.
    void setError(std::string_view what) {
        if (!m_error) {
            m_error = Error{ErrorCode::Invalid, std::format("Lua: {}", what)};
        }
    }

    // The character at pattern position p, or 0 past the end (the C string's terminator in Lua's matcher).
    [[nodiscard]] char pat(std::size_t p) const { return p < m_pat.size() ? m_pat[p] : '\0'; }

    // Whether c is in the class named by the letter `cl` (%a, %d, ...; upper case for the complement), or equals cl.
    static bool matchClass(unsigned char c, unsigned char cl) {
        bool result = false;
        switch (std::tolower(cl)) {
        case 'a':
            result = std::isalpha(c) != 0;
            break;
        case 'c':
            result = std::iscntrl(c) != 0;
            break;
        case 'd':
            result = std::isdigit(c) != 0;
            break;
        case 'l':
            result = std::islower(c) != 0;
            break;
        case 'p':
            result = std::ispunct(c) != 0;
            break;
        case 's':
            result = std::isspace(c) != 0;
            break;
        case 'u':
            result = std::isupper(c) != 0;
            break;
        case 'w':
            result = std::isalnum(c) != 0;
            break;
        case 'x':
            result = std::isxdigit(c) != 0;
            break;
        default:
            return cl == c;
        }
        return std::isupper(cl) != 0 ? !result : result;
    }

    // The pattern position after the single-character class at p; records an error for a malformed one.
    std::size_t classEnd(std::size_t p) {
        const char c = pat(p++);
        if (c == '%') {
            if (p >= m_pat.size()) {
                setError("malformed pattern (ends with `%')");
                return m_pat.size();
            }
            return p + 1;
        }
        if (c == '[') {
            if (pat(p) == '^') {
                ++p;
            }
            // The first character of a set may be `]' itself.
            do {
                if (p >= m_pat.size()) {
                    setError("malformed pattern (missing `]')");
                    return m_pat.size();
                }
                const char next = pat(p);
                ++p;
                if (next == '%' && p < m_pat.size()) {
                    ++p;
                }
            } while (pat(p) != ']');
            return p + 1;
        }
        return p;
    }

    // Whether c is in the set [p, ec], with p at '[' and ec at the closing ']'.
    bool matchSet(unsigned char c, std::size_t p, std::size_t ec) const {
        bool inside = true;
        if (pat(p + 1) == '^') {
            inside = false;
            ++p;
        }
        while (++p < ec) {
            if (pat(p) == '%') {
                ++p;
                if (matchClass(c, static_cast<unsigned char>(pat(p)))) {
                    return inside;
                }
            } else if (pat(p + 1) == '-' && p + 2 < ec) {
                p += 2;
                if (static_cast<unsigned char>(pat(p - 2)) <= c && c <= static_cast<unsigned char>(pat(p))) {
                    return inside;
                }
            } else if (static_cast<unsigned char>(pat(p)) == c) {
                return inside;
            }
        }
        return !inside;
    }

    // Whether the subject character at s matches the single-character class [p, ep).
    bool singleMatch(std::size_t s, std::size_t p, std::size_t ep) const {
        if (s >= m_src.size()) {
            return false;
        }
        const auto c = static_cast<unsigned char>(m_src[s]);
        switch (pat(p)) {
        case '.':
            return true;
        case '%':
            return matchClass(c, static_cast<unsigned char>(pat(p + 1)));
        case '[':
            return matchSet(c, p, ep - 1);
        default:
            return static_cast<unsigned char>(pat(p)) == c;
        }
    }

    // The matcher proper: the end of a match of the pattern from p against the subject from s.
    // NOLINTNEXTLINE(misc-no-recursion): the pattern language is recursive; the depth is bounded by the pattern
    std::optional<std::size_t> doMatch(std::size_t s, std::size_t p) {
        while (!m_error) {
            if (p >= m_pat.size()) {
                return s;
            }
            switch (pat(p)) {
            case '(':
                return pat(p + 1) == ')' ? startCapture(s, p + 2, kCapturePosition)
                                         : startCapture(s, p + 1, kCaptureOpen);
            case ')':
                return endCapture(s, p + 1);
            case '%':
                if (std::isdigit(static_cast<unsigned char>(pat(p + 1))) != 0) {
                    const std::optional<std::size_t> next = matchBackReference(s, pat(p + 1));
                    if (!next) {
                        return std::nullopt;
                    }
                    s = *next;
                    p += 2;
                    continue;
                }
                if (pat(p + 1) == 'b') {
                    const std::optional<std::size_t> next = matchBalance(s, p + 2);
                    if (!next) {
                        return std::nullopt;
                    }
                    s = *next;
                    p += 4;
                    continue;
                }
                break;
            case '$':
                if (p + 1 == m_pat.size()) {
                    return s == m_src.size() ? std::optional<std::size_t>(s) : std::nullopt;
                }
                break;
            default:
                break;
            }
            // A single-character class, perhaps followed by a repetition.
            const std::size_t ep = classEnd(p);
            if (m_error) {
                return std::nullopt;
            }
            const bool matches = singleMatch(s, p, ep);
            switch (pat(ep)) {
            case '?': {
                if (matches) {
                    if (const std::optional<std::size_t> end = doMatch(s + 1, ep + 1)) {
                        return end;
                    }
                }
                p = ep + 1;
                continue;
            }
            case '*':
                return maxExpand(s, p, ep);
            case '+':
                return matches ? maxExpand(s + 1, p, ep) : std::nullopt;
            case '-':
                return minExpand(s, p, ep);
            default:
                if (!matches) {
                    return std::nullopt;
                }
                ++s;
                p = ep;
                continue;
            }
        }
        return std::nullopt;
    }

    // `*` and `+`: as many repetitions as match, then fewer until the rest matches.
    // NOLINTNEXTLINE(misc-no-recursion): part of doMatch's recursion
    std::optional<std::size_t> maxExpand(std::size_t s, std::size_t p, std::size_t ep) {
        std::size_t count = 0;
        while (singleMatch(s + count, p, ep)) {
            ++count;
        }
        while (true) {
            if (const std::optional<std::size_t> end = doMatch(s + count, ep + 1)) {
                return end;
            }
            if (count == 0 || m_error) {
                return std::nullopt;
            }
            --count;
        }
    }

    // `-`: as few repetitions as let the rest match.
    // NOLINTNEXTLINE(misc-no-recursion): part of doMatch's recursion
    std::optional<std::size_t> minExpand(std::size_t s, std::size_t p, std::size_t ep) {
        while (true) {
            if (const std::optional<std::size_t> end = doMatch(s, ep + 1)) {
                return end;
            }
            if (m_error || !singleMatch(s, p, ep)) {
                return std::nullopt;
            }
            ++s;
        }
    }

    // Opens capture `what` (open or position) at s and matches the rest from p.
    // NOLINTNEXTLINE(misc-no-recursion): part of doMatch's recursion
    std::optional<std::size_t> startCapture(std::size_t s, std::size_t p, std::ptrdiff_t what) {
        if (m_level >= kMaxCaptures) {
            setError("too many captures");
            return std::nullopt;
        }
        m_captures.at(static_cast<std::size_t>(m_level)) = {s, what};
        ++m_level;
        std::optional<std::size_t> end = doMatch(s, p);
        if (!end) {
            --m_level;
        }
        return end;
    }

    // Closes the innermost open capture at s and matches the rest from p.
    // NOLINTNEXTLINE(misc-no-recursion): part of doMatch's recursion
    std::optional<std::size_t> endCapture(std::size_t s, std::size_t p) {
        int open = m_level - 1;
        while (open >= 0 && m_captures.at(static_cast<std::size_t>(open)).second != kCaptureOpen) {
            --open;
        }
        if (open < 0) {
            setError("invalid pattern capture");
            return std::nullopt;
        }
        auto& capture = m_captures.at(static_cast<std::size_t>(open));
        capture.second = static_cast<std::ptrdiff_t>(s - capture.first);
        std::optional<std::size_t> end = doMatch(s, p);
        if (!end) {
            capture.second = kCaptureOpen;
        }
        return end;
    }

    // `%bxy`: a balanced run from x to its matching y.
    std::optional<std::size_t> matchBalance(std::size_t s, std::size_t p) {
        if (p + 1 >= m_pat.size()) {
            setError("unbalanced pattern");
            return std::nullopt;
        }
        if (s >= m_src.size() || m_src[s] != pat(p)) {
            return std::nullopt;
        }
        const char open = pat(p);
        const char close = pat(p + 1);
        int depth = 1;
        for (std::size_t i = s + 1; i < m_src.size(); ++i) {
            if (m_src[i] == close) {
                --depth;
                if (depth == 0) {
                    return i + 1;
                }
            } else if (m_src[i] == open) {
                ++depth;
            }
        }
        return std::nullopt;
    }

    // `%1`-`%9`: the text of an earlier, closed capture again.
    std::optional<std::size_t> matchBackReference(std::size_t s, char digit) {
        const int index = digit - '1';
        if (index < 0 || index >= m_level || m_captures.at(static_cast<std::size_t>(index)).second == kCaptureOpen) {
            setError("invalid capture index");
            return std::nullopt;
        }
        const auto [start, length] = m_captures.at(static_cast<std::size_t>(index));
        const auto size = static_cast<std::size_t>(std::max<std::ptrdiff_t>(length, 0));
        if (m_src.size() - s >= size && m_src.substr(start, size) == m_src.substr(s, size)) {
            return s + size;
        }
        return std::nullopt;
    }

    std::string_view m_src;
    std::string_view m_pat;
    int m_level = 0;
    std::array<std::pair<std::size_t, std::ptrdiff_t>, kMaxCaptures> m_captures{}; // start, length (or a marker)
    std::optional<Error> m_error;
};

// `strfind(s, pattern [, init [, plain]])`: the start and end (1-based) of the first match and its captures; nil if
// none.
Results stringFind(std::span<const Value> args) {
    auto subject = checkString(args, 0, "strfind");
    auto pattern = checkString(args, 1, "strfind");
    auto init = optNumber(args, 2, 1.0, "strfind");
    if (!subject || !pattern || !init) {
        return std::unexpected(!subject ? subject.error() : !pattern ? pattern.error() : init.error());
    }
    const std::int64_t start = relativePosition(toInteger(*init), subject->size()) - 1;
    const auto from = static_cast<std::size_t>(std::clamp<std::int64_t>(start, 0, std::ssize(*subject)));
    const bool plain = !argAt(args, 3).isNil() || pattern->find_first_of(kPatternSpecials) == std::string::npos;
    if (plain) {
        const std::size_t found = std::string_view(*subject).find(*pattern, from);
        if (found == std::string::npos) {
            return one(Value());
        }
        return std::vector<Value>{Value(static_cast<double>(found + 1)),
                                  Value(static_cast<double>(found + pattern->size()))};
    }
    const bool anchored = pattern->starts_with('^');
    PatternMatcher matcher(*subject, *pattern);
    for (std::size_t s = from;; ++s) {
        const std::optional<std::size_t> end = matcher.match(s, anchored ? 1 : 0);
        if (const auto& failure = matcher.error()) {
            return std::unexpected(*failure);
        }
        if (end) {
            std::vector<Value> results{Value(static_cast<double>(s + 1)), Value(static_cast<double>(*end))};
            std::vector<Value> captures = matcher.captures(s, *end, false);
            results.insert(results.end(), captures.begin(), captures.end());
            return results;
        }
        if (anchored || s >= subject->size()) {
            return one(Value());
        }
    }
}

// `gsub(s, pattern, repl [, n])`: s with (at most n) matches replaced by repl, a string with %0-%9 references or a
// function of the captures; and the number of replacements.
Results stringReplace(LuaVm& vm, std::span<const Value> args) {
    auto subject = checkString(args, 0, "gsub");
    auto pattern = checkString(args, 1, "gsub");
    if (!subject || !pattern) {
        return std::unexpected(!subject ? subject.error() : pattern.error());
    }
    const Value& replacement = argAt(args, 2);
    const std::optional<std::string> replacementText = asString(replacement);
    if (!replacementText && !replacement.function()) {
        return badArgument("gsub", 2, "string or function");
    }
    auto limit = optNumber(args, 3, static_cast<double>(subject->size() + 1), "gsub");
    if (!limit) {
        return std::unexpected(std::move(limit.error()));
    }
    const bool anchored = pattern->starts_with('^');
    PatternMatcher matcher(*subject, *pattern);
    std::string result;
    std::size_t s = 0;
    std::int64_t count = 0;
    while (static_cast<double>(count) < *limit) {
        const std::optional<std::size_t> end = matcher.match(s, anchored ? 1 : 0);
        if (const auto& failure = matcher.error()) {
            return std::unexpected(*failure);
        }
        if (end) {
            ++count;
            // Append the replacement of the match [s, end).
            if (replacementText) {
                for (std::size_t i = 0; i < replacementText->size(); ++i) {
                    const char c = (*replacementText)[i];
                    if (c != '%' || i + 1 >= replacementText->size()) {
                        result += c;
                        continue;
                    }
                    const char next = (*replacementText)[++i];
                    if (std::isdigit(static_cast<unsigned char>(next)) == 0) {
                        result += next;
                        continue;
                    }
                    const Value capture = next == '0' ? Value(std::string(subject->substr(s, *end - s)))
                                                      : matcher.capture(next - '1', s, *end);
                    if (const auto& failure = matcher.error()) {
                        return std::unexpected(*failure);
                    }
                    result += asString(capture).value_or(std::string());
                }
            } else {
                const std::vector<Value> captures = matcher.captures(s, *end, true);
                auto replaced = vm.call(replacement, captures);
                if (!replaced) {
                    return std::unexpected(std::move(replaced.error()));
                }
                if (!replaced->empty()) {
                    result += asString(replaced->front()).value_or(std::string());
                }
            }
        }
        if (end && *end > s) {
            s = *end;
        } else if (s < subject->size()) {
            result += (*subject)[s++];
        } else {
            break;
        }
        if (anchored) {
            break;
        }
    }
    result += subject->substr(std::min(s, subject->size()));
    return std::vector<Value>{Value(std::move(result)), Value(static_cast<double>(count))};
}

// `%q`: a string in double quotes that Lua reads back as the same string.
std::string quoted(std::string_view text) {
    std::string out = "\"";
    for (const char c : text) {
        if (c == '"' || c == '\\' || c == '\n') {
            out += '\\';
        }
        out += c;
    }
    out += '"';
    return out;
}

// A std::format replacement field `{:<parts>}`.
std::string replacementField(std::string_view a, std::string_view b = {}, std::string_view c = {}) {
    std::string field = "{:";
    field.append(a).append(b).append(c).append("}");
    return field;
}

// `format(fmt, ...)`: C's printf conversions (`%c %d %i %o %u %x %X %e %E %f %g %G %s %q %%` with flags, width and
// precision), done through std::format so no C format string is ever built at run time.
Results stringFormat(std::span<const Value> args) {
    auto format = checkString(args, 0, "format");
    if (!format) {
        return std::unexpected(std::move(format.error()));
    }
    std::string out;
    std::size_t next = 1;
    for (std::size_t i = 0; i < format->size(); ++i) {
        const char c = (*format)[i];
        if (c != '%') {
            out += c;
            continue;
        }
        if (i + 1 < format->size() && (*format)[i + 1] == '%') {
            out += '%';
            ++i;
            continue;
        }
        // The specification: flags, width, precision, conversion.
        std::string flags;
        std::size_t j = i + 1;
        while (j < format->size() && std::string_view("-+ #0").find((*format)[j]) != std::string_view::npos) {
            flags += (*format)[j++];
        }
        std::string width;
        while (j < format->size() && std::isdigit(static_cast<unsigned char>((*format)[j])) != 0) {
            width += (*format)[j++];
        }
        std::string precision;
        if (j < format->size() && (*format)[j] == '.') {
            precision = ".";
            ++j;
            while (j < format->size() && std::isdigit(static_cast<unsigned char>((*format)[j])) != 0) {
                precision += (*format)[j++];
            }
            if (precision == ".") {
                precision = ".0";
            }
        }
        if (j >= format->size()) {
            return fail(ErrorCode::Invalid, "Lua: invalid format (ends inside a conversion)");
        }
        const char conversion = (*format)[j];
        i = j;
        const std::size_t argument = next++;
        // The std::format specification for the same flags: '-' aligns left, else numbers and strings align right.
        const bool left = flags.find('-') != std::string::npos;
        std::string spec = left ? "<" : ">";
        for (const char flag : {'+', ' ', '#'}) {
            if (flags.find(flag) != std::string::npos) {
                spec += flag;
            }
        }
        if (!left && flags.find('0') != std::string::npos && conversion != 's' && conversion != 'c') {
            spec = spec.substr(1) + "0"; // zero padding replaces the alignment
        }
        spec += width;
        switch (conversion) {
        case 'c': {
            auto number = checkNumber(args, argument, "format");
            if (!number) {
                return std::unexpected(std::move(number.error()));
            }
            const std::string text(1, static_cast<char>(toInteger(*number)));
            out += std::vformat(replacementField(spec), std::make_format_args(text));
            break;
        }
        case 'd':
        case 'i':
        case 'o':
        case 'u':
        case 'x':
        case 'X': {
            auto number = checkNumber(args, argument, "format");
            if (!number) {
                return std::unexpected(std::move(number.error()));
            }
            // Lua 4.0 passes C an int for %d and %i and an unsigned int for the others.
            const auto value = static_cast<std::int32_t>(toInteger(*number));
            const std::string type = conversion == 'o' ? "o" : conversion == 'x' ? "x" : conversion == 'X' ? "X" : "d";
            if (conversion == 'd' || conversion == 'i') {
                out += std::vformat(replacementField(spec, type), std::make_format_args(value));
            } else {
                const auto unsignedValue = static_cast<std::uint32_t>(value);
                out += std::vformat(replacementField(spec, type), std::make_format_args(unsignedValue));
            }
            break;
        }
        case 'e':
        case 'E':
        case 'f':
        case 'g':
        case 'G': {
            auto number = checkNumber(args, argument, "format");
            if (!number) {
                return std::unexpected(std::move(number.error()));
            }
            // printf's default precision is 6 for all five.
            const std::string digits = precision.empty() ? ".6" : precision;
            const double value = *number;
            out += std::vformat(replacementField(spec, digits, std::string_view(&conversion, 1)),
                                std::make_format_args(value));
            break;
        }
        case 'q': {
            auto text = checkString(args, argument, "format");
            if (!text) {
                return std::unexpected(std::move(text.error()));
            }
            out += quoted(*text);
            break;
        }
        case 's': {
            auto text = checkString(args, argument, "format");
            if (!text) {
                return std::unexpected(std::move(text.error()));
            }
            out += std::vformat(replacementField(spec, precision), std::make_format_args(*text));
            break;
        }
        default:
            return fail(ErrorCode::Invalid, std::format("Lua: invalid option `%{}' in `format'", conversion));
        }
    }
    return one(Value(std::move(out)));
}

// `strsub(s, i [, j])`: characters i to j (1-based, negative from the end).
Results stringSub(std::span<const Value> args) {
    auto text = checkString(args, 0, "strsub");
    auto first = checkNumber(args, 1, "strsub");
    auto last = optNumber(args, 2, -1.0, "strsub");
    if (!text || !first || !last) {
        return std::unexpected(!text ? text.error() : !first ? first.error() : last.error());
    }
    std::int64_t start = relativePosition(toInteger(*first), text->size());
    std::int64_t end = relativePosition(toInteger(*last), text->size());
    start = std::max<std::int64_t>(start, 1);
    end = std::min<std::int64_t>(end, std::ssize(*text));
    if (start > end) {
        return one(Value(std::string()));
    }
    return one(Value(text->substr(static_cast<std::size_t>(start - 1), static_cast<std::size_t>(end - start + 1))));
}

// `strbyte(s [, i])` (and `ascii`): the code of character i (1 by default).
Results stringByte(std::span<const Value> args) {
    auto text = checkString(args, 0, "strbyte");
    auto position = optNumber(args, 1, 1.0, "strbyte");
    if (!text || !position) {
        return std::unexpected(!text ? text.error() : position.error());
    }
    const std::int64_t at = relativePosition(toInteger(*position), text->size());
    if (at <= 0 || at > std::ssize(*text)) {
        return fail(ErrorCode::Invalid, "Lua: bad argument #2 to `strbyte' (out of range)");
    }
    return one(Value(static_cast<double>(static_cast<unsigned char>((*text)[static_cast<std::size_t>(at - 1)]))));
}

// `strlower(s)` and `strupper(s)`: the C locale's case mapping.
Results stringCase(std::span<const Value> args, bool upper) {
    auto text = checkString(args, 0, upper ? "strupper" : "strlower");
    if (!text) {
        return std::unexpected(std::move(text.error()));
    }
    for (char& c : *text) {
        const auto byte = static_cast<unsigned char>(c);
        c = static_cast<char>(upper ? std::toupper(byte) : std::tolower(byte));
    }
    return one(Value(std::move(*text)));
}

// ---- The math library ----

// Degrees to radians and back: Lua 4.0's trigonometry works in degrees.
constexpr double kPi = 3.14159265358979323846;
constexpr double kRadiansPerDegree = kPi / 180.0;

// A math function of one number.
NativeFunction unary(std::string_view name, double (*function)(double)) {
    return [name = std::string(name), function](std::span<const Value> args) -> Results {
        auto x = checkNumber(args, 0, name);
        if (!x) {
            return std::unexpected(std::move(x.error()));
        }
        return one(Value(function(*x)));
    };
}

// The math library's random generator: a 64-bit xorshift, Coney's own, so a run never depends on the C library.
struct RandomState {
    std::uint64_t state = 0x9e3779b97f4a7c15ULL;

    // The next value in [0, 1).
    double next() {
        state ^= state << 13U;
        state ^= state >> 7U;
        state ^= state << 17U;
        constexpr double kScale = 1.0 / 9007199254740992.0; // 2^-53
        return static_cast<double>(state >> 11U) * kScale;
    }
};

} // namespace

std::string luaToString(const Value& value) {
    switch (value.type()) {
    case Value::Type::Nil:
        return "nil";
    case Value::Type::Number:
    case Value::Type::String:
        return asString(value).value_or(std::string());
    case Value::Type::Table:
        return std::format("table: {}", static_cast<const void*>(value.table().get()));
    case Value::Type::Function:
        return std::format("function: {}", static_cast<const void*>(value.function().get()));
    }
    return {};
}

void openBaseLibrary(LuaVm& vm, BaseLibraryHooks hooks) {
    auto shared = std::make_shared<BaseLibraryHooks>(std::move(hooks));
    auto nextTag = std::make_shared<double>(kFirstUserTag);
    LuaVm* machine = &vm;

    add(vm, "assert", [](std::span<const Value> args) -> Results {
        if (argAt(args, 0).isNil()) {
            return fail(ErrorCode::Invalid,
                        std::format("Lua: assertion failed!  {}", asString(argAt(args, 1)).value_or(std::string())));
        }
        return none();
    });
    add(vm, "call", [machine](std::span<const Value> args) { return callFunction(*machine, args); });
    add(vm, "collectgarbage", [](std::span<const Value>) { return none(); });
    add(vm, "copytagmethods", [](std::span<const Value> args) { return one(argAt(args, 0)); });
    add(vm, "dofile", [shared](std::span<const Value> args) -> Results {
        auto name = checkString(args, 0, "dofile");
        if (!name) {
            return std::unexpected(std::move(name.error()));
        }
        return shared->doFile ? shared->doFile(*name) : none();
    });
    add(vm, "dostring", [machine](std::span<const Value> args) -> Results {
        auto text = checkString(args, 0, "dostring");
        if (!text) {
            return std::unexpected(std::move(text.error()));
        }
        // Only a precompiled chunk can run: Coney has no Lua compiler. dostring returns nil when it cannot run one.
        const auto* bytes = reinterpret_cast<const std::byte*>(text->data());
        auto chunk = loadLuaChunk(std::span(bytes, text->size()));
        if (!chunk) {
            return one(Value());
        }
        return machine->run(std::move(*chunk));
    });
    add(vm, "error", [](std::span<const Value> args) -> Results {
        return fail(ErrorCode::Invalid, std::format("Lua: {}", asString(argAt(args, 0)).value_or("error")));
    });
    add(vm, "foreach", [machine](std::span<const Value> args) { return forEach(*machine, args, false); });
    add(vm, "foreachi", [machine](std::span<const Value> args) { return forEach(*machine, args, true); });
    add(vm, "gcinfo", [](std::span<const Value>) { return Results(std::vector<Value>{Value(0.0), Value(0.0)}); });
    // getglobal(name), also under its 4.0 compatibility name.
    const NativeFunction getGlobal = [machine](std::span<const Value> args) -> Results {
        auto name = checkString(args, 0, "getglobal");
        if (!name) {
            return std::unexpected(std::move(name.error()));
        }
        return one(machine->global(*name));
    };
    add(vm, "getglobal", getGlobal);
    add(vm, "rawgetglobal", getGlobal);
    add(vm, "gettagmethod", [](std::span<const Value>) { return one(Value()); });
    add(vm, "globals", [machine](std::span<const Value>) -> Results {
        // The globals table is shared with the VM; replacing it is not supported.
        return one(Value(machine->globalsTable()));
    });
    add(vm, "newtag", [nextTag](std::span<const Value>) {
        const double tag = *nextTag;
        *nextTag += 1;
        return one(Value(tag));
    });
    add(vm, "next", [](std::span<const Value> args) { return nextEntry(args, "next"); });
    add(vm, "print", [shared](std::span<const Value> args) {
        std::string line;
        for (std::size_t i = 0; i < args.size(); ++i) {
            line += (i == 0 ? "" : "\t") + luaToString(args[i]);
        }
        if (shared->print) {
            shared->print(line);
        }
        return none();
    });
    add(vm, "rawget", rawGet);
    add(vm, "rawset", rawSet);
    add(vm, "rawgettable", rawGet);
    add(vm, "rawsettable", rawSet);
    // setglobal(name, v), also under its 4.0 compatibility name.
    const NativeFunction setGlobal = [machine](std::span<const Value> args) -> Results {
        auto name = checkString(args, 0, "setglobal");
        if (!name) {
            return std::unexpected(std::move(name.error()));
        }
        machine->setGlobal(*name, argAt(args, 1));
        return none();
    };
    add(vm, "setglobal", setGlobal);
    add(vm, "rawsetglobal", setGlobal);
    add(vm, "settag", [](std::span<const Value> args) { return one(argAt(args, 0)); });
    add(vm, "settagmethod", [](std::span<const Value>) { return one(Value()); });
    add(vm, "tag", [](std::span<const Value> args) { return one(Value(typeTag(argAt(args, 0)))); });
    add(vm, "tonumber", toNumber);
    add(vm, "tostring", [](std::span<const Value> args) { return one(Value(luaToString(argAt(args, 0)))); });
    add(vm, "type", [](std::span<const Value> args) { return one(Value(std::string(typeName(argAt(args, 0))))); });
    add(vm, "getn", [](std::span<const Value> args) -> Results {
        auto table = checkTable(args, 0, "getn");
        if (!table) {
            return std::unexpected(std::move(table.error()));
        }
        return one(Value(static_cast<double>(tableLength(**table))));
    });
    add(vm, "sort", [machine](std::span<const Value> args) { return sortTable(*machine, args); });
    add(vm, "tinsert", tableInsert);
    add(vm, "tremove", tableRemove);

    // Lua 4.0's compatibility names for 3.x scripts.
    add(vm, "foreachvar", [machine](std::span<const Value> args) {
        const std::array<Value, 2> forwarded{Value(machine->globalsTable()), argAt(args, 0)};
        return forEach(*machine, forwarded, false);
    });
    add(vm, "nextvar", [machine](std::span<const Value> args) {
        const std::array<Value, 2> forwarded{Value(machine->globalsTable()), argAt(args, 0)};
        return nextEntry(forwarded, "nextvar");
    });
    vm.setGlobal("_VERSION", Value(std::string("Lua 4.0.1")));
}

void openStringLibrary(LuaVm& vm) {
    LuaVm* machine = &vm;
    add(vm, "strlen", [](std::span<const Value> args) -> Results {
        auto text = checkString(args, 0, "strlen");
        if (!text) {
            return std::unexpected(std::move(text.error()));
        }
        return one(Value(static_cast<double>(text->size())));
    });
    add(vm, "strsub", stringSub);
    add(vm, "strlower", [](std::span<const Value> args) { return stringCase(args, false); });
    add(vm, "strupper", [](std::span<const Value> args) { return stringCase(args, true); });
    add(vm, "strchar", [](std::span<const Value> args) -> Results {
        std::string text;
        for (std::size_t i = 0; i < args.size(); ++i) {
            auto code = checkNumber(args, i, "strchar");
            if (!code) {
                return std::unexpected(std::move(code.error()));
            }
            text += static_cast<char>(toInteger(*code));
        }
        return one(Value(std::move(text)));
    });
    add(vm, "strrep", [](std::span<const Value> args) -> Results {
        auto text = checkString(args, 0, "strrep");
        auto count = checkNumber(args, 1, "strrep");
        if (!text || !count) {
            return std::unexpected(!text ? text.error() : count.error());
        }
        std::string repeated;
        for (std::int64_t i = 0; i < toInteger(*count); ++i) {
            repeated += *text;
        }
        return one(Value(std::move(repeated)));
    });
    add(vm, "strbyte", stringByte);
    add(vm, "ascii", stringByte);
    add(vm, "format", stringFormat);
    add(vm, "strformat", stringFormat);
    add(vm, "strfind", stringFind);
    add(vm, "gsub", [machine](std::span<const Value> args) { return stringReplace(*machine, args); });
}

void openMathLibrary(LuaVm& vm) {
    add(vm, "abs", unary("abs", [](double x) { return std::fabs(x); }));
    add(vm, "sin", unary("sin", [](double x) { return std::sin(x * kRadiansPerDegree); }));
    add(vm, "cos", unary("cos", [](double x) { return std::cos(x * kRadiansPerDegree); }));
    add(vm, "tan", unary("tan", [](double x) { return std::tan(x * kRadiansPerDegree); }));
    add(vm, "asin", unary("asin", [](double x) { return std::asin(x) / kRadiansPerDegree; }));
    add(vm, "acos", unary("acos", [](double x) { return std::acos(x) / kRadiansPerDegree; }));
    add(vm, "atan", unary("atan", [](double x) { return std::atan(x) / kRadiansPerDegree; }));
    add(vm, "atan2", [](std::span<const Value> args) -> Results {
        auto y = checkNumber(args, 0, "atan2");
        auto x = checkNumber(args, 1, "atan2");
        if (!y || !x) {
            return std::unexpected(!y ? y.error() : x.error());
        }
        return one(Value(std::atan2(*y, *x) / kRadiansPerDegree));
    });
    add(vm, "ceil", unary("ceil", [](double x) { return std::ceil(x); }));
    add(vm, "floor", unary("floor", [](double x) { return std::floor(x); }));
    add(vm, "mod", [](std::span<const Value> args) -> Results {
        auto x = checkNumber(args, 0, "mod");
        auto y = checkNumber(args, 1, "mod");
        if (!x || !y) {
            return std::unexpected(!x ? x.error() : y.error());
        }
        return one(Value(std::fmod(*x, *y)));
    });
    add(vm, "frexp", [](std::span<const Value> args) -> Results {
        auto x = checkNumber(args, 0, "frexp");
        if (!x) {
            return std::unexpected(std::move(x.error()));
        }
        int exponent = 0;
        const double mantissa = std::frexp(*x, &exponent);
        return std::vector<Value>{Value(mantissa), Value(static_cast<double>(exponent))};
    });
    add(vm, "ldexp", [](std::span<const Value> args) -> Results {
        auto x = checkNumber(args, 0, "ldexp");
        auto e = checkNumber(args, 1, "ldexp");
        if (!x || !e) {
            return std::unexpected(!x ? x.error() : e.error());
        }
        return one(Value(std::ldexp(*x, static_cast<int>(std::clamp<std::int64_t>(toInteger(*e), -4096, 4096)))));
    });
    add(vm, "sqrt", unary("sqrt", [](double x) { return std::sqrt(x); }));
    // min and max over one or more numbers.
    for (const bool isMax : {false, true}) {
        const std::string_view name = isMax ? "max" : "min";
        add(vm, name, [isMax, name = std::string(name)](std::span<const Value> args) -> Results {
            auto best = checkNumber(args, 0, name);
            if (!best) {
                return std::unexpected(std::move(best.error()));
            }
            for (std::size_t i = 1; i < args.size(); ++i) {
                auto x = checkNumber(args, i, name);
                if (!x) {
                    return std::unexpected(std::move(x.error()));
                }
                if (isMax ? *x > *best : *x < *best) {
                    best = *x;
                }
            }
            return one(Value(*best));
        });
    }
    add(vm, "log", unary("log", [](double x) { return std::log(x); }));
    add(vm, "log10", unary("log10", [](double x) { return std::log10(x); }));
    add(vm, "exp", unary("exp", [](double x) { return std::exp(x); }));
    add(vm, "deg", unary("deg", [](double x) { return x / kRadiansPerDegree; }));
    add(vm, "rad", unary("rad", [](double x) { return x * kRadiansPerDegree; }));
    auto random = std::make_shared<RandomState>();
    add(vm, "random", [random](std::span<const Value> args) -> Results {
        // random() in [0, 1); random(m) in [1, m]; random(m, n) in [m, n], whole numbers.
        const double r = random->next();
        if (args.empty()) {
            return one(Value(r));
        }
        auto low = checkNumber(args, 0, "random");
        if (!low) {
            return std::unexpected(std::move(low.error()));
        }
        double lowest = 1.0;
        double highest = *low;
        if (args.size() > 1) {
            auto high = checkNumber(args, 1, "random");
            if (!high) {
                return std::unexpected(std::move(high.error()));
            }
            lowest = *low;
            highest = *high;
        }
        if (lowest > highest) {
            return fail(ErrorCode::Invalid, "Lua: bad argument to `random' (interval is empty)");
        }
        return one(Value(std::floor(r * (highest - lowest + 1)) + lowest));
    });
    add(vm, "randomseed", [random](std::span<const Value> args) -> Results {
        auto seed = checkNumber(args, 0, "randomseed");
        if (!seed) {
            return std::unexpected(std::move(seed.error()));
        }
        // xorshift needs a state other than 0.
        random->state = static_cast<std::uint64_t>(toInteger(*seed)) | 1U;
        return none();
    });
    vm.setGlobal("PI", Value(kPi));
}

} // namespace coney::script
