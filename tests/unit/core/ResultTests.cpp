#include "core/Result.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>

using af::core::ErrorCode;
using af::core::makeError;
using af::core::Result;

namespace
{

Result<int> parsePositive(int value)
{
    if (value <= 0)
    {
        return makeError(ErrorCode::invalidArgument, "Value must be positive", "value");
    }
    return value;
}

Result<void> check(bool good)
{
    if (!good)
    {
        return makeError(ErrorCode::invalidData, "Check failed");
    }
    return {};
}

} // namespace

TEST_CASE("Result holds a value on success", "[core][result]")
{
    const auto result = parsePositive(5);
    REQUIRE(result.ok());
    REQUIRE(static_cast<bool>(result));
    REQUIRE(result.value() == 5);
    REQUIRE(result.valueOr(0) == 5);
}

TEST_CASE("Result holds an error on failure", "[core][result]")
{
    const auto result = parsePositive(-1);
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().code == ErrorCode::invalidArgument);
    REQUIRE(result.error().subject == "value");
    REQUIRE(result.valueOr(42) == 42);
    REQUIRE(result.error().describe() == "invalidArgument: Value must be positive [value]");
}

TEST_CASE("Result supports move-only values", "[core][result]")
{
    Result<std::unique_ptr<int>> result{std::make_unique<int>(7)};
    REQUIRE(result.ok());
    auto owned = std::move(result).value();
    REQUIRE(*owned == 7);
}

TEST_CASE("Result<void>", "[core][result]")
{
    REQUIRE(check(true).ok());
    const auto failed = check(false);
    REQUIRE_FALSE(failed.ok());
    REQUIRE(failed.error().code == ErrorCode::invalidData);
    REQUIRE(failed.error().describe() == "invalidData: Check failed");
}

TEST_CASE("Every error code has a name", "[core][result]")
{
    for (const auto code : {ErrorCode::invalidArgument, ErrorCode::invalidData,
                            ErrorCode::unsupported, ErrorCode::notFound, ErrorCode::ioError,
                            ErrorCode::outOfMemory, ErrorCode::cancelled, ErrorCode::internal})
    {
        REQUIRE(af::core::toString(code) != "unknown");
    }
}
