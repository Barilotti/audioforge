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

Playback::Document emptyDocument()
{
    model::Event event;
    event.id = model::EventId::generate();
    event.name = "Event 1";
    Playback::Document document;
    document.event = event.id;
    document.project.id = model::ProjectId::generate();
    document.project.events.push_back(model::makeNode(event));
    return document;
}

} // namespace

Playback::Playback(DeviceService& devices, assets::AudioBufferCache& cache, int timerHz)
    : devices_(devices)
    , cache_(cache)
    , compiler_(cache)
    , document_(emptyDocument())
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

void Playback::importFiles(const std::vector<std::filesystem::path>& files)
{
    for (const auto& file : files)
    {
        PendingImport import{model::AssetId::generate(), file};
        core::log::info("Importing {}", toUtf8(file));
        cache_.request(import.asset.uuid, file);
        pending_.push_back(std::move(import));
    }
}

void Playback::clear()
{
    for (const auto& import : pending_)
    {
        cache_.remove(import.asset.uuid);
    }
    pending_.clear();
    const auto old = documentAssets();
    devices_.transport().stop();
    devices_.transport().seek(0);
    document_ = emptyDocument();
    ++revision_;
    updatePins();
    if (!toneOn_)
    {
        publish();
    }
    for (const auto& asset : old)
    {
        cache_.remove(asset.uuid); // a retired graph may still hold a buffer: that is fine
    }
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
    publish();
    if (on)
    {
        transport.play();
    }
}

Playback::Status Playback::status() const
{
    Status status;
    status.testTone = toneOn_;
    const auto event = document_.project.findEvent(document_.event);
    status.layerCount = event ? static_cast<int>(event->layers.size()) : 0;

    float progress = 0.0f;
    auto countLoading = [&](const model::AssetId& asset)
    {
        const auto s = cache_.status(asset.uuid);
        if (s.state == assets::AssetState::decoding || s.state == assets::AssetState::unloaded)
        {
            ++status.loadingCount;
            progress += s.progress;
        }
    };
    for (const auto& import : pending_)
    {
        countLoading(import.asset);
    }
    for (const auto& asset : documentAssets())
    {
        countLoading(asset);
    }
    status.progress =
        status.loadingCount > 0 ? progress / static_cast<float>(status.loadingCount) : 1.0f;
    status.state = status.loadingCount > 0 ? State::loading
                   : status.layerCount > 0 ? State::ready
                                           : State::empty;

    for (const auto& asset : document_.project.assets)
    {
        if (asset && asset->info.sampleRate > 0)
        {
            for (const auto& layer : event ? event->layers : std::vector<model::LayerPtr>{})
            {
                for (const auto& clip : layer->clips)
                {
                    if (clip->asset == asset->id)
                    {
                        status.lengthSeconds =
                            std::max(status.lengthSeconds,
                                     clip->position.toSecondsApprox() +
                                         static_cast<double>(clip->sourceLengthFrames()) /
                                             static_cast<double>(asset->info.sampleRate));
                    }
                }
            }
        }
    }
    return status;
}

std::vector<Playback::ImportProblem> Playback::takeProblems()
{
    return std::exchange(problems_, {});
}

void Playback::refresh()
{
    // Add layers in import order: the head waits until its header is known.
    bool added = false;
    while (!pending_.empty())
    {
        const auto& head = pending_.front();
        const auto loaded = cache_.status(head.asset.uuid);
        if (loaded.state == assets::AssetState::failed)
        {
            const auto message = loaded.error ? loaded.error->describe() : "unknown error";
            core::log::warning("Import failed: {}", message);
            problems_.push_back({toUtf8(head.file.filename()), message, false});
            cache_.remove(head.asset.uuid);
            pending_.pop_front();
            continue;
        }
        if (!loaded.info)
        {
            break; // header not read yet
        }
        addLayer(head, *loaded.info);
        pending_.pop_front();
        added = true;
    }
    if (added)
    {
        ++revision_;
        updatePins();
        if (!toneOn_)
        {
            publish();
        }
    }

    if (!toneOn_ && !waitingFor_.empty() &&
        std::any_of(waitingFor_.begin(), waitingFor_.end(), [this](const assets::AssetKey& key)
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

void Playback::addLayer(const PendingImport& import, const assets::AudioFileInfo& info)
{
    model::AssetRef asset{
        import.asset, toUtf8(import.file), {}, {info.sampleRate, info.channels, info.frames}};

    model::Clip clip;
    clip.id = model::ClipId::generate();
    clip.asset = asset.id;
    clip.sourceOutFrames = info.frames;

    model::Layer layer;
    layer.id = model::LayerId::generate();
    layer.name = toUtf8(import.file.stem());
    layer.clips.push_back(model::makeNode(clip));

    auto& project = document_.project;
    project.assets.push_back(model::makeNode(asset));
    for (auto& event : project.events)
    {
        if (event && event->id == document_.event)
        {
            auto edited = *event;
            edited.layers.push_back(model::makeNode(layer));
            event = model::makeNode(std::move(edited));
        }
    }
    core::log::info("Added layer {}: {} Hz, {} ch, {} frames", layer.name, info.sampleRate,
                    info.channels, info.frames);
    if (info.dataTruncated)
    {
        problems_.push_back({toUtf8(import.file.filename()),
                             "The file is truncated: only the frames present are used", true});
    }
}

std::vector<model::AssetId> Playback::documentAssets() const
{
    std::vector<model::AssetId> assets;
    for (const auto& asset : document_.project.assets)
    {
        if (asset)
        {
            assets.push_back(asset->id);
        }
    }
    return assets;
}

void Playback::publish()
{
    const auto spec = devices_.renderSpec();
    if (!spec)
    {
        waitingFor_.clear();
        return; // no device: compiled when one opens (formatChanged)
    }
    const Document& source = toneOn_ ? tone_ : document_;
    engine::CompileOptions options;
    options.needsCrossfade = devices_.transport().isPlaying(); // layers added while playing
    auto graph = compiler_.compile(source.project, source.event, *spec, options);
    waitingFor_ = graph->pendingAssets();
    devices_.engine().publish(std::move(graph));
    devices_.engine().collectRetired();
}

void Playback::updatePins()
{
    auto pinned = engine::GraphCompiler::referencedAssets(tone_.project, tone_.event);
    for (const auto& asset : documentAssets())
    {
        pinned.push_back(asset.uuid);
    }
    for (const auto& import : pending_)
    {
        pinned.push_back(import.asset.uuid);
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
