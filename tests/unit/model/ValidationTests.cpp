#include "model/Limits.h"
#include "model/Node.h"
#include "model/Project.h"
#include "model/Validation.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <functional>
#include <limits>
#include <string>
#include <vector>

using namespace af::model;
using af::core::ErrorCode;
using af::core::TimeTicks;

namespace
{

/// A valid project with one asset (2 s at 48 kHz), one event, one layer and one clip, built as
/// plain values so each test can break exactly one field before freezing it.
struct Fixture
{
    AssetRef asset{AssetId::generate(), "/sfx/hit.wav", "hit.wav", {48'000, 2, 96'000}};
    Clip clip;
    Layer layer;
    Event event;
    Project project;

    Fixture()
    {
        clip.id = ClipId::generate();
        clip.asset = asset.id;
        clip.position = TimeTicks::fromSeconds(1);
        clip.sourceInFrames = 0;
        clip.sourceOutFrames = 96'000;
        clip.fadeIn.length = TimeTicks{TimeTicks::perSecond / 10};
        clip.fadeOut.length = TimeTicks{TimeTicks::perSecond / 2};

        layer.id = LayerId::generate();
        layer.name = "Hit";
        event.id = EventId::generate();
        event.name = "Impact";
        project.id = ProjectId::generate();
    }

    [[nodiscard]] Project build() const
    {
        auto l = layer;
        l.clips.push_back(makeNode(clip));
        auto e = event;
        e.layers.push_back(makeNode(l));
        auto p = project;
        p.assets.push_back(makeNode(asset));
        p.events.push_back(makeNode(e));
        return p;
    }
};

/// Exactly one violation whose subject ends with `field`.
void requireSingleViolation(const Project& project, const std::string& field)
{
    const auto errors = findInvariantViolations(project);
    INFO("expected violation of " << field);
    for (const auto& e : errors)
    {
        INFO(e.describe());
    }
    REQUIRE(errors.size() == 1);
    REQUIRE(errors.front().code == ErrorCode::invalidData);
    const auto& subject = errors.front().subject;
    REQUIRE(subject.size() >= field.size());
    REQUIRE(subject.compare(subject.size() - field.size(), field.size(), field) == 0);

    const auto result = validate(project);
    REQUIRE_FALSE(result.ok());
    REQUIRE(result.error().subject == subject);
}

} // namespace

TEST_CASE("Fixture project is valid", "[model][validation]")
{
    const Fixture f;
    const auto project = f.build();
    REQUIRE(findInvariantViolations(project).empty());
    REQUIRE(validate(project).ok());
    REQUIRE(eventEnd(*project.events[0], project) == TimeTicks::fromSeconds(3));
}

TEST_CASE("Ids must be present and unique", "[model][validation]")
{
    SECTION("nil clip id")
    {
        Fixture f;
        f.clip.id = {};
        requireSingleViolation(f.build(), "clip 00000000-0000-0000-0000-000000000000");
    }
    SECTION("nil project id")
    {
        Fixture f;
        f.project.id = {};
        const auto errors = findInvariantViolations(f.build());
        REQUIRE(errors.size() == 1);
        REQUIRE(errors.front().message == "Id is nil");
    }
    SECTION("layer reuses the clip id")
    {
        Fixture f;
        f.layer.id.uuid = f.clip.id.uuid;
        const auto errors = findInvariantViolations(f.build());
        REQUIRE(errors.size() == 1);
        REQUIRE(errors.front().message == "Id is used by more than one entity");
        REQUIRE(errors.front().subject == "clip " + f.clip.id.uuid.toString());
    }
    SECTION("same clip node twice in a layer")
    {
        Fixture f;
        auto project = f.build();
        project.events[0] = modified(project.events[0],
                                     [](Event& e) {
                                         e.layers[0] = modified(e.layers[0], [](Layer& l)
                                                                { l.clips.push_back(l.clips[0]); });
                                     });
        REQUIRE(findInvariantViolations(project).size() == 1);
    }
}

TEST_CASE("Null nodes are reported", "[model][validation]")
{
    Fixture f;
    auto project = f.build();
    project.events.push_back(nullptr);
    project.assets.push_back(nullptr);
    REQUIRE(findInvariantViolations(project).size() == 2);
}

