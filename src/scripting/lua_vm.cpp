// SPDX-License-Identifier: GPL-3.0-or-later
#include "scripting/lua_vm.h"

#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "core/assert.h"

namespace coney::script {

namespace {

// The Lua 4.0 instruction set, in its numbering (the order of Lua 4.0's opcode list).
enum class Op : std::uint8_t {
    End,
    Return,
    Call,
    TailCall,
    PushNil,
    Pop,
    PushInt,
    PushString,
    PushNum,
    PushNegNum,
    PushUpvalue,
    GetLocal,
    GetGlobal,
    GetTable,
    GetDotted,
    GetIndexed,
    PushSelf,
    CreateTable,
    SetLocal,
    SetGlobal,
    SetTable,
    SetList,
    SetMap,
    Add,
    AddI,
    Sub,
    Mult,
    Div,
    Pow,
    Concat,
    Minus,
    Not,
    JmpNe,
    JmpEq,
    JmpLt,
    JmpLe,
    JmpGt,
    JmpGe,
    JmpT,
    JmpF,
    JmpOnT,
    JmpOnF,
    Jmp,
    PushNilJmp,
    ForPrep,
    ForLoop,
    LForPrep,
    LForLoop,
    Closure,
};

// Lua 4.0's instruction layout: a 6-bit opcode in the low bits; above it either one 26-bit operand U (S when read
// signed, with an excess of kMaxArgS), or a 9-bit B operand and a 17-bit A operand.
constexpr std::uint32_t kOpBits = 6;
constexpr std::uint32_t kBBits = 9;
constexpr std::int64_t kMaxArgS = ((std::int64_t{1} << 26) - 1) >> 1;
// The B operand of CALL that asks for every result.
constexpr std::uint32_t kMultipleResults = 255;
// How many list items one SETLIST stores at most; its A operand counts blocks of this size. The game's build flushes
// every 62 items, not stock Lua 4.0's 64 (`luaV_execute`, 0x00334478, multiplies A by 0x3e; the largest B on the disc
// is 62): with 64, every list item past the 62nd lands at the wrong index (docs/research/scripting.md, "Notes for
// implementers").
constexpr std::uint32_t kFieldsPerFlush = 62;

// The operands of one instruction.
struct Instruction {
    Op op;
    std::uint32_t u; // unsigned argument
    std::int64_t s;  // signed argument
    std::uint32_t a;
    std::uint32_t b;
};

// Splits an instruction word into its opcode and operands.
Instruction decode(std::uint32_t word) {
    const std::uint32_t u = word >> kOpBits;
    return Instruction{static_cast<Op>(word & ((1U << kOpBits) - 1)), u, static_cast<std::int64_t>(u) - kMaxArgS,
                       word >> (kOpBits + kBBits), u & ((1U << kBBits) - 1)};
}

// A type's name for error messages.
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
    return "?";
}

// A number as Lua 4.0 writes it into a string ("%.16g").
std::string numberText(double number) { return std::format("{:.16g}", number); }

// Lua's `<` for two numbers or two strings (strings by byte, where Lua 4.0 uses the C locale's collation).
std::optional<bool> lessThan(const Value& x, const Value& y) {
    // Each accessor returns a fresh optional, so bind them before testing and reading.
    const std::optional<double> xn = x.number();
    const std::optional<double> yn = y.number();
    if (xn && yn) {
        return *xn < *yn;
    }
    const std::optional<std::string_view> xs = x.string();
    const std::optional<std::string_view> ys = y.string();
    if (xs && ys) {
        return *xs < *ys;
    }
    return std::nullopt;
}

// A value as an arithmetic operand: a number, or a string that reads as one (Lua 4.0 converts numeric strings in
// arithmetic, as `tonumber` does).
std::optional<double> arithmeticOperand(const Value& value) {
    if (const std::optional<double> number = value.number()) {
        return number;
    }
    if (const std::optional<std::string_view> text = value.string()) {
        return parseLuaNumber(*text);
    }
    return std::nullopt;
}

// Applies an arithmetic instruction to two numbers.
double arithmetic(Op op, double x, double y) {
    switch (op) {
    case Op::Add:
        return x + y;
    case Op::Sub:
        return x - y;
    case Op::Mult:
        return x * y;
    case Op::Div:
        return x / y;
    default:
        return std::pow(x, y);
    }
}

// Sets a key that cannot fail (a string, or a number that is not NaN), which keeps the callers free of error paths
// that cannot happen.
void setKnownKey(Table& table, const Value& key, Value value) {
    const bool stored = table.set(key, std::move(value)).has_value();
    CONEY_ASSERT(stored);
}

// The value stack of one Lua function call, with every access checked: bad bytecode must give an error, never a
// crash. Index 0 is the call's base (its first parameter).
class Frame {
  public:
    std::vector<Value> stack;
    // Where GETGLOBAL pushed an unset global, and its name: a later call of that slot names the global it calls.
    std::vector<std::pair<std::size_t, const std::string*>> unsetGlobals;

