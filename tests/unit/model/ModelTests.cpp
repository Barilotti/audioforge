#include "model/Node.h"
#include "model/Project.h"
#include "model/Validation.h"

#include <catch2/catch_test_macros.hpp>

using namespace af::model;
using af::core::TimeTicks;

TEST_CASE("Empty project has one empty event and is valid", "[model]")
{
    const auto project = makeEmptyProject();
    REQUIRE_FALSE(project->id.isNil());
    REQUIRE(project->events.size() == 1);
    REQUIRE(project->events.front()->layers.empty());
    REQUIRE(validate(*project).ok());
    REQUIRE(eventEnd(*project->events.front(), *project) == TimeTicks{});
}

TEST_CASE("modified() copies a node and leaves the original unchanged", "[model]")
{
    Clip clip;
    clip.id = ClipId::generate();
    clip.gainDb = -6.0;
    const auto original = makeNode(clip);

    const auto louder = modified(original, [](Clip& c) { c.gainDb = 0.0; });

    REQUIRE(original->gainDb == -6.0);
    REQUIRE(louder->gainDb == 0.0);
    REQUIRE(louder->id == original->id);
    REQUIRE(louder.get() != original.get());
}

TEST_CASE("Editing one clip shares every untouched node", "[model]")
{
    const auto asset =
        makeNode(AssetRef{AssetId::generate(), "/sfx/a.wav", {}, {48'000, 2, 48'000}});
    auto makeClip = [&](std::int64_t seconds)
    {
        Clip c;
        c.id = ClipId::generate();
        c.asset = asset->id;
        c.position = TimeTicks::fromSeconds(seconds);
        c.sourceOutFrames = 48'000;
        return makeNode(c);
    };

    Layer layerA;
    layerA.id = LayerId::generate();
    layerA.clips = {makeClip(0), makeClip(2)};
    Layer layerB;
    layerB.id = LayerId::generate();
    layerB.clips = {makeClip(4)};
    Event event;
    event.id = EventId::generate();
    event.layers = {makeNode(layerA), makeNode(layerB)};
    Project v1;
    v1.id = ProjectId::generate();
    v1.assets = {asset};
    v1.events = {makeNode(event)};
    const auto doc1 = makeNode(v1);

    // Move the second clip of layer A by one second: copy the clip, layer A, event and project.
    const auto& oldLayer = doc1->events[0]->layers[0];
    const auto newClip =
        modified(oldLayer->clips[1], [](Clip& c) { c.position += TimeTicks::fromSeconds(1); });
    const auto newLayer = modified(oldLayer, [&](Layer& l) { l.clips[1] = newClip; });
    const auto newEvent = modified(doc1->events[0], [&](Event& e) { e.layers[0] = newLayer; });
    const auto doc2 = modified(doc1, [&](Project& p) { p.events[0] = newEvent; });

    REQUIRE(validate(*doc2).ok());
    REQUIRE(doc1->events[0]->layers[0]->clips[1]->position == TimeTicks::fromSeconds(2));
    REQUIRE(doc2->events[0]->layers[0]->clips[1]->position == TimeTicks::fromSeconds(3));
    REQUIRE(doc2->events[0]->layers[0]->clips[0] == doc1->events[0]->layers[0]->clips[0]);
    REQUIRE(doc2->events[0]->layers[1] == doc1->events[0]->layers[1]);
    REQUIRE(doc2->assets[0] == doc1->assets[0]);
    REQUIRE(eventEnd(*doc2->events[0], *doc2) == TimeTicks::fromSeconds(5));
}

TEST_CASE("Clip length on the timeline follows the asset sample rate", "[model]")
{
    Clip clip;
    clip.sourceInFrames = 100;
    clip.sourceOutFrames = 100 + 44'100;
    REQUIRE(clipTimelineLength(clip, {44'100, 1, 1'000'000}) == TimeTicks::fromSeconds(1));
    REQUIRE(clipTimelineLength(clip, {88'200, 1, 1'000'000}).value == TimeTicks::perSecond / 2);
}

TEST_CASE("Project lookups find assets and events by id", "[model]")
{
    const auto project = makeEmptyProject();
    const auto eventId = project->events.front()->id;
    REQUIRE(project->findEvent(eventId) == project->events.front());
    REQUIRE(project->findEvent(EventId::generate()) == nullptr);
    REQUIRE(project->findAsset(AssetId::generate()) == nullptr);
}
