#pragma once

#include "assets/AudioBufferCache.h"
#include "core/rt/Realtime.h"
#include "dsp/Fade.h"
#include "dsp/Gain.h"
#include "dsp/SincKernel.h"
#include "engine/RenderSpec.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace af::engine
{

/// Plays one clip: reads its source range (forwards or backwards), resamples it to the render
/// rate, applies fades and clip gain (architecture v0.2, section 4.4, CLIP).
///
/// Every output sample is computed from its absolute timeline frame with exact integer
/// arithmetic, so the result does not depend on how the timeline is cut into blocks.
struct ClipVoice
{
    assets::DecodedAudioPtr audio;
    int sourceChannels = 1;               ///< 1 (mono) or 2 (first two channels of the asset).
    core::FrameCount startFrame = 0;      ///< Timeline position at the render rate.
    core::FrameCount lengthFrames = 0;    ///< Output length at the render rate.
    core::FrameCount sourceIn = 0;        ///< First asset frame.
    core::FrameCount sourceLength = 0;    ///< Asset frames played.
    core::SampleRate sourceRate = 48'000; ///< Asset sample rate.
    core::SampleRate renderRate = 48'000;
    bool reverse = false;
    float gain = 1.0f;
    core::FrameCount fadeInFrames = 0;
    core::FrameCount fadeOutFrames = 0;
    dsp::FadeShape fadeInShape = dsp::FadeShape::equalPower;
    dsp::FadeShape fadeOutShape = dsp::FadeShape::equalPower;
    std::shared_ptr<const dsp::SincKernel> kernel; ///< Null without resampling or in draft quality.

    [[nodiscard]] core::FrameCount endFrame() const noexcept { return startFrame + lengthFrames; }

    /// Adds frames [blockStart, blockStart + frames) to the bus. A mono voice on a stereo bus is
    /// added to both channels; on a mono bus only `left` is used.
    void render(core::FrameCount blockStart, int frames, float* left, float* right,
                bool monoBus) const noexcept AF_NONBLOCKING;

private:
    [[nodiscard]] float sourceSample(int channel,
                                     core::FrameCount index) const noexcept AF_NONBLOCKING;
    void sourceFrame(core::FrameCount n, float& left, float& right) const noexcept AF_NONBLOCKING;
    [[nodiscard]] float envelope(core::FrameCount n) const noexcept AF_NONBLOCKING;
};

/// One layer: sum of its clips -> gain -> pan -> mute/solo (architecture v0.2, section 4.4).
struct LayerNode
{
    std::vector<ClipVoice> clips;
    bool monoBus = false;  ///< All clips are mono: pan uses the constant-power law.
    dsp::StereoGain gains; ///< Fader and pan combined.
    bool audible = true;   ///< False when muted or when another layer is soloed.
};

/// The immutable, compiled form of one Event for one RenderSpec (architecture v0.2, 4.2).
/// Only the audio thread calls render(); its scratch buffers are allocated at construction.
class RenderGraph
{
public:
    struct Contents
    {
        std::vector<LayerNode> layers;
        float masterGain = 1.0f;
        bool needsCrossfade = false;
        /// End of the last clip, including clips whose asset is still decoding: playback stops
        /// here.
        core::FrameCount endFrame = 0;
        std::vector<assets::AssetKey>
            pendingAssets; ///< Clips skipped because the asset is not ready.
    };

    RenderGraph(const RenderSpec& spec, std::uint64_t generation, Contents contents);

    [[nodiscard]] const RenderSpec& spec() const noexcept { return spec_; }
    [[nodiscard]] std::uint64_t generation() const noexcept { return generation_; }
    [[nodiscard]] bool needsCrossfade() const noexcept { return contents_.needsCrossfade; }
    [[nodiscard]] core::FrameCount endFrame() const noexcept { return contents_.endFrame; }
    [[nodiscard]] const std::vector<assets::AssetKey>& pendingAssets() const noexcept
    {
        return contents_.pendingAssets;
    }
    [[nodiscard]] const std::vector<LayerNode>& layers() const noexcept { return contents_.layers; }

    /// Renders timeline frames [start, start + frames) into `left`/`right`, overwriting them.
    /// Requires frames <= spec().maxBlockFrames.
    void render(core::FrameCount start, int frames, float* left,
                float* right) noexcept AF_NONBLOCKING;

private:
    RenderSpec spec_;
    std::uint64_t generation_;
    Contents contents_;
    std::vector<float> busLeft_;
    std::vector<float> busRight_;
};

} // namespace af::engine