    // The name of the unset global last pushed into stack slot `slot`; null when none was.
    [[nodiscard]] const std::string* unsetGlobalAt(std::size_t slot) const {
        for (auto it = unsetGlobals.rbegin(); it != unsetGlobals.rend(); ++it) {
            if (it->first == slot) {
                return it->second;
            }
        }
        return nullptr;
    }

    // True when at least `n` values are on the stack.
    [[nodiscard]] bool has(std::size_t n) const { return stack.size() >= n; }
    // Removes and returns the top value; the caller has checked has(1).
    Value pop() {
        Value top = std::move(stack.back());
        stack.pop_back();
        return top;
    }
    // Removes `n` values; the caller has checked has(n).
    void drop(std::size_t n) { stack.resize(stack.size() - n); }
    // The value `n` places below the top (1 is the top); the caller has checked has(n).
    Value& fromTop(std::size_t n) { return stack[stack.size() - n]; }
};

} // namespace

LuaVm::LuaVm(LuaVmOptions options) : m_options(options), m_globals(std::make_shared<Table>()) {}

void LuaVm::setGlobal(std::string_view name, Value value) {
    setKnownKey(*m_globals, Value(std::string(name)), std::move(value));
}

void LuaVm::registerFunction(std::string_view name, NativeFunction function) {
    auto wrapped = std::make_shared<Function>();
    wrapped->native = std::move(function);
    setGlobal(name, Value(std::shared_ptr<const Function>(std::move(wrapped))));
}

std::expected<std::vector<Value>, Error> LuaVm::run(std::shared_ptr<const LuaProto> chunk) {
    auto main = std::make_shared<Function>();
    main->proto = chunk.get();
    main->chunk = std::move(chunk);
    return call(Value(std::shared_ptr<const Function>(std::move(main))), {});
}

std::expected<std::vector<Value>, Error> LuaVm::call(const Value& function, std::span<const Value> args) {
    // The instruction budget counts from the outermost call; a binding that runs another script shares it.
    if (m_depth == 0) {
        m_budgetStart = m_instructions;
    }
    return callNested(function, args);
}

std::expected<std::vector<Value>, Error> LuaVm::callNested(const Value& function, std::span<const Value> args) {
    if (function.isNil() && m_options.nilCallsAreNoOps) {
        ++m_nilCalls;
        ++m_nilCallsByName[m_calleeName];
        return std::vector<Value>{};
    }
    const std::shared_ptr<const Function>& target = function.function();
    if (!target) {
        // Name the likely culprit: calling nil is nearly always a global that is not set (a binding Coney lacks).
        const std::string hint =
            function.isNil() && !m_calleeName.empty() ? std::format(" (global `{}`)", m_calleeName) : std::string();
        return fail(ErrorCode::Invalid, std::format("Lua: attempt to call a {} value{}", typeName(function), hint));
    }
    if (m_depth >= m_options.maxCallDepth) {
        return fail(ErrorCode::Invalid, "Lua: calls nested too deeply");
    }
    // Track the depth across bindings too, so a binding that runs a script nests inside this call.
    ++m_depth;
    auto results = target->native ? target->native(args) : execute(*target, args);
    --m_depth;
    return results;
}

