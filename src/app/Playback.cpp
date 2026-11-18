#include "app/Playback.h"

#include "core/Log.h"
#include "model/Node.h"

#include <algorithm>
#include <utility>

namespace af::app
{

namespace
{

std::string toUtf8(const std::filesystem::path& path)
{
    const auto u8 = path.u8string();
    return {u8.begin(), u8.end()};
}

} // namespace

Playback::Playback(DeviceService& devices, assets::AudioBufferCache& cache, int timerHz)
    : devices_(devices)
    , cache_(cache)
    , compiler_(cache)
{
    auto tone = makeTestTone(cache_);
    tone_ = {std::move(tone.project), tone.event};
    updatePins();
    devices_.addListener(this);
    if (timerHz > 0)
    {
        startTimerHz(timerHz);
    }
}

Playback::~Playback()
{
    stopTimer();
    devices_.removeListener(this);
}

void Playback::importFile(const std::filesystem::path& file)
{
    if (loading_)
    {
        cache_.remove(loading_->uuid); // a newer import replaces the one still decoding
    }
    loading_ = model::AssetId::generate();
    loadingPath_ = file;
    status_.state = State::loading;
    status_.loadingName = toUtf8(file.filename());
    status_.error.clear();
    status_.progress = 0.0f;
    core::log::info("Importing {}", toUtf8(file));
    cache_.request(loading_->uuid, file);
}

void Playback::setTestTone(bool on)
{
    if (on == toneOn_)
    {
        return;
    }
    auto& transport = devices_.transport();
    transport.stop();
    transport.seek(0);
    toneOn_ = on;
    status_.testTone = on;
    publish();
    if (on)
    {
        transport.play();
    }
}

Playback::Status Playback::status() const
{
    return status_;
}

void Playback::refresh()
{
    if (loading_)
    {
        const auto id = *loading_;
        const auto loaded = cache_.status(id.uuid);
        switch (loaded.state)
        {
        case assets::AssetState::ready:
            finishImport(id, loaded);
            break;
        case assets::AssetState::failed:
            status_.state = State::failed;
            status_.error = loaded.error ? loaded.error->describe() : "unknown error";
            core::log::warning("Import failed: {}", status_.error);
            cache_.remove(id.uuid);
            loading_.reset();
            break;
        case assets::AssetState::decoding:
        case assets::AssetState::unloaded:
        case assets::AssetState::evicted:
            status_.progress = loaded.progress;
            break;
        }
    }

    if (!waitingFor_.empty() &&
        std::all_of(waitingFor_.begin(), waitingFor_.end(), [this](const assets::AssetKey& key)
                    { return cache_.status(key).state == assets::AssetState::ready; }))
    {
        publish();
    }

    // The test tone loops: reaching its end stops the transport and returns to the start.
    if (toneOn_ && !devices_.transport().isPlaying() && devices_.renderSpec())
    {
        devices_.transport().play();
    }
}

void Playback::finishImport(model::AssetId id, const assets::AssetStatus& loaded)
{
    const auto& audio = loaded.audio;
    if (!audio)
    {
        return; // ready always carries the buffer; nothing to play otherwise
    }
    model::AssetRef asset{
        id, toUtf8(loadingPath_), {}, {audio->sampleRate(), audio->channels(), audio->frames()}};

    model::Clip clip;
    clip.id = model::ClipId::generate();
    clip.asset = asset.id;
    clip.sourceOutFrames = audio->frames();

    const auto stem = toUtf8(loadingPath_.stem());
    model::Layer layer;
    layer.id = model::LayerId::generate();
    layer.name = stem;
    layer.clips.push_back(model::makeNode(clip));

    model::Event event;
    event.id = model::EventId::generate();
    event.name = stem;
    event.layers.push_back(model::makeNode(layer));

    Source source;
    source.event = event.id;
    source.project.id = model::ProjectId::generate();
    source.project.assets.push_back(model::makeNode(asset));
    source.project.events.push_back(model::makeNode(event));

    const auto previous = fileAsset_;
    file_ = std::move(source);
    fileAsset_ = id;
    loading_.reset();

    status_.state = State::ready;
    status_.fileName = status_.loadingName;
    status_.progress = 1.0f;
    status_.lengthSeconds =
        static_cast<double>(audio->frames()) / static_cast<double>(audio->sampleRate());
    status_.truncated = loaded.info && loaded.info->dataTruncated;
    core::log::info("Imported {}: {} Hz, {} ch, {} frames", status_.fileName, audio->sampleRate(),
                    audio->channels(), audio->frames());

    updatePins();
    if (!toneOn_)
    {
        devices_.transport().stop();
        devices_.transport().seek(0);
        publish();
    }
    if (previous)
    {
        cache_.remove(previous->uuid); // a retired graph may still hold the buffer: that is fine
    }
}

void Playback::publish()
{
    const auto spec = devices_.renderSpec();
    if (!spec)
    {
        waitingFor_.clear();
        return; // no device: compiled when one opens (formatChanged)
    }
    static const Source nothing{};
    const Source& source = toneOn_ ? tone_ : (file_ ? *file_ : nothing);
    auto graph = compiler_.compile(source.project, source.event, *spec);
    waitingFor_ = graph->pendingAssets();
    devices_.engine().publish(std::move(graph));
    devices_.engine().collectRetired();
}

void Playback::updatePins()
{
    auto pinned = engine::GraphCompiler::referencedAssets(tone_.project, tone_.event);
    if (file_)
    {
        const auto fileAssets =
            engine::GraphCompiler::referencedAssets(file_->project, file_->event);
        pinned.insert(pinned.end(), fileAssets.begin(), fileAssets.end());
    }
    cache_.setPinned(pinned);
}

void Playback::deviceEvent(const DeviceEvent& event)
{
    switch (event.kind)
    {
    case DeviceEvent::Kind::formatChanged:
        publish(); // the device restarted: the engine dropped its graphs
        break;
    case DeviceEvent::Kind::deviceLost:
    case DeviceEvent::Kind::noDevice:
        setTestTone(false); // the device service already stopped the transport
        break;
    case DeviceEvent::Kind::deviceRestored:
        break;
    }
}

} // namespace af::app
