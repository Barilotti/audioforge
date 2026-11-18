#include "model/Validation.h"

#include "model/Limits.h"

#include <cmath>
#include <string>
#include <unordered_set>

namespace af::model
{

namespace
{

/// An asset may not be longer than an Event can be; this also keeps tick arithmetic far from
/// int64 overflow.
constexpr core::FrameCount maxAssetFrames(core::SampleRate rate) noexcept
{
    return limits::maxEventLength.value / core::TimeTicks::perSecond * rate;
}

class Validator
{
public:
    explicit Validator(const Project& project)
        : project_(project)
    {
    }

    std::vector<core::Error> run()
    {
        const std::string projectSubject = "project " + project_.id.uuid.toString();
        checkId(project_.id.uuid, projectSubject);
        if (!core::isSupportedSampleRate(project_.settings.defaultSampleRate))
        {
            fail("Project sample rate is not supported", projectSubject + ".defaultSampleRate");
        }

        for (const auto& asset : project_.assets)
        {
            if (!asset)
            {
                fail("Null asset node", projectSubject + ".assets");
                continue;
            }
            checkAsset(*asset);
        }

        for (const auto& event : project_.events)
        {
            if (!event)
            {
                fail("Null event node", projectSubject + ".events");
                continue;
            }
            checkEvent(*event);
        }
        return std::move(errors_);
    }

private:
    void fail(std::string message, std::string subject)
    {
        errors_.push_back(
            core::makeError(core::ErrorCode::invalidData, std::move(message), std::move(subject)));
    }

    void checkId(const core::Uuid& id, const std::string& subject)
    {
        if (id.isNil())
        {
            fail("Id is nil", subject);
        }
        else if (!seenIds_.insert(id).second)
        {
            fail("Id is used by more than one entity", subject);
        }
    }

    void checkGain(double gainDb, const std::string& subject)
    {
        if (!std::isfinite(gainDb) || gainDb < limits::minGainDb || gainDb > limits::maxGainDb)
        {
            fail("Gain is outside [-96, +24] dB", subject);
        }
    }

    void checkAsset(const AssetRef& asset)
    {
        const std::string subject = "asset " + asset.id.uuid.toString();
        checkId(asset.id.uuid, subject);
        if (asset.absolutePath.empty())
        {
            fail("Asset path is empty", subject + ".absolutePath");
        }
        if (!core::isSupportedSampleRate(asset.info.sampleRate))
        {
            fail("Asset sample rate is not supported", subject + ".sampleRate");
        }
        if (asset.info.channels < 1 || asset.info.channels > limits::maxAssetChannels)
        {
            fail("Asset channel count is outside 1...64", subject + ".channels");
        }
        if (asset.info.frames < 1)
        {
            fail("Asset has no frames", subject + ".frames");
        }
        else if (core::isSupportedSampleRate(asset.info.sampleRate) &&
                 asset.info.frames > maxAssetFrames(asset.info.sampleRate))
        {
            fail("Asset is longer than 24 hours", subject + ".frames");
        }
    }

    void checkEvent(const Event& event)
    {
        const std::string subject = "event " + event.id.uuid.toString();
        checkId(event.id.uuid, subject);
        checkGain(event.masterGainDb, subject + ".masterGainDb");
        for (const auto& layer : event.layers)
        {
            if (!layer)
            {
                fail("Null layer node", subject + ".layers");
                continue;
            }
            checkLayer(*layer);
        }
    }

    void checkLayer(const Layer& layer)
    {
        const std::string subject = "layer " + layer.id.uuid.toString();
        checkId(layer.id.uuid, subject);
        checkGain(layer.gainDb, subject + ".gainDb");
        if (!std::isfinite(layer.pan) || layer.pan < limits::minPan || layer.pan > limits::maxPan)
        {
            fail("Pan is outside [-1, +1]", subject + ".pan");
        }
        if (layer.colourRgb > 0xFFFFFFu)
        {
            fail("Colour is not 0xRRGGBB", subject + ".colour");
        }
        for (const auto& clip : layer.clips)
        {
            if (!clip)
            {
                fail("Null clip node", subject + ".clips");
                continue;
            }
            checkClip(*clip);
        }
    }

    void checkClip(const Clip& clip)
    {
        const std::string subject = "clip " + clip.id.uuid.toString();
        checkId(clip.id.uuid, subject);
        checkGain(clip.gainDb, subject + ".gainDb");

        if (clip.position.value < 0)
        {
            fail("Clip starts before the Event start", subject + ".position");
        }
        if (clip.fadeIn.length.value < 0)
        {
            fail("Fade length is negative", subject + ".fadeIn");
        }
        if (clip.fadeOut.length.value < 0)
        {
            fail("Fade length is negative", subject + ".fadeOut");
        }

        const auto asset = project_.findAsset(clip.asset);
        if (!asset)
        {
            fail("Clip refers to an asset that is not in the project", subject + ".asset");
            return;
        }

        const auto& info = asset->info;
        if (clip.sourceInFrames < 0 || clip.sourceInFrames >= clip.sourceOutFrames ||
            clip.sourceOutFrames > info.frames)
        {
            fail("Clip source range is empty or outside the asset", subject + ".sourceFrames");
            return;
        }
        if (!core::isSupportedSampleRate(info.sampleRate) ||
            info.frames > maxAssetFrames(info.sampleRate))
        {
            return; // Already reported for the asset; the length cannot be computed safely.
        }

        const auto length = clipTimelineLength(clip, info);
        if (clip.position.value >= 0 &&
            clip.position.value > limits::maxEventLength.value - length.value)
        {
            fail("Clip ends after the 24-hour Event limit", subject + ".position");
        }
        if (clip.fadeIn.length.value >= 0 && clip.fadeOut.length.value >= 0 &&
            clip.fadeIn.length.value > length.value - clip.fadeOut.length.value)
        {
            fail("Fades are longer than the clip", subject + ".fades");
        }
    }

    const Project& project_;
    std::unordered_set<core::Uuid> seenIds_;
    std::vector<core::Error> errors_;
};

} // namespace

std::vector<core::Error> findInvariantViolations(const Project& project)
{
    return Validator{project}.run();
}

core::Result<void> validate(const Project& project)
{
    auto errors = findInvariantViolations(project);
    if (errors.empty())
    {
        return {};
    }
    return std::move(errors.front());
}

} // namespace af::model
