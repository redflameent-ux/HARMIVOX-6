cmake_minimum_required(VERSION 3.22)

project(HARMIVOX6 VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# ---------------------------------------------------------------------------
# JUCE (downloaded automatically; pass -DFETCHCONTENT_SOURCE_DIR_JUCE=<path>
# to use a local copy instead)
# ---------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

option(HVX_BUILD_TESTS "Build the offline engine test runner" OFF)

# ---------------------------------------------------------------------------
# Embedded fonts
# ---------------------------------------------------------------------------
juce_add_binary_data(HvxBinaryData SOURCES
    Resources/fonts/Barlow-Regular.ttf
    Resources/fonts/Barlow-Medium.ttf
    Resources/fonts/Barlow-SemiBold.ttf
    Resources/fonts/Barlow-Bold.ttf
    Resources/fonts/BarlowCondensed-SemiBold.ttf
    Resources/fonts/BarlowCondensed-Bold.ttf
    Resources/fonts/ShareTechMono-Regular.ttf
    Resources/images/logo.png)

set(HVX_ENGINE_SOURCES
    Source/engine/Model.cpp
    Source/engine/Analysis.cpp
    Source/engine/Renderer.cpp
    Source/engine/Cleaner.cpp
    Source/engine/Harmony.cpp
    Source/engine/Aligner.cpp
    Source/engine/Finish.cpp
    Source/engine/VoiceProfile.cpp
    Source/engine/Session.cpp)

set(HVX_UI_SOURCES
    Source/ui/Theme.cpp
    Source/ui/Widgets.cpp
    Source/ui/TrackLanes.cpp
    Source/ui/NoteEditor.cpp
    Source/ui/SidePanels.cpp
    Source/ui/AddVoicesDialog.cpp
    Source/ui/VoiceLab.cpp
    Source/ui/MainEditor.cpp
    Source/ui/DevSnapshot.cpp)

# ---------------------------------------------------------------------------
# The plugin (VST3 for FL Studio / Ableton, plus a standalone app)
# ---------------------------------------------------------------------------
juce_add_plugin(Harmivox6
    PRODUCT_NAME "HARMIVOX-6"
    COMPANY_NAME "RedFlame Ent"
    BUNDLE_ID com.redflame.harmivox6
    PLUGIN_MANUFACTURER_CODE Rfle
    PLUGIN_CODE Hvx6
    FORMATS VST3 Standalone
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "HARMIVOX-6 records your voice for the Voice lab."
    VST3_CATEGORIES Fx Pitch Shift
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(Harmivox6 PRIVATE
    Source/PluginProcessor.cpp
    ${HVX_ENGINE_SOURCES}
    ${HVX_UI_SOURCES})

target_include_directories(Harmivox6 PRIVATE Source)

target_compile_definitions(Harmivox6 PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Harmivox6
    PRIVATE
        HvxBinaryData
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Offline engine tests (renders WAVs and checks pitch, used during development)
# ---------------------------------------------------------------------------
if(HVX_BUILD_TESTS)
    juce_add_console_app(HvxTests PRODUCT_NAME "HvxTests")
    target_sources(HvxTests PRIVATE Tests/EngineTests.cpp ${HVX_ENGINE_SOURCES})
    target_include_directories(HvxTests PRIVATE Source)
    target_compile_definitions(HvxTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 HVX_HEADLESS=1)
    target_link_libraries(HvxTests PRIVATE
        juce::juce_audio_basics juce::juce_audio_formats juce::juce_dsp juce::juce_events juce::juce_graphics
        juce::juce_recommended_config_flags)
endif()

