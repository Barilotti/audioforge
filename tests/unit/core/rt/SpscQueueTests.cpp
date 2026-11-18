#include "core/rt/SpscQueue.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <thread>

using af::core::rt::SpscQueue;

TEST_CASE("SpscQueue preserves order and reports full and empty", "[core][rt]")
{
    SpscQueue<int, 4> queue;
    int out = 0;
    REQUIRE_FALSE(queue.tryPop(out));
    REQUIRE(queue.freeSlots() == 4);

    for (int i = 0; i < 4; ++i)
    {
        REQUIRE(queue.tryPush(i));
    }
    REQUIRE(queue.freeSlots() == 0);
    REQUIRE_FALSE(queue.tryPush(99)); // full: rejected, nothing overwritten

    for (int i = 0; i < 4; ++i)
    {
        REQUIRE(queue.tryPop(out));
        REQUIRE(out == i);
    }
    REQUIRE_FALSE(queue.tryPop(out));
    REQUIRE(queue.freeSlots() == 4);
}

TEST_CASE("SpscQueue wraps around many times", "[core][rt]")
{
    SpscQueue<std::uint64_t, 8> queue;
    std::uint64_t out = 0;
    for (std::uint64_t i = 0; i < 1000; ++i)
    {
        REQUIRE(queue.tryPush(i));
        REQUIRE(queue.tryPush(i + 1'000'000));
        REQUIRE(queue.tryPop(out));
        REQUIRE(out == i);
        REQUIRE(queue.tryPop(out));
        REQUIRE(out == i + 1'000'000);
    }
}

TEST_CASE("SpscQueue delivers every element exactly once across threads", "[core][rt][stress]")
{
    constexpr std::uint64_t count = 2'000'000;
    SpscQueue<std::uint64_t, 256> queue;

    std::jthread producer{[&queue]
                          {
                              for (std::uint64_t i = 1; i <= count;)
                              {
                                  if (queue.tryPush(i))
                                  {
                                      ++i;
                                  }
                              }
                          }};

    std::uint64_t expected = 1;
    std::uint64_t sum = 0;
    std::uint64_t value = 0;
    while (expected <= count)
    {
        if (queue.tryPop(value))
        {
            REQUIRE(value == expected); // order and no duplicates or losses
            sum += value;
            ++expected;
        }
    }
    REQUIRE(sum == count * (count + 1) / 2);
}
