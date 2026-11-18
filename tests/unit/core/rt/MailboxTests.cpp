#include "core/rt/Mailbox.h"
#include "core/rt/RetireQueue.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <memory>
#include <thread>

using af::core::rt::Mailbox;
using af::core::rt::RetireQueue;

namespace
{

// Counts live instances so the tests can prove nothing leaks or is deleted twice.
struct Tracked
{
    explicit Tracked(int v)
        : value(v)
    {
        live.fetch_add(1);
    }
    ~Tracked() { live.fetch_sub(1); }
    Tracked(const Tracked&) = delete;
    Tracked& operator=(const Tracked&) = delete;
    Tracked(Tracked&&) = delete;
    Tracked& operator=(Tracked&&) = delete;

    int value;
    static inline std::atomic<int> live{0};
};

} // namespace

TEST_CASE("Mailbox hands over the newest object and returns stale ones", "[core][rt]")
{
    {
        Mailbox<Tracked> mailbox;
        REQUIRE(mailbox.take() == nullptr);

        REQUIRE(mailbox.publish(std::make_unique<Tracked>(1)) == nullptr);
        auto stale = mailbox.publish(std::make_unique<Tracked>(2));
        REQUIRE(stale != nullptr); // never taken: returned to the publisher
        REQUIRE(stale->value == 1);
        stale.reset();

        std::unique_ptr<Tracked> taken{mailbox.take()};
        REQUIRE(taken != nullptr);
        REQUIRE(taken->value == 2);
        REQUIRE(mailbox.take() == nullptr);

        REQUIRE(mailbox.publish(std::make_unique<Tracked>(3)) == nullptr);
        // Left in the mailbox: deleted by its destructor.
    }
    REQUIRE(Tracked::live.load() == 0);
}

TEST_CASE("RetireQueue refuses when full and deletes collected objects", "[core][rt]")
{
    {
        RetireQueue<Tracked, 4> retire;
        for (int i = 0; i < 4; ++i)
        {
            REQUIRE(retire.canRetire());
            REQUIRE(retire.retire(new Tracked(i))); // NOLINT(cppcoreguidelines-owning-memory)
        }
        REQUIRE_FALSE(retire.canRetire());

        auto kept = std::make_unique<Tracked>(99);
        REQUIRE_FALSE(retire.retire(kept.get())); // full: ownership stays with the caller
        REQUIRE(Tracked::live.load() == 5);

        REQUIRE(retire.collect() == 4);
        REQUIRE(Tracked::live.load() == 1);
        REQUIRE(retire.canRetire());
        REQUIRE(retire.retire(nullptr)); // retiring nothing always succeeds
    }
    REQUIRE(Tracked::live.load() == 0);
}

TEST_CASE("Mailbox and RetireQueue lose and leak nothing under load", "[core][rt][stress]")
{
    // Simulates the RenderGraph life cycle: the UI thread publishes objects and collects retired
    // ones; the "audio" thread takes the newest object and retires the previous one, but only
    // when the retire queue has room (otherwise it keeps playing the current one).
    constexpr int published = 200'000;
    Mailbox<Tracked> mailbox;
    RetireQueue<Tracked, 8> retire;
    std::atomic<bool> stop{false};
    std::atomic<int> deferredSwaps{0};
    int lastSeen = -1;
    bool monotonic = true;
    bool retireFailed = false;

    std::jthread audio{[&]
                       {
                           Tracked* current = nullptr;
                           while (!stop.load(std::memory_order_acquire))
                           {
                               if (!retire.canRetire())
                               {
                                   deferredSwaps.fetch_add(1, std::memory_order_relaxed);
                                   continue;
                               }
                               if (Tracked* next = mailbox.take())
                               {
                                   monotonic = monotonic && next->value > lastSeen;
                                   lastSeen = next->value;
                                   REQUIRE(retire.retire(current));
                                   current = next;
                               }
                           }
                           while (!retire.retire(current))
                           {
                           }
                       }};

    std::size_t staleDeleted = 0;
    for (int i = 0; i < published; ++i)
    {
        if (mailbox.publish(std::make_unique<Tracked>(i)) != nullptr)
        {
            ++staleDeleted;
        }
        retire.collect();
    }
    stop.store(true, std::memory_order_release);
    audio.join();
    retire.collect();
    mailbox.reclaim().reset();

    REQUIRE(monotonic); // the audio thread never goes back to an older object
    REQUIRE_FALSE(retireFailed);
    REQUIRE(Tracked::live.load() == 0);
    INFO("stale objects deleted by the publisher: " << staleDeleted);
    INFO("deferred swaps: " << deferredSwaps.load());
}
