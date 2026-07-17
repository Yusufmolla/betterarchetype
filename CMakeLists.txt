cmake_minimum_required(VERSION 3.22)

project(BetterArchetype VERSION 0.0.1)

add_subdirectory(libs/JUCE)

set(BETTER_ARCHETYPE_ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/BetterArchetype_1024.png")
set(BETTER_ARCHETYPE_ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/BetterArchetype_256.png")
set(BETTER_ARCHETYPE_ICON_ARGS)
set(BETTER_ARCHETYPE_PLUGIN_FORMATS Standalone VST3)

if(APPLE)
    list(APPEND BETTER_ARCHETYPE_PLUGIN_FORMATS AU)
endif()

if(EXISTS "${BETTER_ARCHETYPE_ICON_BIG}")
    list(APPEND BETTER_ARCHETYPE_ICON_ARGS ICON_BIG "${BETTER_ARCHETYPE_ICON_BIG}")
endif()

if(EXISTS "${BETTER_ARCHETYPE_ICON_SMALL}")
    list(APPEND BETTER_ARCHETYPE_ICON_ARGS ICON_SMALL "${BETTER_ARCHETYPE_ICON_SMALL}")
endif()

juce_add_plugin(BetterArchetype
    VERSION ${PROJECT_VERSION}
    PRODUCT_NAME "BetterArchetype"
    COMPANY_NAME "Molla"

    PLUGIN_MANUFACTURER_CODE "YMol"
    PLUGIN_CODE "Bton"

    FORMATS ${BETTER_ARCHETYPE_PLUGIN_FORMATS}

    ${BETTER_ARCHETYPE_ICON_ARGS}

    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "BetterArchetype needs audio input to process your guitar signal."
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD TRUE
)

target_compile_features(BetterArchetype PRIVATE cxx_std_20)

target_include_directories(BetterArchetype
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/libs/NeuralAmpModelerCore
        ${CMAKE_CURRENT_SOURCE_DIR}/libs/NeuralAmpModelerCore/Dependencies/eigen
        ${CMAKE_CURRENT_SOURCE_DIR}/libs/NeuralAmpModelerCore/Dependencies/nlohmann
)

target_sources(BetterArchetype
    PRIVATE
        src/main.cpp
        src/core/GraphAudioProcessor.cpp
        src/core/GraphAudioProcessor.h
        src/gui/ControlPanelComponent.cpp
        src/gui/ControlPanelComponent.h
        src/gui/GraphEditor.cpp
        src/gui/GraphEditor.h
        src/gui/ModuleBrowserComponent.cpp
        src/gui/ModuleBrowserComponent.h
        src/gui/NodeCanvasComponent.cpp
        src/gui/NodeCanvasComponent.h
        src/audio/graph/AssetPaths.h
        src/audio/graph/GraphTypes.h
        src/audio/graph/ModuleRegistry.cpp
        src/audio/graph/ModuleRegistry.h
        src/audio/modules/DriveProcessor.cpp
        src/audio/modules/DriveProcessor.h
        src/audio/modules/AmpProcessor.cpp        
        src/audio/modules/AmpProcessor.h
        src/audio/modules/RouterProcessor.cpp
        src/audio/modules/RouterProcessor.h
        src/audio/modules/IRCabProcessor.cpp
        src/audio/modules/IRCabProcessor.h
        src/audio/modules/IRLoader.h
        src/audio/modules/ModuleProcessor.cpp
        src/audio/modules/ModuleProcessor.h
        src/audio/modules/NAMModelLoader.h
        src/audio/modules/NAMProcessor.cpp
        src/audio/modules/NAMProcessor.h
        src/audio/modules/SimpleConvolver.h

        # NAMCore wird direkt mitgebaut; es gibt kein separates Runtime-Library-Ziel.
        libs/NeuralAmpModelerCore/NAM/activations.cpp
        libs/NeuralAmpModelerCore/NAM/container.cpp
        libs/NeuralAmpModelerCore/NAM/conv1d.cpp
        libs/NeuralAmpModelerCore/NAM/convnet.cpp
        libs/NeuralAmpModelerCore/NAM/dsp.cpp
        libs/NeuralAmpModelerCore/NAM/get_dsp.cpp
        libs/NeuralAmpModelerCore/NAM/linear.cpp
        libs/NeuralAmpModelerCore/NAM/lstm.cpp
        libs/NeuralAmpModelerCore/NAM/ring_buffer.cpp
        libs/NeuralAmpModelerCore/NAM/util.cpp
        libs/NeuralAmpModelerCore/NAM/wavenet/a2_fast.cpp
        libs/NeuralAmpModelerCore/NAM/wavenet/model.cpp
        libs/NeuralAmpModelerCore/NAM/wavenet/slimmable.cpp
)

target_compile_definitions(BetterArchetype
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_ASIO=1
        NAM_SAMPLE_FLOAT=1
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        BETTER_ARCHETYPE_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets"
)

target_link_libraries(BetterArchetype
    PRIVATE
        juce::juce_audio_processors
        juce::juce_audio_formats
        juce::juce_dsp
        juce::juce_audio_utils
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
