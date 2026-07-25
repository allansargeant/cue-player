cmake_minimum_required(VERSION 3.22)

project(CuePlayer VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS deployment target")

if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS build architectures")
endif()

# --------------------------------------------------------------------------------------
# Optional Steinberg ASIO SDK (Windows only).
#
# The ASIO SDK licence forbids redistribution, so it is never vendored here. Download it
# from https://www.steinberg.net/developers/ and point CMake at the unpacked folder:
#
#     cmake -B build -DCUEPLAYER_ASIO_SDK_PATH=C:/SDKs/asiosdk_2.3.3
#
# Without it, Windows builds still get WASAPI (shared + exclusive) and DirectSound.
# --------------------------------------------------------------------------------------
set(CUEPLAYER_ASIO_SDK_PATH "" CACHE PATH "Path to the unpacked Steinberg ASIO SDK (Windows only)")

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.6
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(JUCE)

juce_add_gui_app(CuePlayer
    COMPANY_NAME "Allan Sargeant"
    BUNDLE_ID "com.allansargeant.cueplayer"
    PRODUCT_NAME "Cue Player"
    VERSION ${PROJECT_VERSION}
    NEEDS_CURL FALSE
    NEEDS_WEB_BROWSER FALSE
)

juce_generate_juce_header(CuePlayer)

target_sources(CuePlayer
    PRIVATE
        Source/Main.cpp
        Source/App/MainComponent.cpp

        Source/Model/Cue.cpp
        Source/Model/CueList.cpp
        Source/Model/Show.cpp
        Source/Model/FadeCurve.cpp

        Source/Audio/AudioEngine.cpp
        Source/Audio/CueVoice.cpp
        Source/Audio/SampleSource.cpp
        Source/Audio/SampleCache.cpp

        Source/GUI/LookAndFeel.cpp
        Source/GUI/TransportBar.cpp
        Source/GUI/CueListComponent.cpp
        Source/GUI/CueInspector.cpp
        Source/GUI/WaveformComponent.cpp
        Source/GUI/RoutingMatrixComponent.cpp
        Source/GUI/AudioSetupWindow.cpp
        Source/GUI/ActiveCuesComponent.cpp
)

target_include_directories(CuePlayer PRIVATE Source)

target_compile_definitions(CuePlayer
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:CuePlayer,JUCE_PRODUCT_NAME>"
        JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:CuePlayer,JUCE_VERSION>"
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_MODAL_LOOPS_PERMITTED=0
)

# ---- Audio backends -----------------------------------------------------------------
# Each backend is enabled only where its headers actually exist, so a build never fails
# looking for, say, jack/jack.h on a Mac.
if(WIN32)
    # WASAPI shared and exclusive (exclusive is the low-latency path), plus DirectSound
    # as the fallback for older or unusual drivers. ASIO is handled separately below.
    target_compile_definitions(CuePlayer PRIVATE
        JUCE_WASAPI=1
        JUCE_WASAPI_EXCLUSIVE=1
        JUCE_DIRECTSOUND=1)
elseif(APPLE)
    # CoreAudio is always compiled in by JUCE on macOS; nothing to switch on.
    # JACK is available on macOS too, but only if the user has installed its headers.
    find_path(CUEPLAYER_JACK_INCLUDE_DIR jack/jack.h
              PATHS /usr/local/include /opt/homebrew/include /usr/include)

    if(CUEPLAYER_JACK_INCLUDE_DIR)
        message(STATUS "Cue Player: JACK support enabled (${CUEPLAYER_JACK_INCLUDE_DIR})")
        target_compile_definitions(CuePlayer PRIVATE JUCE_JACK=1)
        target_include_directories(CuePlayer PRIVATE ${CUEPLAYER_JACK_INCLUDE_DIR})
    endif()
else()
    # Linux. ALSA is the baseline; JACK covers pro audio setups. PipeWire needs neither
    # of its own — it is reached through its ALSA and JACK compatibility layers, and
    # appears there as an ordinary device.
    target_compile_definitions(CuePlayer PRIVATE JUCE_ALSA=1)

    find_path(CUEPLAYER_JACK_INCLUDE_DIR jack/jack.h PATHS /usr/include /usr/local/include)

    if(CUEPLAYER_JACK_INCLUDE_DIR)
        message(STATUS "Cue Player: JACK support enabled")
        target_compile_definitions(CuePlayer PRIVATE JUCE_JACK=1)
        target_include_directories(CuePlayer PRIVATE ${CUEPLAYER_JACK_INCLUDE_DIR})
    else()
        message(STATUS "Cue Player: JACK headers not found (install libjack-jackd2-dev "
                       "or pipewire-jack headers to enable it). ALSA remains available.")
    endif()
endif()

# ---- ASIO ---------------------------------------------------------------------------
if(WIN32 AND CUEPLAYER_ASIO_SDK_PATH)
    if(EXISTS "${CUEPLAYER_ASIO_SDK_PATH}/common/iasiodrv.h")
        message(STATUS "Cue Player: ASIO enabled (SDK at ${CUEPLAYER_ASIO_SDK_PATH})")
        target_compile_definitions(CuePlayer PRIVATE JUCE_ASIO=1)
        target_include_directories(CuePlayer PRIVATE "${CUEPLAYER_ASIO_SDK_PATH}/common")
    else()
        message(FATAL_ERROR
            "CUEPLAYER_ASIO_SDK_PATH is set to '${CUEPLAYER_ASIO_SDK_PATH}' but "
            "common/iasiodrv.h was not found there. Point it at the unpacked ASIO SDK root.")
    endif()
elseif(WIN32)
    message(STATUS "Cue Player: ASIO disabled (set CUEPLAYER_ASIO_SDK_PATH to enable). "
                   "WASAPI and DirectSound remain available.")
endif()

# ---- Engine tests -------------------------------------------------------------------
# Console harness that drives the real CueVoice with a ramp signal and checks positions,
# fades, loops and vamps numerically. Run with: ./build/CuePlayerTests
juce_add_console_app(CuePlayerTests PRODUCT_NAME "CuePlayerTests")

target_sources(CuePlayerTests
    PRIVATE
        Tests/EngineTests.cpp
        Source/Model/Cue.cpp
        Source/Model/CueList.cpp
        Source/Model/Show.cpp
        Source/Model/FadeCurve.cpp
        Source/Audio/CueVoice.cpp
        Source/Audio/SampleSource.cpp)

target_include_directories(CuePlayerTests PRIVATE Source)

target_compile_definitions(CuePlayerTests
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_STANDALONE_APPLICATION=1)

target_link_libraries(CuePlayerTests
    PRIVATE
        juce::juce_audio_formats
        juce::juce_audio_basics
        juce::juce_events
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

enable_testing()
add_test(NAME engine COMMAND CuePlayerTests)

target_link_libraries(CuePlayer
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_dsp
        juce::juce_gui_extra
        juce::juce_osc
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
