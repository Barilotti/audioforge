#pragma once

#include "app/DeviceService.h"
#include "app/TestTone.h"
#include "assets/AudioBufferCache.h"
#include "engine/GraphCompiler.h"
#include "model/Project.h"

#include <juce_events/juce_events.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace af::app
{

/// Stage S1 "hear a file": what the engine plays and when its graph is rebuilt.
///
/// File > Import builds a one-event project around the file (decoded in the background by the
/// cache) and replaces the previous one once the new file is ready. The test tone of the Audio
/// Settings dialog temporarily takes the engine over. The graph is compiled again whenever the
/// device format changes or an asset the graph waits for becomes ready. Message thread only.
class Playback final : public DeviceService::Listener, private juce::Timer
{
public:
    enum class State
    {
        empty,   ///< Nothing imported yet.
        loading, ///< A file is being decoded; the previous one (if any) stays playable.
        ready,
        failed, ///< The last import failed; the previous file (if any) stays playable.
    };

    struct Status
    {
        State state = State::empty;
        std::string fileName;    ///< The playable file (UTF-8), empty if none.
        std::string loadingName; ///< The file being decoded or that failed.
        std::string error;       ///< English diagnostic when the last import failed.
        float progress = 0.0f;   ///< Decoding progress, 0...1.
        double lengthSeconds = 0.0;
        bool truncated = false; ///< The file's data chunk was cut short (ADR-0012).
        bool testTone = false;
    };

    /// `timerHz` 0 disables the timer: tests call refresh() themselves.
    Playback(DeviceService& devices, assets::AudioBufferCache& cache, int timerHz = 30);
    ~Playback() override;

    Playback(const Playback&) = delete;
    Playback& operator=(const Playback&) = delete;
    Playback(Playback&&) = delete;
    Playback& operator=(Playback&&) = delete;

    void importFile(const std::filesystem::path& file);
    void setTestTone(bool on);

    [[nodiscard]] Status status() const;

    /// What the timeline shows: the imported project (null before the first import).
    struct Document
    {
        model::Project project;
        model::EventId event;
    };
    [[nodiscard]] const Document* document() const noexcept { return file_ ? &*file_ : nullptr; }
    /// Grows whenever document() changes.
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }
    [[nodiscard]] assets::AudioBufferCache& cache() noexcept { return cache_; }
    [[nodiscard]] Transport& transport() noexcept { return devices_.transport(); }

    /// Follows the import and graph state; runs on the timer.
    void refresh();

private:
    using Source = Document;

    void deviceEvent(const DeviceEvent& event) override;
    void timerCallback() override { refresh(); }
    void publish();
    void finishImport(model::AssetId id, const assets::AssetStatus& loaded);
    void updatePins();

    DeviceService& devices_;
    assets::AudioBufferCache& cache_;
    engine::GraphCompiler compiler_;
    Source tone_;
    std::optional<Source> file_;
    bool toneOn_ = false;

    std::optional<model::AssetId> loading_;
    std::filesystem::path loadingPath_;
    std::optional<model::AssetId> fileAsset_;
    std::vector<assets::AssetKey> waitingFor_; ///< Assets the published graph had to skip.
    Status status_;
    std::uint64_t revision_ = 0;
};

} // namespace af::app
