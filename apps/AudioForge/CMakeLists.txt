juce_add_gui_app(AudioForge
    PRODUCT_NAME "AudioForge"
    COMPANY_NAME "AudioForge"
    BUNDLE_ID "app.audioforge.AudioForge"
    VERSION "${PROJECT_VERSION}")

target_sources(AudioForge PRIVATE Main.cpp)

# JUCE sources are already compiled into af_juce (linked through af_ui); the app target must
# not link juce:: modules directly.
target_link_libraries(AudioForge PRIVATE AudioForge::ui)

af_set_warnings(AudioForge)
set_target_properties(AudioForge PROPERTIES FOLDER "Apps")
