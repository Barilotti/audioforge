#include "core/rt/ParameterBank.h"
#include "core/rt/TripleBuffer.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <map>
#include <thread>

using af::core::rt::ParameterBank;
using af::core::rt::TripleBuffer;

TEST_CASE("ParameterBank coalesces changes to the latest value per slot", "[core][rt]")
{
    ParameterBank<128> bank;
    for (int i = 0; i < 1000; ++i)
    {
        bank.set(3, static_cast<float>(i));
    }
    bank.set(70, 0.5f);
    bank.set(127, -1.0f);
    bank.set(128, 9.0f); // out of range: ignored

    std::map<std::uint32_t, float> changes;
    bank.consumeChanges([&](std::uint32_t slot, float value) { changes[slot] = value; });
    REQUIRE(changes.size() == 3);
    REQUIRE(changes[3] == 999.0f);
    REQUIRE(changes[70] == 0.5f);
    REQUIRE(changes[127] == -1.0f);

    int calls = 0;
    bank.consumeChanges([&](std::uint32_t, float) { ++calls; });
    REQUIRE(calls == 0); // nothing changed since the last call
    REQUIRE(bank.get(3) == 999.0f);
}

TEST_CASE("ParameterBank never loses the final value under concurrent writes", "[core][rt][stress]")
{
    ParameterBank<64> bank;
    constexpr int writes = 500'000;
    float seen = -1.0f;
    {
        std::jthread writer{[&bank]
                            {
                                for (int i = 0; i <= writes; ++i)
                                {
                                    bank.set(5, static_cast<float>(i));
                                }
                            }};
        while (seen < static_cast<float>(writes))
        {
            bank.consumeChanges(
                [&](std::uint32_t, float value)
                {
                    REQUIRE(value >= seen); // values only move forward
                    seen = value;
                });
        }
    }
    REQUIRE(seen == static_cast<float>(writes));
}

namespace
{
struct Snapshot
{
    std::uint64_t sequence = 0;
    std::uint64_t checksum = 0; // sequence * 3: a torn read would break the relation
};
} // namespace

TEST_CASE("TripleBuffer returns the latest snapshot and flags freshness", "[core][rt]")
{
    TripleBuffer<Snapshot> buffer;
    Snapshot out;
    REQUIRE_FALSE(buffer.read(out));
    buffer.write({1, 3});
    buffer.write({2, 6});
    REQUIRE(buffer.read(out));
    REQUIRE(out.sequence == 2);
    REQUIRE_FALSE(buffer.read(out)); // nothing new
    REQUIRE(out.sequence == 2);
}

TEST_CASE("TripleBuffer never tears snapshots across threads", "[core][rt][stress]")
{
    TripleBuffer<Snapshot> buffer;
    constexpr std::uint64_t last = 1'000'000;
    std::jthread writer{[&buffer]
                        {
                            for (std::uint64_t i = 1; i <= last; ++i)
                            {
                                buffer.write({i, i * 3});
                            }
                        }};

    Snapshot out;
    std::uint64_t previous = 0;
    while (previous < last)
    {
        buffer.read(out);
        REQUIRE(out.checksum == out.sequence * 3);
        REQUIRE(out.sequence >= previous);
        previous = out.sequence;
    }
}
