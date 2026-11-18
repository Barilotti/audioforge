#pragma once

#include "core/TimeTicks.h"

#include <cstdint>

namespace af::engine
{

/// A transport command from the message thread to the audio thread (architecture v0.2,
/// section 4.2: SPSC queue, 256 entries). The audio thread applies every queued command at the
/// start of a block, before rendering it.
struct TransportCommand
{
    enum class Kind : std::uint8_t
    {
        play,  ///< Start rendering from the current position.
        pause, ///< Stop rendering; the position stays.
        seek,  ///< Move the position to `frame`; the play state stays.
    };

    Kind kind = Kind::pause;
    core::FrameCount frame = 0;

    [[nodiscard]] static constexpr TransportCommand play() noexcept { return {Kind::play, 0}; }
    [[nodiscard]] static constexpr TransportCommand pause() noexcept { return {Kind::pause, 0}; }
    [[nodiscard]] static constexpr TransportCommand seek(core::FrameCount frame) noexcept
    {
        return {Kind::seek, frame};
    }
};

/// An event from the audio thread to the message thread (SPSC queue, 256 entries). When the
/// queue is full the event is dropped and EngineStatus::droppedEvents grows; the state itself
/// is always readable through EngineStatus.
struct EngineEvent
{
    enum class Kind : std::uint8_t
    {
        endReached, ///< Playback passed the end of the graph and stopped.
    };

    Kind kind = Kind::endReached;
    core::FrameCount frame = 0;        ///< Position when the event happened.
    std::uint64_t appliedCommands = 0; ///< Transport commands applied before the event.
};

} // namespace af::engine
