#pragma once

/**
 * @file expected.h
 * @brief C++17 compatible expected type implementation
 *
 * This header provides a std::expected-like type that works with C++17.
 * When compiled with C++23 or later, it will use std::expected directly.
 *
 * Usage:
 *   expected<int, std::string> foo() {
 *       return 42;  // success
 *   }
 *
 *   expected<int, std::string> bar() {
 *       return unexpected("error message");  // error
 *   }
 *
 *   auto result = foo();
 *   if (result.has_value()) {
 *       std::cout << result.value() << std::endl;
 *   }
 */

#include <variant>
#include <optional>
#include <type_traits>
#include <utility>
#include <stdexcept>
#include <functional>

#if __cplusplus >= 202302L && __has_include(<expected>)
    // C++23: Use standard library expected
    #include <expected>
    #define USE_STD_EXPECTED 1
#else
    #define USE_STD_EXPECTED 0
#endif

namespace common {
namespace utils {

#if USE_STD_EXPECTED

// C++23: Just alias std types
template<typename T, typename E>
using expected = std::expected<T, E>;

template<typename E>
using unexpected = std::unexpected<E>;

using unexpect_t = std::unexpect_t;
inline constexpr unexpect_t unexpect{};

#else

// C++17: Custom implementation

/**
 * @brief Unexpected type wrapper for errors
 */
template<typename E>
class unexpected {
public:
    static_assert(!std::is_same_v<E, void>, "E must not be void");

    constexpr unexpected(const unexpected&) = default;
    constexpr unexpected(unexpected&&) = default;

    template<typename Err = E,
             std::enable_if_t<!std::is_same_v<std::decay_t<Err>, unexpected>, int> = 0,
             std::enable_if_t<std::is_constructible_v<E, Err>, int> = 0>
    constexpr explicit unexpected(Err&& e)
        : error_(std::forward<Err>(e)) {}

    constexpr unexpected& operator=(const unexpected&) = default;
    constexpr unexpected& operator=(unexpected&&) = default;

    constexpr const E& error() const& noexcept { return error_; }
    constexpr E& error() & noexcept { return error_; }
    constexpr const E&& error() const&& noexcept { return std::move(error_); }
    constexpr E&& error() && noexcept { return std::move(error_); }

    constexpr void swap(unexpected& other) noexcept(std::is_nothrow_swappable_v<E>) {
        using std::swap;
        swap(error_, other.error_);
    }

private:
    E error_;
};

template<typename E>
unexpected(E) -> unexpected<E>;

/**
 * @brief Tag type for in-place error construction
 */
struct unexpect_t {
    explicit unexpect_t() = default;
};

inline constexpr unexpect_t unexpect{};

/**
 * @brief C++17 compatible expected type
 *
 * A type that either contains a value of type T or an error of type E.
 * Similar to Rust's Result<T, E> or Haskell's Either E T.
 */
template<typename T, typename E>
class expected {
public:
    static_assert(!std::is_same_v<T, void>, "T must not be void (use expected<void, E> specialization)");
    static_assert(!std::is_same_v<E, void>, "E must not be void");
    static_assert(!std::is_same_v<E, unexpected<T>>, "E must not be unexpected<T>");

    // ==================== Constructors ====================

    // Default constructor (only if T is default constructible)
    template<typename U = T,
             std::enable_if_t<std::is_default_constructible_v<U>, int> = 0>
    constexpr expected() : data_(std::in_place_index<0>) {}

    // Copy constructor
    constexpr expected(const expected&) = default;

    // Move constructor
    constexpr expected(expected&&) = default;

    // Constructor from value
    template<typename U = T,
             std::enable_if_t<!std::is_same_v<std::decay_t<U>, expected>, int> = 0,
             std::enable_if_t<!std::is_same_v<std::decay_t<U>, unexpected<E>>, int> = 0,
             std::enable_if_t<std::is_constructible_v<T, U>, int> = 0>
    constexpr expected(U&& value)
        : data_(std::in_place_index<0>, std::forward<U>(value)) {}

    // Constructor from unexpected (error)
    template<typename Err = E,
             std::enable_if_t<std::is_constructible_v<E, Err>, int> = 0>
    constexpr expected(unexpected<Err>&& e)
        : data_(std::in_place_index<1>, std::move(e).error()) {}

    template<typename Err = E,
             std::enable_if_t<std::is_constructible_v<E, Err>, int> = 0>
    constexpr expected(const unexpected<Err>& e)
        : data_(std::in_place_index<1>, e.error()) {}

    // In-place constructor for value
    template<typename... Args,
             std::enable_if_t<std::is_constructible_v<T, Args...>, int> = 0>
    constexpr explicit expected(std::in_place_t, Args&&... args)
        : data_(std::in_place_index<0>, std::forward<Args>(args)...) {}

