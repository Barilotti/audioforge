// Test runner for modules that use JUCE (app, ui services): JUCE needs a MessageManager on the
// test thread, and its singletons must be destroyed before the leak detector runs.
#include <catch2/catch_session.hpp>
#include <juce_events/juce_events.h>

int main(int argc, char* argv[])
{
    juce::MessageManager::getInstance()->setCurrentThreadAsMessageThread();
    const int result = Catch::Session().run(argc, argv);
    juce::DeletedAtShutdown::deleteAll();
    juce::MessageManager::deleteInstance();
    return result;
}
