#include "core/Log.h"
#include "core/RealtimeThread.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>

namespace fs = std::filesystem;
namespace aflog = af::core::log;

namespace
{

// A fresh directory under the system temp folder, removed at the end of the test.
class TempDir
{
public:
    TempDir()
        : path_(fs::temp_directory_path() /
                ("af-log-test-" +
                 std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id())) + "-" +
                 std::to_string(counter_++)))
    {
        fs::remove_all(path_);
    }
    ~TempDir()
    {
        aflog::shutdown();
        std::error_code ec;
        fs::remove_all(path_, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    TempDir(TempDir&&) = delete;
    TempDir& operator=(TempDir&&) = delete;

    [[nodiscard]] const fs::path& path() const { return path_; }

private:
    fs::path path_;
    static inline int counter_ = 0;
};

std::string readAll(const fs::path& file)
{
    std::ifstream in{file};
    return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

int countLogFiles(const fs::path& dir)
{
    int count = 0;
    for (const auto& entry : fs::directory_iterator(dir))
    {
        if (entry.path().extension() == ".log")
        {
            ++count;
        }
    }
    return count;
}

} // namespace

TEST_CASE("Log writes formatted messages to a new file", "[core][log]")
{
    const TempDir dir;
    const auto file = aflog::initialise({.directory = dir.path(), .minLevel = aflog::Level::info});
    REQUIRE(file.ok());
    REQUIRE(file.value().parent_path() == dir.path());

    aflog::info("Imported {} files", 3);
    aflog::debug("hidden {}", 1);
    aflog::error("Cannot open {}", "hit.wav");
    aflog::shutdown();

    const auto text = readAll(file.value());
    REQUIRE(text.find("[INFO   ]") != std::string::npos);
    REQUIRE(text.find("Imported 3 files") != std::string::npos);
    REQUIRE(text.find("[ERROR  ] ") != std::string::npos);
    REQUIRE(text.find("Cannot open hit.wav") != std::string::npos);
    REQUIRE(text.find("hidden") == std::string::npos);
}

TEST_CASE("Log keeps at most maxFiles files", "[core][log]")
{
    const TempDir dir;
    for (int i = 0; i < 6; ++i)
    {
        REQUIRE(aflog::initialise({.directory = dir.path(), .maxFiles = 3}).ok());
        aflog::info("run {}", i);
    }
    aflog::shutdown();
    REQUIRE(countLogFiles(dir.path()) == 3);
}

TEST_CASE("Log rejects invalid configuration", "[core][log]")
{
    REQUIRE_FALSE(aflog::initialise({}).ok());
    const TempDir dir;
    REQUIRE(aflog::initialise({.directory = dir.path(), .maxFiles = 0}).error().code ==
            af::core::ErrorCode::invalidArgument);
}

TEST_CASE("Messages from a real-time thread are dropped and reported", "[core][log]")
{
    const TempDir dir;
    const auto file = aflog::initialise({.directory = dir.path()});
    REQUIRE(file.ok());

    const auto before = aflog::droppedRealtimeMessages();
    {
        const af::core::ScopedRealtimeThread realtime;
        REQUIRE(af::core::isRealtimeThread());
        aflog::info("must not be written");
        aflog::error("must not be written either");
    }
    REQUIRE_FALSE(af::core::isRealtimeThread());
    REQUIRE(aflog::droppedRealtimeMessages() == before + 2);

    aflog::info("back on a normal thread");
    aflog::shutdown();

    const auto text = readAll(file.value());
    REQUIRE(text.find("must not be written") == std::string::npos);
    REQUIRE(text.find("2 log message(s) dropped") != std::string::npos);
    REQUIRE(text.find("back on a normal thread") != std::string::npos);
}