    // In-place constructor for error
    template<typename... Args,
             std::enable_if_t<std::is_constructible_v<E, Args...>, int> = 0>
    constexpr explicit expected(unexpect_t, Args&&... args)
        : data_(std::in_place_index<1>, std::forward<Args>(args)...) {}

    // ==================== Assignment ====================

    constexpr expected& operator=(const expected&) = default;
    constexpr expected& operator=(expected&&) = default;

    template<typename U = T,
             std::enable_if_t<!std::is_same_v<expected, std::decay_t<U>>, int> = 0,
             std::enable_if_t<std::is_constructible_v<T, U>, int> = 0,
             std::enable_if_t<std::is_assignable_v<T&, U>, int> = 0>
    constexpr expected& operator=(U&& value) {
        data_.template emplace<0>(std::forward<U>(value));
        return *this;
    }

    template<typename Err = E,
             std::enable_if_t<std::is_constructible_v<E, Err>, int> = 0,
             std::enable_if_t<std::is_assignable_v<E&, Err>, int> = 0>
    constexpr expected& operator=(const unexpected<Err>& e) {
        data_.template emplace<1>(e.error());
        return *this;
    }

    template<typename Err = E,
             std::enable_if_t<std::is_constructible_v<E, Err>, int> = 0,
             std::enable_if_t<std::is_assignable_v<E&, Err>, int> = 0>
    constexpr expected& operator=(unexpected<Err>&& e) {
        data_.template emplace<1>(std::move(e).error());
        return *this;
    }

    // ==================== Observers ====================

    constexpr bool has_value() const noexcept {
        return data_.index() == 0;
    }

    constexpr explicit operator bool() const noexcept {
        return has_value();
    }

    // Value access
    constexpr T& value() & {
        if (!has_value()) {
            throw std::runtime_error("bad_expected_access");
        }
        return std::get<0>(data_);
    }

    constexpr const T& value() const& {
        if (!has_value()) {
            throw std::runtime_error("bad_expected_access");
        }
        return std::get<0>(data_);
    }

    constexpr T&& value() && {
        if (!has_value()) {
            throw std::runtime_error("bad_expected_access");
        }
        return std::move(std::get<0>(data_));
    }

    constexpr const T&& value() const&& {
        if (!has_value()) {
            throw std::runtime_error("bad_expected_access");
        }
        return std::move(std::get<0>(data_));
    }

    // Error access
    constexpr E& error() & {
        if (has_value()) {
            throw std::runtime_error("bad_expected_access");
        }
        return std::get<1>(data_);
    }

    constexpr const E& error() const& {
        if (has_value()) {
            throw std::runtime_error("bad_expected_access");
        }
        return std::get<1>(data_);
    }

    constexpr E&& error() && {
        if (has_value()) {
            throw std::runtime_error("bad_expected_access");
        }
        return std::move(std::get<1>(data_));
    }

    constexpr const E&& error() const&& {
        if (has_value()) {
            throw std::runtime_error("bad_expected_access");
        }
        return std::move(std::get<1>(data_));
    }

    // Dereference operators
    constexpr T& operator*() & noexcept {
        return std::get<0>(data_);
    }

    constexpr const T& operator*() const& noexcept {
        return std::get<0>(data_);
    }

    constexpr T&& operator*() && noexcept {
        return std::move(std::get<0>(data_));
    }

    constexpr const T&& operator*() const&& noexcept {
        return std::move(std::get<0>(data_));
    }

    constexpr T* operator->() noexcept {
        return &std::get<0>(data_);
    }

    constexpr const T* operator->() const noexcept {
        return &std::get<0>(data_);
    }

    // ==================== Monadic operations ====================

    template<typename F,
             typename U = std::invoke_result_t<F, T&>,
             std::enable_if_t<!std::is_same_v<U, void>, int> = 0>
    constexpr auto and_then(F&& f) & -> expected<U, E> {
        if (has_value()) {
            return std::invoke(std::forward<F>(f), **this);
        }
        return unexpected<E>(error());
    }

    template<typename F,
             typename U = std::invoke_result_t<F, const T&>,
             std::enable_if_t<!std::is_same_v<U, void>, int> = 0>
    constexpr auto and_then(F&& f) const& -> expected<U, E> {
        if (has_value()) {
            return std::invoke(std::forward<F>(f), **this);
        }
        return unexpected<E>(error());
    }

    template<typename F,
             typename U = std::invoke_result_t<F, T&&>,
             std::enable_if_t<!std::is_same_v<U, void>, int> = 0>
    constexpr auto and_then(F&& f) && -> expected<U, E> {
        if (has_value()) {
            return std::invoke(std::forward<F>(f), std::move(**this));
        }
        return unexpected<E>(std::move(error()));
    }

    template<typename F,
             typename G = std::invoke_result_t<F, E&>,
             std::enable_if_t<!std::is_same_v<G, void>, int> = 0>
    constexpr auto or_else(F&& f) & -> expected<T, G> {
        if (has_value()) {
            return **this;
        }
        return std::invoke(std::forward<F>(f), error());
    }

