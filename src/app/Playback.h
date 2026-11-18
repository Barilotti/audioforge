#pragma once

#include "app/DeviceService.h"
#include "app/TestTone.h"
#include "assets/AudioBufferCache.h"
#include "engine/GraphCompiler.h"
#include "model/Project.h"

#include <juce_events/juce_events.h>

#include <cstdint>
#include <deque>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace af::app
{

/// The session of stage S2 "build layers": one event that imported files are added to, each on
/// its own layer, and what the engine plays.
///
/// Import decodes in the background (AudioBufferCache). A file becomes a layer as soon as its
/// header is read, in the order the files were given, and shows its waveform while it loads;
/// it becomes audible when decoded. Files that cannot be read add no layer and are reported.
/// The graph is compiled again when the document changes, when the device format changes and
/// when an asset the graph waits for becomes ready. The test tone of the Audio Settings dialog
/// temporarily takes the engine over. Message thread only.
///
/// Replaced by ProjectSession with Commands and Undo in stage S3.
class Playback final : public DeviceService::Listener, private juce::Timer
{
public:
    enum class State
    {
        empty,   ///< No layers.
        loading, ///< Files are being read or decoded.
        ready,
    };

    struct Status
    {
        State state = State::empty;
        int layerCount = 0;
        int loadingCount = 0;  ///< Files not yet audible.
        float progress = 0.0f; ///< Mean decoding progress of loading files, 0...1.
        double lengthSeconds = 0.0;
        bool testTone = false;
    };

    /// A file that could not be imported, or imported with a warning.
    struct ImportProblem
    {
        std::string fileName;  ///< UTF-8, without folders.
        std::string message;   ///< English diagnostic.
        bool imported = false; ///< True for a warning: the file was added anyway.
    };

    /// What the timeline shows.
    struct Document
    {
        model::Project project;
        model::EventId event;
    };

    /// `timerHz` 0 disables the timer: tests call refresh() themselves.
    Playback(DeviceService& devices, assets::AudioBufferCache& cache, int timerHz = 30);
    ~Playback() override;

    Playback(const Playback&) = delete;
    Playback& operator=(const Playback&) = delete;
    Playback(Playback&&) = delete;
    Playback& operator=(Playback&&) = delete;

    /// Adds each file as a new layer at the bottom, starting at 0.
    void importFiles(const std::vector<std::filesystem::path>& files);
    void importFile(const std::filesystem::path& file) { importFiles({file}); }
    /// Removes every layer (File > New).
    void clear();
    void setTestTone(bool on);

    [[nodiscard]] Status status() const;
    [[nodiscard]] bool testToneOn() const noexcept { return toneOn_; }
    /// Problems since the previous call; the UI reports them once.
    [[nodiscard]] std::vector<ImportProblem> takeProblems();

    [[nodiscard]] const Document* document() const noexcept { return &document_; }
    /// Grows whenever document() changes.
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }
    [[nodiscard]] assets::AudioBufferCache& cache() noexcept { return cache_; }
    [[nodiscard]] Transport& transport() noexcept { return devices_.transport(); }

    /// Follows the imports and the graph state; runs on the timer.
    void refresh();

private:
    struct PendingImport
    {
        model::AssetId asset;
        std::filesystem::path file;
    };

    void deviceEvent(const DeviceEvent& event) override;
    void timerCallback() override { refresh(); }
    void publish();
    void addLayer(const PendingImport& import, const assets::AudioFileInfo& info);
    void updatePins();
    [[nodiscard]] std::vector<model::AssetId> documentAssets() const;

    DeviceService& devices_;
    assets::AudioBufferCache& cache_;
    engine::GraphCompiler compiler_;
    Document tone_;
    Document document_;
    bool toneOn_ = false;

    std::deque<PendingImport> pending_;        ///< Waiting for their header, in import order.
    std::vector<assets::AssetKey> waitingFor_; ///< Assets the published graph had to skip.
    std::vector<ImportProblem> problems_;
    std::uint64_t revision_ = 0;
};

} // namespace af::app
