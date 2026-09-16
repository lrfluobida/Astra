#pragma once

#include <cassert>
#include <type_traits>
#include <utility>

namespace astra {

struct Nullopt {};
const Nullopt nullopt = Nullopt();

// C++11 value holder for Astra's default-constructible observation/decision types.
// Presence is independent of the stored value: zero and empty strings are valid.
template <typename T>
class Optional {
public:
    Optional() : present_(false), value_() {}
    Optional(Nullopt) : present_(false), value_() {}

    template <typename U,
              typename std::enable_if<
                  !std::is_same<typename std::decay<U>::type, Optional<T>>::value &&
                  std::is_constructible<T, U&&>::value, int>::type = 0>
    Optional(U&& value) : present_(true), value_(std::forward<U>(value)) {}

    explicit operator bool() const { return present_; }
    bool has_value() const { return present_; }
    const T& operator*() const { assert(present_); return value_; }
    T& operator*() { assert(present_); return value_; }
    const T* operator->() const { assert(present_); return &value_; }
    T* operator->() { assert(present_); return &value_; }
    T value_or(const T& fallback) const { return present_ ? value_ : fallback; }
    void reset() { value_ = T(); present_ = false; }

private:
    bool present_;
    T value_;
};

template <typename T>
bool operator==(const Optional<T>& left, const Optional<T>& right) {
    return left.has_value() == right.has_value() && (!left || *left == *right);
}

template <typename T>
bool operator!=(const Optional<T>& left, const Optional<T>& right) {
    return !(left == right);
}

template <typename T, typename U>
typename std::enable_if<std::is_convertible<U, T>::value, bool>::type
operator==(const Optional<T>& left, const U& right) {
    return left && *left == right;
}

template <typename T, typename U>
typename std::enable_if<std::is_convertible<U, T>::value, bool>::type
operator==(const U& left, const Optional<T>& right) {
    return right && left == *right;
}

template <typename T, typename U>
typename std::enable_if<std::is_convertible<U, T>::value, bool>::type
operator!=(const Optional<T>& left, const U& right) {
    return !(left == right);
}

template <typename T, typename U>
typename std::enable_if<std::is_convertible<U, T>::value, bool>::type
operator!=(const U& left, const Optional<T>& right) {
    return !(left == right);
}

}  // namespace astra