    template<typename F,
             typename G = std::invoke_result_t<F, const E&>,
             std::enable_if_t<!std::is_same_v<G, void>, int> = 0>
    constexpr auto or_else(F&& f) const& -> expected<T, G> {
        if (has_value()) {
            return **this;
        }
        return std::invoke(std::forward<F>(f), error());
    }

    template<typename F,
             typename G = std::invoke_result_t<F, E&&>,
             std::enable_if_t<!std::is_same_v<G, void>, int> = 0>
    constexpr auto or_else(F&& f) && -> expected<T, G> {
        if (has_value()) {
            return std::move(**this);
        }
        return std::invoke(std::forward<F>(f), std::move(error()));
    }

    // ==================== Value or ====================

    template<typename U = T,
             std::enable_if_t<std::is_convertible_v<U, T>, int> = 0>
    constexpr T value_or(U&& default_value) const& {
        return has_value() ? **this : static_cast<T>(std::forward<U>(default_value));
    }

    template<typename U = T,
             std::enable_if_t<std::is_convertible_v<U, T>, int> = 0>
    constexpr T value_or(U&& default_value) && {
        return has_value() ? std::move(**this) : static_cast<T>(std::forward<U>(default_value));
    }

    // ==================== Swap ====================

    constexpr void swap(expected& other)
        noexcept(std::is_nothrow_swappable_v<T> && std::is_nothrow_swappable_v<E>) {
        data_.swap(other.data_);
    }

private:
    std::variant<T, E> data_;
};

// ==================== Specialization for void T ====================

template<typename E>
class expected<void, E> {
public:
    static_assert(!std::is_same_v<E, void>, "E must not be void");

    // Default constructor (success state)
    constexpr expected() : has_value_(true) {}

    // Copy/Move constructors
    constexpr expected(const expected&) = default;
    constexpr expected(expected&&) = default;

    // Constructor from unexpected (error)
    template<typename Err = E,
             std::enable_if_t<std::is_constructible_v<E, Err>, int> = 0>
    constexpr expected(unexpected<Err>&& e)
        : has_value_(false), error_(std::move(e).error()) {}

    template<typename Err = E,
             std::enable_if_t<std::is_constructible_v<E, Err>, int> = 0>
    constexpr expected(const unexpected<Err>& e)
        : has_value_(false), error_(e.error()) {}

    // In-place constructor for error
    template<typename... Args,
             std::enable_if_t<std::is_constructible_v<E, Args...>, int> = 0>
    constexpr explicit expected(unexpect_t, Args&&... args)
        : has_value_(false), error_(std::forward<Args>(args)...) {}

    // Assignment
    constexpr expected& operator=(const expected&) = default;
    constexpr expected& operator=(expected&&) = default;

    // Observers
    constexpr bool has_value() const noexcept { return has_value_; }
    constexpr explicit operator bool() const noexcept { return has_value_; }

    constexpr E& error() & {
        if (has_value_) throw std::runtime_error("bad_expected_access");
        return error_;
    }

    constexpr const E& error() const& {
        if (has_value_) throw std::runtime_error("bad_expected_access");
        return error_;
    }

    constexpr E&& error() && {
        if (has_value_) throw std::runtime_error("bad_expected_access");
        return std::move(error_);
    }

    constexpr const E&& error() const&& {
        if (has_value_) throw std::runtime_error("bad_expected_access");
        return std::move(error_);
    }

private:
    bool has_value_;
    E error_;
};

// ==================== Comparison operators ====================

template<typename T1, typename E1, typename T2, typename E2>
constexpr bool operator==(const expected<T1, E1>& lhs, const expected<T2, E2>& rhs) {
    if (lhs.has_value() != rhs.has_value()) return false;
    if (lhs.has_value()) {
        return *lhs == *rhs;
    }
    return lhs.error() == rhs.error();
}

template<typename T1, typename E1, typename T2, typename E2>
constexpr bool operator!=(const expected<T1, E1>& lhs, const expected<T2, E2>& rhs) {
    return !(lhs == rhs);
}

#endif // USE_STD_EXPECTED

} // namespace utils
} // namespace common

// Provide std namespace aliases for drop-in replacement
// Note: Extending std namespace is technically UB but widely supported
#if USE_STD_EXPECTED
    // Already using std types
#else
    // 只注入 expected 到 std 命名空间
    // 注意: 不注入 unexpected，因为 C++17 中 std::unexpected 是一个函数
    namespace std {
        // Import our types into std namespace for compatibility
        template<typename T, typename E>
        using expected = common::utils::expected<T, E>;

        // 注意: 不注入 unexpected，避免与 std::unexpected() 函数冲突
        // 使用 common::utils::unexpected<E> 代替
    }

    // 提供便捷的命名空间别名
    namespace common {
        namespace utils {
            // 便捷访问
        }
    }
#endif
