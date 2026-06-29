cmake_minimum_required(VERSION 3.22)

project(RATTLE VERSION 0.9.2 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# JUCE (lives on the external data drive per project layout)
# ---------------------------------------------------------------------------
set(JUCE_DIR "H:/Dev_Data/SDK/JUCE" CACHE PATH "Path to the JUCE framework")
add_subdirectory(${JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE)

# ---------------------------------------------------------------------------
# Sources shared by both plug-ins (the single DSP/UI codebase)
# ---------------------------------------------------------------------------
set(RATTLE_SHARED_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/core/SampleSet.cpp
    Source/core/SampleSet.h
    Source/core/RattleEngine.cpp
    Source/core/RattleEngine.h
    Source/core/SequenceVoice.cpp
    Source/core/SequenceVoice.h
    Source/core/PreFilter.cpp
    Source/core/PreFilter.h
    Source/core/OnsetDetector.cpp
    Source/core/OnsetDetector.h
    Source/ui/WaveformView.cpp
    Source/ui/WaveformView.h
    Source/ui/RattleGraph.cpp
    Source/ui/RattleGraph.h
    Source/ui/FFTView.cpp
    Source/ui/FFTView.h
    Source/ui/ChoiceToggleGroup.h
    Source/ui/LedDot.h
    Source/ui/SlotButton.h)

set(RATTLE_COMPANY     "RimshotAudio")
set(RATTLE_MFR_CODE    Simn)   # 4 chars, manufacturer

# ---------------------------------------------------------------------------
# RATTLE FX  — audio-insert effect
# ---------------------------------------------------------------------------
juce_add_plugin(RattleFX
    COMPANY_NAME              "${RATTLE_COMPANY}"
    PLUGIN_MANUFACTURER_CODE  ${RATTLE_MFR_CODE}
    PLUGIN_CODE               Rtfx
    PRODUCT_NAME              "RATTLE FX"
    FORMATS                   VST3
    VST3_CATEGORIES           Fx
    IS_SYNTH                  FALSE
    NEEDS_MIDI_INPUT          FALSE
    NEEDS_MIDI_OUTPUT         FALSE
    IS_MIDI_EFFECT            FALSE
    COPY_PLUGIN_AFTER_BUILD   FALSE)

# ---------------------------------------------------------------------------
# RATTLE Inst — MIDI-triggered instrument
# ---------------------------------------------------------------------------
juce_add_plugin(RattleInst
    COMPANY_NAME              "${RATTLE_COMPANY}"
    PLUGIN_MANUFACTURER_CODE  ${RATTLE_MFR_CODE}
    PLUGIN_CODE               Rtin
    PRODUCT_NAME              "RATTLE Inst"
    FORMATS                   VST3
    VST3_CATEGORIES           Instrument
    IS_SYNTH                  TRUE
    NEEDS_MIDI_INPUT          TRUE
    NEEDS_MIDI_OUTPUT         FALSE
    IS_MIDI_EFFECT            FALSE
    COPY_PLUGIN_AFTER_BUILD   FALSE)

# ---------------------------------------------------------------------------
# Common configuration for both targets
# ---------------------------------------------------------------------------
foreach(target RattleFX RattleInst)
    target_sources(${target} PRIVATE ${RATTLE_SHARED_SOURCES})

    target_include_directories(${target} PRIVATE Source)

    # Source files are UTF-8 (no BOM); tell MSVC so non-ASCII string literals
    # (em-dashes, arrows in displayed text) compile correctly instead of mojibake.
    target_compile_options(${target} PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

    target_compile_definitions(${target} PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)

    target_link_libraries(${target}
        PRIVATE
            juce::juce_audio_utils
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
endforeach()

# Mode switch baked into each target
target_compile_definitions(RattleInst PUBLIC RATTLE_IS_INSTRUMENT=1)
target_compile_definitions(RattleFX   PUBLIC RATTLE_IS_INSTRUMENT=0)
