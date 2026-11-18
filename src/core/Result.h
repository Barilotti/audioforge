#pragma once

#include <cassert>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

namespace af::core
{

/// Error categories. The UI maps the code (plus Error::subject) to a localised message, so the
/// code says what kind of failure it is, independent of the English diagnostic text.
enum class ErrorCode
{
    invalidArgument, ///< Caller passed a value that violates a precondition.
    invalidData,     ///< Malformed or inconsistent data (corrupt file, bad project JSON).
    unsupported,     ///< Valid but not supported (format, channel count, sample rate).
    notFound,        ///< File, asset or entity does not exist.
    ioError,         ///< Operating system I/O failure.
    outOfMemory,     ///< Allocation failed or a memory budget was exceeded.
    cancelled,       ///< The operation was cancelled by the user or by shutdown.
    internal,        ///< A bug: an invariant inside AudioForge was broken.
};

[[nodiscard]] std::string_view toString(ErrorCode code) noexcept;

/// An error value. `message` is English diagnostic text for logs and bug reports; `subject` names
/// what failed (a file path, a field name) and is shown to the user inside a localised message.
struct Error
{
    ErrorCode code = ErrorCode::internal;
    std::string message;
    std::string subject;

    /// "invalidData: Unexpected end of file [D:/SFX/hit.wav]"
    [[nodiscard]] std::string describe() const;
};

/// Either a value of type T or an Error. A minimal stand-in for C++23 std::expected; it will be
/// replaced by std::expected once the toolchains are on C++23.
template <typename T>
class [[nodiscard]] Result
{
    static_assert(!std::is_same_v<T, Error>, "Result<Error> is ambiguous");
    static_assert(!std::is_reference_v<T>, "Result does not hold references");

public:
    Result(T value) // NOLINT(google-explicit-constructor): implicit success, like std::expected
        : storage_(std::in_place_index<0>, std::move(value))
    {
    }

    Result(Error error) // NOLINT(google-explicit-constructor)
        : storage_(std::in_place_index<1>, std::move(error))
    {
    }

    [[nodiscard]] bool ok() const noexcept { return storage_.index() == 0; }
    explicit operator bool() const noexcept { return ok(); }

    [[nodiscard]] T& value() & noexcept
    {
        assert(ok());
        return *std::get_if<0>(&storage_);
    }

    [[nodiscard]] const T& value() const& noexcept
    {
        assert(ok());
        return *std::get_if<0>(&storage_);
    }

    [[nodiscard]] T&& value() && noexcept
    {
        assert(ok());
        return std::move(*std::get_if<0>(&storage_));
    }

    [[nodiscard]] const Error& error() const noexcept
    {
        assert(!ok());
        return *std::get_if<1>(&storage_);
    }

    template <typename U>
    [[nodiscard]] T valueOr(U&& fallback) const&
    {
        return ok() ? value() : static_cast<T>(std::forward<U>(fallback));
    }

private:
    std::variant<T, Error> storage_;
};

/// Result of an operation without a value.
template <>
class [[nodiscard]] Result<void>
{
public:
    Result() = default;

    Result(Error error) // NOLINT(google-explicit-constructor)
        : error_(std::move(error))
        , failed_(true)
    {
    }

    [[nodiscard]] bool ok() const noexcept { return !failed_; }
    explicit operator bool() const noexcept { return ok(); }

    [[nodiscard]] const Error& error() const noexcept
    {
        assert(failed_);
        return error_;
    }

private:
    Error error_;
    bool failed_ = false;
};

/// Shorthand for building errors: `return makeError(ErrorCode::notFound, "No such file", path);`
[[nodiscard]] inline Error makeError(ErrorCode code, std::string message, std::string subject = {})
{
    return Error{code, std::move(message), std::move(subject)};
}

} // namespace af::core
