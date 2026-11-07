// Minimal result type (C++20 has no std::expected).
#pragma once
#include <optional>
#include <string>
#include <utility>

namespace fulmetry {

struct Error {
    std::string code;     // stable, machine-readable, e.g. "not_found"
    std::string message;  // human-readable
};

template <class T>
class Result {
public:
    Result(T value) : value_(std::move(value)) {}
    Result(Error error) : error_(std::move(error)) {}
    bool ok() const { return value_.has_value(); }
    explicit operator bool() const { return ok(); }
    T& value() { return *value_; }
    const T& value() const { return *value_; }
    T* operator->() { return &*value_; }
    const T* operator->() const { return &*value_; }
    const Error& error() const { return *error_; }

private:
    std::optional<T> value_;
    std::optional<Error> error_;
};

struct Ok {};
using Status = Result<Ok>;

}  // namespace fulmetry
