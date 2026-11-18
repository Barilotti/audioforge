#pragma once

#include "engine/AudioEngine.h"

#include <cstdint>
#include <optional>

namespace af::app
{

/// The message-thread side of the transport (architecture v0.2, section 4.2): Play, Pause,
/// Stop and seek, sent to the audio thread as TransportCommands.
///
/// It keeps the desired state, which the UI shows at once, even before the audio thread has
/// applied it (or while no device runs at all). Commands that do not fit into the engine queue
/// are kept and sent again by pump(). Because the audio thread applies all queued commands at
/// the start of one block, only the last play state and the last seek matter, so the kept
/// commands collapse to at most two and can never grow.
///
/// Stop returns to the position where playback last started; Stop while stopped returns to the
/// start of the event. Reaching the end behaves like Stop.
class Transport
{
public:
    explicit Transport(engine::AudioEngine& engine);

    void play();
    void pause();
    void stop();
    /// Space: Play when stopped or paused, Stop when playing.
    void togglePlayStop();
    /// Shift+Space: Play when stopped or paused, Pause when playing.
    void togglePlayPause();
    void seek(core::FrameCount frame);

    /// Call on a timer (30 Hz): resends kept commands and handles engine events.
    void pump();

    [[nodiscard]] bool isPlaying() const noexcept { return playing_; }
    /// The playhead: the engine position, or the target of a seek it has not applied yet.
    [[nodiscard]] core::FrameCount position() const noexcept;
    [[nodiscard]] core::FrameCount startPosition() const noexcept { return startPosition_; }

private:
    void send(engine::TransportCommand command);
    void flush();

    engine::AudioEngine& engine_;
    bool playing_ = false;
    core::FrameCount startPosition_ = 0;
    std::uint64_t sent_ = 0; ///< Commands accepted by the engine queue.
    std::optional<core::FrameCount> lastSeek_;
    std::uint64_t lastSeekSerial_ = 0; ///< Value of sent_ after the last seek was accepted.
    std::optional<core::FrameCount> keptSeek_;
    std::optional<bool> keptPlaying_;
};

} // namespace af::app