// One Lua function call: the bytecode interpreter. Each case follows the semantics of the Lua 4.0 instruction of the
// same name; the comments give only what the instruction does to the stack.
// NOLINTNEXTLINE(readability-function-cognitive-complexity): one switch over the instruction set reads best whole
std::expected<std::vector<Value>, Error> LuaVm::execute(const Function& closure, std::span<const Value> args) {
    const LuaProto& proto = *closure.proto;
    Frame frame;
    // The prologue: the fixed parameters (missing ones nil), then for a vararg function a table `arg` of the rest with
    // its count in `n`.
    const auto numParams = static_cast<std::size_t>(proto.numParams);
    frame.stack.reserve(static_cast<std::size_t>(proto.maxStackSize) + numParams + 1);
    for (std::size_t i = 0; i < numParams; ++i) {
        frame.stack.push_back(i < args.size() ? args[i] : Value());
    }
    if (proto.isVararg) {
        auto extra = std::make_shared<Table>();
        double count = 0;
        for (std::size_t i = numParams; i < args.size(); ++i) {
            count += 1;
            setKnownKey(*extra, Value(count), args[i]);
        }
        setKnownKey(*extra, Value(std::string("n")), Value(count));
        frame.stack.emplace_back(std::move(extra));
    }

    std::size_t pc = 0;
    // Errors carry the opcode and its position, so a failing script can be found.
    const auto error = [&pc](std::string_view what) {
        return fail(ErrorCode::Invalid, std::format("Lua: {} (instruction {})", what, pc - 1));
    };
    const auto underflow = [&error] { return error("stack underflow (malformed bytecode)"); };
    // Moves the program counter by a jump's signed operand, relative to the next instruction.
    const auto jump = [&pc, &proto](std::int64_t offset) {
        const std::int64_t target = static_cast<std::int64_t>(pc) + offset;
        if (target < 0 || target > static_cast<std::int64_t>(proto.code.size())) {
            return false;
        }
        pc = static_cast<std::size_t>(target);
        return true;
    };
    // Fetches a string constant, or nothing when the operand is out of range.
    const auto constant = [&proto](std::uint32_t index) -> const std::string* {
        return index < proto.strings.size() ? &proto.strings[index] : nullptr;
    };

    while (true) {
        if (pc >= proto.code.size()) {
            return error("ran past the end of the code");
        }
        if (m_instructions - m_budgetStart >= m_options.maxInstructions) {
            return error("instruction limit reached");
        }
        ++m_instructions;
        const Instruction in = decode(proto.code[pc++]);
        switch (in.op) {
        case Op::End:
            return std::vector<Value>{};
        case Op::Return: // the values from local u to the top
            if (!frame.has(in.u)) {
                return underflow();
            }
            return std::vector<Value>(frame.stack.begin() + in.u, frame.stack.end());
        case Op::Call:
        case Op::TailCall: { // function at a, arguments above it; b results (or all) replace them
            if (!frame.has(in.a + 1)) {
                return underflow();
            }
            const Value function = frame.stack[in.a];
            // Name the callee when it is an unset global, for the nil-call count and the error message.
            if (const std::string* name = function.isNil() ? frame.unsetGlobalAt(in.a) : nullptr; name != nullptr) {
                m_calleeName = *name;
            } else {
                m_calleeName.clear();
            }
            const std::vector<Value> callArgs(frame.stack.begin() + in.a + 1, frame.stack.end());
            auto results = callNested(function, callArgs);
            if (!results) {
                return results;
            }
            frame.stack.resize(in.a);
            if (in.op == Op::TailCall) {
                // A tail call returns all its results, counted from local b.
                frame.stack.insert(frame.stack.end(), results->begin(), results->end());
                if (!frame.has(in.b)) {
                    return underflow();
                }
                return std::vector<Value>(frame.stack.begin() + in.b, frame.stack.end());
            }
            if (in.b != kMultipleResults) {
                results->resize(in.b);
            }
            frame.stack.insert(frame.stack.end(), results->begin(), results->end());
            break;
        }
        case Op::PushNil:
            frame.stack.resize(frame.stack.size() + in.u);
            break;
        case Op::Pop:
            if (!frame.has(in.u)) {
                return underflow();
            }
            frame.drop(in.u);
            break;
        case Op::PushInt:
            frame.stack.emplace_back(static_cast<double>(in.s));
            break;
        case Op::PushString: {
            const std::string* text = constant(in.u);
            if (text == nullptr) {
                return error("string constant out of range");
            }
            frame.stack.emplace_back(*text);
            break;
        }
        case Op::PushNum:
        case Op::PushNegNum: {
            if (in.u >= proto.numbers.size()) {
                return error("number constant out of range");
            }
            const double number = proto.numbers[in.u];
            frame.stack.emplace_back(in.op == Op::PushNum ? number : -number);
            break;
        }
        case Op::PushUpvalue:
            if (in.u >= closure.upvalues.size()) {
                return error("upvalue out of range");
            }
            frame.stack.push_back(closure.upvalues[in.u]);
            break;
        case Op::GetLocal:
            if (!frame.has(in.u + 1)) {
                return underflow();
            }
            frame.stack.push_back(frame.stack[in.u]);
            break;
        case Op::GetGlobal: {
            const std::string* name = constant(in.u);
            if (name == nullptr) {
                return error("string constant out of range");
            }
            frame.stack.push_back(m_globals->field(*name));
            if (frame.stack.back().isNil()) {
                frame.unsetGlobals.emplace_back(frame.stack.size() - 1, name);
            }
            break;
        }
        case Op::GetTable:
        case Op::GetDotted:
        case Op::GetIndexed:
        case Op::PushSelf: { // the table on the top, indexed by the key the opcode names
            const std::size_t needed = in.op == Op::GetTable ? 2 : 1;
            if (!frame.has(needed)) {
                return underflow();
            }
            Value key;
            if (in.op == Op::GetTable) {
                key = frame.pop();
            } else if (in.op == Op::GetIndexed) {
                if (!frame.has(in.u + 1)) {
                    return underflow();
                }
                key = frame.stack[in.u];
            } else {
                const std::string* name = constant(in.u);
                if (name == nullptr) {
                    return error("string constant out of range");
                }
                key = Value(*name);
            }
            const Value object = frame.pop();
            if (!object.table()) {
                return error(std::format("attempt to index a {} value", typeName(object)));
            }
            frame.stack.push_back(object.table()->get(key));
            if (in.op == Op::PushSelf) {
                frame.stack.push_back(object); // the receiver above the method
            }
            break;
        }
        case Op::CreateTable:
            frame.stack.emplace_back(std::make_shared<Table>());
            break;
        case Op::SetLocal:
            if (!frame.has(in.u + 2)) {
                return underflow();
            }
            frame.stack[in.u] = frame.pop();
            break;
        case Op::SetGlobal: {
            const std::string* name = constant(in.u);
            if (name == nullptr) {
                return error("string constant out of range");
            }
            if (!frame.has(1)) {
                return underflow();
            }
            setGlobal(*name, frame.pop());
            break;
        }
        case Op::SetTable: { // t[k] = the top value, with t a places below the top; then b values are popped
            if (in.a < 2 || !frame.has(in.a) || !frame.has(in.b)) {
                return underflow();
            }
            const Value& object = frame.fromTop(in.a);
            if (!object.table()) {
                return error(std::format("attempt to index a {} value", typeName(object)));
            }
            if (auto set = object.table()->set(frame.fromTop(in.a - 1), frame.fromTop(1)); !set) {
                return error(set.error().message);
            }
            frame.drop(in.b);
            break;
        }
        case Op::SetList: { // the top b values into the table below them, at a × 62 + 1 onwards
            if (!frame.has(in.b + 1)) {
                return underflow();
            }
            const Value& object = frame.fromTop(in.b + 1);
            if (!object.table()) {
                return error("SETLIST without a table");
            }
            for (std::uint32_t i = 0; i < in.b; ++i) {
                const double index = static_cast<double>(in.a) * kFieldsPerFlush + i + 1;
                setKnownKey(*object.table(), Value(index), frame.fromTop(in.b - i));
            }
            frame.drop(in.b);
            break;
        }
        case Op::SetMap: { // the top u key-value pairs into the table below them
            if (!frame.has((std::size_t{in.u} * 2) + 1)) {
                return underflow();
            }
            const Value& object = frame.fromTop((std::size_t{in.u} * 2) + 1);
            if (!object.table()) {
                return error("SETMAP without a table");
            }
            for (std::size_t i = 0; i < in.u; ++i) {
                const std::size_t keyAt = (std::size_t{in.u} - i) * 2;
                if (auto set = object.table()->set(frame.fromTop(keyAt), frame.fromTop(keyAt - 1)); !set) {
                    return error(set.error().message);
                }
            }
            frame.drop(std::size_t{in.u} * 2);
            break;
        }
        case Op::Add:
        case Op::Sub:
        case Op::Mult:
        case Op::Div:
        case Op::Pow: {
            if (!frame.has(2)) {
                return underflow();
            }
            const Value y = frame.pop();
            const Value x = frame.pop();
            const std::optional<double> xn = arithmeticOperand(x);
            const std::optional<double> yn = arithmeticOperand(y);
            if (!xn || !yn) {
                return error(std::format("arithmetic on a {} and a {}", typeName(x), typeName(y)));
            }
            frame.stack.emplace_back(arithmetic(in.op, *xn, *yn));
            break;
        }
        case Op::AddI: {
            if (!frame.has(1)) {
                return underflow();
            }
            const std::optional<double> n = arithmeticOperand(frame.fromTop(1));
            if (!n) {
                return error(std::format("arithmetic on a {}", typeName(frame.fromTop(1))));
            }
            frame.fromTop(1) = Value(*n + static_cast<double>(in.s));
            break;
        }
        case Op::Concat: { // the top u values joined into one string
            if (in.u == 0 || !frame.has(in.u)) {
                return underflow();
            }
            std::string joined;
            for (std::size_t i = in.u; i > 0; --i) {
                const Value& part = frame.fromTop(i);
                if (const std::optional<std::string_view> s = part.string()) {
                    joined += *s;
                } else if (const std::optional<double> n = part.number()) {
                    joined += numberText(*n);
                } else {
                    return error(std::format("attempt to concatenate a {} value", typeName(part)));
                }
            }
            frame.drop(in.u);
            frame.stack.emplace_back(std::move(joined));
            break;
        }
        case Op::Minus: {
            if (!frame.has(1)) {
                return underflow();
            }
            const std::optional<double> n = arithmeticOperand(frame.fromTop(1));
            if (!n) {
                return error(std::format("arithmetic on a {}", typeName(frame.fromTop(1))));
            }
            frame.fromTop(1) = Value(-*n);
            break;
        }
        case Op::Not: // nil becomes 1, anything else nil
            if (!frame.has(1)) {
                return underflow();
            }
            frame.fromTop(1) = frame.fromTop(1).isNil() ? Value(1.0) : Value();
            break;
        case Op::JmpNe:
        case Op::JmpEq:
        case Op::JmpLt:
        case Op::JmpLe:
        case Op::JmpGt:
        case Op::JmpGe: { // compare x (pushed first) with y, pop both, jump when true
            if (!frame.has(2)) {
                return underflow();
            }
            const Value y = frame.pop();
            const Value x = frame.pop();
            bool taken = false;
            if (in.op == Op::JmpNe || in.op == Op::JmpEq) {
                taken = (x == y) == (in.op == Op::JmpEq);
            } else {
                const bool swap = in.op == Op::JmpLe || in.op == Op::JmpGt;
                const std::optional<bool> less = swap ? lessThan(y, x) : lessThan(x, y);
                if (!less) {
                    return error(std::format("attempt to compare a {} with a {}", typeName(x), typeName(y)));
                }
                // LE is not (y < x) and GE is not (x < y).
                taken = (in.op == Op::JmpLe || in.op == Op::JmpGe) ? !*less : *less;
            }
            if (taken && !jump(in.s)) {
                return error("jump out of range");
            }
            break;
        }
        case Op::JmpT:
        case Op::JmpF: { // pop x; jump when it is (not) nil
            if (!frame.has(1)) {
                return underflow();
            }
            const bool isNil = frame.pop().isNil();
            if (isNil == (in.op == Op::JmpF) && !jump(in.s)) {
                return error("jump out of range");
            }
            break;
        }
        case Op::JmpOnT:
        case Op::JmpOnF: { // keep x and jump when it is (not) nil, else pop it
            if (!frame.has(1)) {
                return underflow();
            }
            const bool isNil = frame.fromTop(1).isNil();
            if (isNil == (in.op == Op::JmpOnF)) {
                if (!jump(in.s)) {
                    return error("jump out of range");
                }
            } else {
                frame.drop(1);
            }
            break;
        }
        case Op::Jmp:
            if (!jump(in.s)) {
                return error("jump out of range");
            }
            break;
        case Op::PushNilJmp: // push nil and skip the next instruction
            frame.stack.emplace_back();
            if (!jump(1)) {
                return error("jump out of range");
            }
            break;
        case Op::ForPrep:
        case Op::ForLoop: { // numeric for: index, limit and step on the top
            if (!frame.has(3)) {
                return underflow();
            }
            Value& index = frame.fromTop(3);
            const std::optional<double> limit = frame.fromTop(2).number();
            const std::optional<double> step = frame.fromTop(1).number();
            const std::optional<double> start = index.number();
            if (!start || !limit || !step) {
                return error("`for' values must be numbers");
            }
            const double at = in.op == Op::ForLoop ? *start + *step : *start;
            if (in.op == Op::ForLoop) {
                index = Value(at);
            }
            const bool done = *step > 0 ? at > *limit : at < *limit;
            // FORPREP jumps past the loop when it is empty; FORLOOP jumps back while it is not done.
            if (done) {
                frame.drop(3);
            }
            if (done == (in.op == Op::ForPrep) && !jump(in.s)) {
                return error("jump out of range");
            }
            break;
        }
        case Op::LForPrep:
        case Op::LForLoop: { // `for k, v in t`: the table, then the current key and value
            const std::size_t tableAt = in.op == Op::LForPrep ? 1 : 3;
            if (!frame.has(tableAt)) {
                return underflow();
            }
            const std::shared_ptr<Table> table = frame.fromTop(tableAt).table();
            if (!table) {
                return error("`for' table must be a table");
            }
            const auto entry = table->next(in.op == Op::LForPrep ? Value() : frame.fromTop(2));
            if (!entry) {
                frame.drop(tableAt);
                if (in.op == Op::LForPrep && !jump(in.s)) {
                    return error("jump out of range");
                }
                break;
            }
            if (in.op == Op::LForPrep) {
                frame.stack.push_back(entry->first);
                frame.stack.push_back(entry->second);
            } else {
                frame.fromTop(2) = entry->first;
                frame.fromTop(1) = entry->second;
                if (!jump(in.s)) {
                    return error("jump out of range");
                }
            }
            break;
        }
        case Op::Closure: { // function a of this one, closing over the top b values
            if (in.a >= proto.protos.size()) {
                return error("function constant out of range");
            }
            if (!frame.has(in.b)) {
                return underflow();
            }
            auto made = std::make_shared<Function>();
            made->chunk = closure.chunk;
            made->proto = &proto.protos[in.a];
            made->upvalues.assign(frame.stack.end() - in.b, frame.stack.end());
            frame.drop(in.b);
            frame.stack.emplace_back(std::shared_ptr<const Function>(std::move(made)));
            break;
        }
        default:
            return error(std::format("unknown opcode {}", static_cast<int>(in.op)));
        }
    }
}

std::optional<double> parseLuaNumber(std::string_view text) {
    // Lua 4.0's rule: optional spaces, a decimal number as C's strtod reads it, optional spaces, nothing else.
    // std::from_chars reads neither a leading "+" nor spaces, so both are handled here.
    const auto isSpace = [](char c) { return c == ' ' || (c >= '\t' && c <= '\r'); };
    while (!text.empty() && isSpace(text.front())) {
        text.remove_prefix(1);
    }
    while (!text.empty() && isSpace(text.back())) {
        text.remove_suffix(1);
    }
    bool negative = false;
    if (!text.empty() && (text.front() == '-' || text.front() == '+')) {
        negative = text.front() == '-';
        text.remove_prefix(1);
    }
    if (text.empty() || text.front() == '-' || text.front() == '+') {
        return std::nullopt;
    }
    double number = 0.0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), number);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
        return std::nullopt;
    }
    return negative ? -number : number;
}

std::expected<std::shared_ptr<const LuaProto>, Error> loadLuaChunk(std::span<const std::byte> data) {
    auto proto = parseLuaChunk(data);
    if (!proto) {
        return std::unexpected(std::move(proto.error()));
    }
    return std::make_shared<const LuaProto>(std::move(*proto));
}

} // namespace coney::script
