cmake_minimum_required(VERSION 3.22)

project(LfoStudio VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

option(LFO_BUILD_TESTS "Build the LFO engine unit tests" ON)
option(LFO_ONLY_TESTS "Build only the unit tests (does not download JUCE)" OFF)

if(NOT LFO_ONLY_TESTS)
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.6
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)

    juce_add_plugin(LfoStudio
        PRODUCT_NAME "LFO Studio"
        COMPANY_NAME "WolfBlack"
        BUNDLE_ID "com.wolfblack.lfostudio"
        PLUGIN_MANUFACTURER_CODE Wlfb
        PLUGIN_CODE Lfos
        FORMATS VST3 Standalone
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT TRUE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        VST3_CATEGORIES Fx Modulation
        COPY_PLUGIN_AFTER_BUILD FALSE)

    target_sources(LfoStudio PRIVATE
        Source/LfoEngine.h
        Source/PluginProcessor.h
        Source/PluginProcessor.cpp
        Source/PluginEditor.h
        Source/PluginEditor.cpp)

    target_compile_definitions(LfoStudio PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)

    target_link_libraries(LfoStudio
        PRIVATE
            juce::juce_audio_utils
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
endif()

if(LFO_BUILD_TESTS)
    enable_testing()
    add_executable(LfoEngineTests Tests/EngineTests.cpp)
    target_include_directories(LfoEngineTests PRIVATE Source)
    add_test(NAME LfoEngineTests COMMAND LfoEngineTests)

    if(NOT LFO_ONLY_TESTS)
        # Runs the real processor headlessly, reusing the plugin's already-compiled code.
        add_executable(LfoProcessorTests Tests/ProcessorTests.cpp)
        target_link_libraries(LfoProcessorTests PRIVATE LfoStudio)
        target_include_directories(LfoProcessorTests PRIVATE Source
            $<TARGET_PROPERTY:LfoStudio,INCLUDE_DIRECTORIES>)
        target_compile_definitions(LfoProcessorTests PRIVATE
            $<TARGET_PROPERTY:LfoStudio,COMPILE_DEFINITIONS>)
        add_test(NAME LfoProcessorTests COMMAND LfoProcessorTests)
    endif()
endif()
