#include "app/Transport.h"

#include <algorithm>

namespace af::app
{

using engine::TransportCommand;

Transport::Transport(engine::AudioEngine& engine)
    : engine_(engine)
{
}

void Transport::play()
{
    if (playing_)
    {
        return;
    }
    auto from = position();
    const auto end = engine_.status().endFrame;
    if (end > 0 && from >= end)
    {
        from = 0; // at the end: play from the start
        send(TransportCommand::seek(0));
    }
    startPosition_ = from;
    playing_ = true;
    send(TransportCommand::play());
}

void Transport::pause()
{
    if (!playing_)
    {
        return;
    }
    playing_ = false;
    send(TransportCommand::pause());
}

void Transport::stop()
{
    const auto target = playing_ ? startPosition_ : core::FrameCount{0};
    if (playing_)
    {
        playing_ = false;
        send(TransportCommand::pause());
    }
    send(TransportCommand::seek(target));
    startPosition_ = target;
}

void Transport::togglePlayStop()
{
    if (playing_)
    {
        stop();
    }
    else
    {
        play();
    }
}

void Transport::togglePlayPause()
{
    if (playing_)
    {
        pause();
    }
    else
    {
        play();
    }
}

void Transport::seek(core::FrameCount frame)
{
    frame = std::max<core::FrameCount>(frame, 0);
    if (!playing_)
    {
        startPosition_ = frame;
    }
    send(TransportCommand::seek(frame));
}

core::FrameCount Transport::position() const noexcept
{
    if (keptSeek_)
    {
        return *keptSeek_;
    }
    const auto status = engine_.status();
    if (lastSeek_ && status.appliedCommands < lastSeekSerial_)
    {
        return *lastSeek_;
    }
    return status.position;
}

void Transport::send(TransportCommand command)
{
    if (command.kind == TransportCommand::Kind::seek)
    {
        keptSeek_ = command.frame;
    }
    else
    {
        keptPlaying_ = command.kind == TransportCommand::Kind::play;
    }
    flush();
}

void Transport::flush()
{
    // The seek goes first: both are applied before the same block, and a seek kept from before
    // a play must not be applied after it.
    if (keptSeek_)
    {
        if (!engine_.transport(TransportCommand::seek(*keptSeek_)))
        {
            return;
        }
        ++sent_;
        lastSeek_ = keptSeek_;
        lastSeekSerial_ = sent_;
        keptSeek_.reset();
    }
    if (keptPlaying_)
    {
        const auto command = *keptPlaying_ ? TransportCommand::play() : TransportCommand::pause();
        if (!engine_.transport(command))
        {
            return;
        }
        ++sent_;
        keptPlaying_.reset();
    }
}

void Transport::pump()
{
    flush();
    engine::EngineEvent event;
    while (engine_.popEvent(event))
    {
        if (event.kind != engine::EngineEvent::Kind::endReached)
        {
            continue;
        }
        // Ignore an end that happened before newer commands (for example Play pressed again).
        if (event.appliedCommands == sent_ && !keptSeek_ && !keptPlaying_ && playing_)
        {
            playing_ = false;
            send(TransportCommand::seek(startPosition_));
        }
    }
}

} // namespace af::app