TEST_CASE("Asset properties are checked", "[model][validation]")
{
    struct Case
    {
        const char* name;
        std::function<void(AssetRef&)> breakIt;
        const char* field;
    };
    const std::vector<Case> cases = {
        {"empty path", [](AssetRef& a) { a.absolutePath.clear(); }, ".absolutePath"},
        {"sample rate too low", [](AssetRef& a) { a.info.sampleRate = 7'999; }, ".sampleRate"},
        {"sample rate too high", [](AssetRef& a) { a.info.sampleRate = 768'001; }, ".sampleRate"},
        {"no channels", [](AssetRef& a) { a.info.channels = 0; }, ".channels"},
        {"too many channels", [](AssetRef& a) { a.info.channels = limits::maxAssetChannels + 1; },
         ".channels"},
        {"no frames", [](AssetRef& a) { a.info.frames = 0; }, ".frames"},
        {"longer than 24 hours", [](AssetRef& a) { a.info.frames = 48'000LL * 86'400 + 1; },
         ".frames"},
    };
    for (const auto& c : cases)
    {
        DYNAMIC_SECTION(c.name)
        {
            Fixture f;
            f.clip.sourceOutFrames = 1; // keep the clip valid while the asset is broken
            f.clip.fadeIn.length = {};
            f.clip.fadeOut.length = {};
            c.breakIt(f.asset);
            const auto errors = findInvariantViolations(f.build());
            REQUIRE_FALSE(errors.empty());
            const auto& subject = errors.front().subject;
            REQUIRE(subject.rfind("asset ", 0) == 0);
            REQUIRE(subject.substr(subject.size() - std::string(c.field).size()) == c.field);
        }
    }

    SECTION("exactly 24 hours is allowed")
    {
        Fixture f;
        f.asset.info.frames = 48'000LL * 86'400;
        REQUIRE(validate(f.build()).ok());
    }
    SECTION("supported limits are allowed")
    {
        Fixture f;
        f.asset.info.channels = limits::maxAssetChannels;
        f.asset.info.sampleRate = 768'000; // the 2 s asset becomes 0.125 s, so drop the fades
        f.clip.fadeIn.length = {};
        f.clip.fadeOut.length = {};
        f.project.settings.defaultSampleRate = 8'000;
        REQUIRE(validate(f.build()).ok());
    }
}

TEST_CASE("Clip invariants are checked", "[model][validation]")
{
    constexpr auto nan = std::numeric_limits<double>::quiet_NaN();
    constexpr auto inf = std::numeric_limits<double>::infinity();
    struct Case
    {
        const char* name;
        std::function<void(Clip&)> breakIt;
        const char* field;
    };
    const std::vector<Case> cases = {
        {"unknown asset", [](Clip& c) { c.asset = AssetId::generate(); }, ".asset"},
        {"negative source in", [](Clip& c) { c.sourceInFrames = -1; }, ".sourceFrames"},
        {"empty source range", [](Clip& c) { c.sourceInFrames = c.sourceOutFrames; },
         ".sourceFrames"},
        {"reversed source range",
         [](Clip& c)
         {
             c.sourceInFrames = 10;
             c.sourceOutFrames = 5;
         },
         ".sourceFrames"},
        {"source past asset end", [](Clip& c) { c.sourceOutFrames = 96'001; }, ".sourceFrames"},
        {"negative position", [](Clip& c) { c.position = TimeTicks{-1}; }, ".position"},
        {"ends after 24 hours", [](Clip& c)
         { c.position = TimeTicks{limits::maxEventLength.value - TimeTicks::perSecond}; },
         ".position"},
        {"negative fade in", [](Clip& c) { c.fadeIn.length = TimeTicks{-1}; }, ".fadeIn"},
        {"negative fade out", [](Clip& c) { c.fadeOut.length = TimeTicks{-1}; }, ".fadeOut"},
        {"fades longer than clip",
         [](Clip& c)
         {
             c.fadeIn.length = TimeTicks::fromSeconds(1);
             c.fadeOut.length = TimeTicks{TimeTicks::fromSeconds(1).value + 1};
         },
         ".fades"},
        {"gain too high", [](Clip& c) { c.gainDb = 24.001; }, ".gainDb"},
        {"gain too low", [](Clip& c) { c.gainDb = -96.001; }, ".gainDb"},
        {"gain NaN", [=](Clip& c) { c.gainDb = nan; }, ".gainDb"},
        {"gain infinite", [=](Clip& c) { c.gainDb = -inf; }, ".gainDb"},
    };
    for (const auto& c : cases)
    {
        DYNAMIC_SECTION(c.name)
        {
            Fixture f;
            c.breakIt(f.clip);
            requireSingleViolation(f.build(), c.field);
        }
    }

    SECTION("boundaries are allowed")
    {
        Fixture f;
        f.clip.position = TimeTicks{limits::maxEventLength.value - TimeTicks::fromSeconds(2).value};
        f.clip.fadeIn.length = TimeTicks::fromSeconds(1);
        f.clip.fadeOut.length = TimeTicks::fromSeconds(1);
        f.clip.gainDb = limits::maxGainDb;
        f.clip.reverse = true;
        REQUIRE(validate(f.build()).ok());
        f.clip.gainDb = limits::minGainDb;
        f.clip.position = {};
        REQUIRE(validate(f.build()).ok());
    }
}

TEST_CASE("Layer and event parameters are checked", "[model][validation]")
{
    SECTION("pan out of range")
    {
        Fixture f;
        f.layer.pan = 1.01;
        requireSingleViolation(f.build(), ".pan");
    }
    SECTION("pan NaN")
    {
        Fixture f;
        f.layer.pan = std::nan("");
        requireSingleViolation(f.build(), ".pan");
    }
    SECTION("layer gain")
    {
        Fixture f;
        f.layer.gainDb = 30.0;
        requireSingleViolation(f.build(), ".gainDb");
    }
    SECTION("colour wider than 24 bits")
    {
        Fixture f;
        f.layer.colourRgb = 0x01000000;
        requireSingleViolation(f.build(), ".colour");
    }
    SECTION("master gain")
    {
        Fixture f;
        f.event.masterGainDb = -100.0;
        requireSingleViolation(f.build(), ".masterGainDb");
    }
    SECTION("project sample rate")
    {
        Fixture f;
        f.project.settings.defaultSampleRate = 0;
        requireSingleViolation(f.build(), ".defaultSampleRate");
    }
}

TEST_CASE("All violations are reported, validate() returns the first", "[model][validation]")
{
    Fixture f;
    f.layer.pan = 2.0;
    f.clip.gainDb = 100.0;
    f.event.masterGainDb = 100.0;
    const auto project = f.build();
    const auto errors = findInvariantViolations(project);
    REQUIRE(errors.size() == 3);
    REQUIRE(validate(project).error().subject == errors.front().subject);
}
