#pragma once

#include <utility>
#include <variant>

namespace coney {

// A value or an error. std::expected is not usable with every supported toolchain (Clang 18 on libstdc++ 13 does
// not provide it), so the engine carries this small stand-in.
template <typename E> struct Failure {
    E error;
};
template <typename E> Failure(E) -> Failure<E>;

template <typename T, typename E> class Result {
  public:
    Result(T value) : state_(std::in_place_index<0>, std::move(value)) {}
    template <typename F> Result(Failure<F> failure) : state_(std::in_place_index<1>, std::move(failure.error)) {}

    [[nodiscard]] bool has_value() const noexcept { return state_.index() == 0; }
    explicit operator bool() const noexcept { return has_value(); }

    [[nodiscard]] T& operator*() & { return std::get<0>(state_); }
    [[nodiscard]] const T& operator*() const& { return std::get<0>(state_); }
    [[nodiscard]] T* operator->() { return &std::get<0>(state_); }
    [[nodiscard]] const T* operator->() const { return &std::get<0>(state_); }
    [[nodiscard]] const E& error() const { return std::get<1>(state_); }

  private:
    std::variant<T, E> state_;
};

} // namespace coney
