#pragma once

#include "core/Uuid.h"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

namespace af::test
{

/// Directory with the committed WAV fixtures (tests/data/wav).
inline std::filesystem::path wavFixture(const std::string& name)
{
    return std::filesystem::path(AF_TEST_DATA_DIR) / "wav" / name;
}

/// The fixture signal: see tests/data/wav/generate.py.
inline float fixtureSample(long long frame, int channel)
{
    return static_cast<float>((7 * frame + 13 * static_cast<long long>(channel)) % 200 - 100) /
           128.0f;
}

inline constexpr long long fixtureFrames = 301;

inline std::vector<unsigned char> readBytes(const std::filesystem::path& file)
{
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

inline void writeBytes(const std::filesystem::path& file, const std::vector<unsigned char>& bytes)
{
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
}

/// A fresh directory under the system temp folder, removed when the object goes away.
class TempDir
{
public:
    TempDir()
        : path_(std::filesystem::temp_directory_path() /
                ("af-test-" + core::Uuid::generate().toString()))
    {
        std::filesystem::create_directories(path_);
    }

    ~TempDir()
    {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    TempDir(TempDir&&) = delete;
    TempDir& operator=(TempDir&&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }
    [[nodiscard]] std::filesystem::path operator/(const std::string& name) const
    {
        return path_ / name;
    }

private:
    std::filesystem::path path_;
};

} // namespace af::test
