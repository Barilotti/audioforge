#include "core/Uuid.h"

#include <catch2/catch_test_macros.hpp>

#include <set>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

using af::core::Uuid;

TEST_CASE("Default UUID is nil", "[core][uuid]")
{
    REQUIRE(Uuid{}.isNil());
    REQUIRE(Uuid{}.toString() == "00000000-0000-0000-0000-000000000000");
}

TEST_CASE("Generated UUIDs are version 7 with the RFC variant", "[core][uuid]")
{
    for (int i = 0; i < 1000; ++i)
    {
        const auto id = Uuid::generate();
        REQUIRE_FALSE(id.isNil());
        REQUIRE(id.version() == 7);
        REQUIRE((id.bytes()[8] & 0xC0) == 0x80);
    }
}

TEST_CASE("Generated UUIDs are unique across threads", "[core][uuid]")
{
    constexpr int threads = 4;
    constexpr int perThread = 10'000;
    std::vector<std::vector<Uuid>> results(threads);
    {
        std::vector<std::jthread> workers;
        workers.reserve(threads);
        for (int t = 0; t < threads; ++t)
        {
            workers.emplace_back(
                [&results, t]
                {
                    for (int i = 0; i < perThread; ++i)
                    {
                        results[static_cast<std::size_t>(t)].push_back(Uuid::generate());
                    }
                });
        }
    }
    std::unordered_set<Uuid> all;
    for (const auto& list : results)
    {
        all.insert(list.begin(), list.end());
    }
    REQUIRE(all.size() == threads * perThread);
}

TEST_CASE("UUID string round trip", "[core][uuid]")
{
    const auto id = Uuid::generate();
    const auto text = id.toString();
    REQUIRE(text.size() == 36);
    REQUIRE(Uuid::parse(text) == id);
}

TEST_CASE("UUID parsing accepts upper case and rejects malformed text", "[core][uuid]")
{
    const auto parsed = Uuid::parse("0193A5C2-7B1E-7C3D-8E4F-0123456789AB");
    REQUIRE(parsed.has_value());
    REQUIRE(parsed.value_or(Uuid{}).toString() == "0193a5c2-7b1e-7c3d-8e4f-0123456789ab");

    const std::vector<std::string> invalid{
        "",
        "0193a5c2-7b1e-7c3d-8e4f-0123456789a",   // too short
        "0193a5c2-7b1e-7c3d-8e4f-0123456789abc", // too long
        "0193a5c27b1e-7c3d-8e4f-0123456789ab-",  // dash misplaced
        "0193a5c2-7b1e-7c3d-8e4f-0123456789ag",  // not hex
        "{193a5c2-7b1e-7c3d-8e4f-0123456789ab}",
    };
    for (const auto& text : invalid)
    {
        CAPTURE(text);
        REQUIRE_FALSE(Uuid::parse(text).has_value());
    }
}

TEST_CASE("UUIDs generated later sort after earlier ones", "[core][uuid]")
{
    const auto first = Uuid::generate();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    const auto second = Uuid::generate();
    REQUIRE(first < second);
}
