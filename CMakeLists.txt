cmake_minimum_required(VERSION 3.22)

set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "macOS architectures")

project(Comma VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Зависимости скачиваются при сборке. Локально можно передать готовые копии:
#   -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE  -DFETCHCONTENT_SOURCE_DIR_CLAP_JUCE_EXTENSIONS=/path/to/clap-juce-extensions
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.10
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

# CLAP: в JUCE 8 его нет, формат даёт clap-juce-extensions (MIT)
option(COMMA_BUILD_CLAP "Build the CLAP format" ON)
if(COMMA_BUILD_CLAP)
    FetchContent_Declare(clap_juce_extensions
        GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
        GIT_TAG 6bde108329ff9fc24404b79b8b397261a75cf6a8
        GIT_SUBMODULES_RECURSE TRUE)
    FetchContent_MakeAvailable(clap_juce_extensions)
endif()

if(APPLE)
    set(COMMA_FORMATS VST3 AU Standalone)
else()
    set(COMMA_FORMATS VST3 Standalone)
endif()

# Эффект с нотным входом: аудио стерео → стерео, MIDI/MPE на входе.
# AU — Music Effect (aumf): иначе Logic не отправит в плагин MIDI.
juce_add_plugin(Comma
    COMPANY_NAME "Personal"
    BUNDLE_ID "com.personal.comma"
    PLUGIN_MANUFACTURER_CODE Prsn
    PLUGIN_CODE Cmma
    FORMATS ${COMMA_FORMATS}
    PRODUCT_NAME "Comma"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx "Pitch Shift"
    AU_MAIN_TYPE kAudioUnitType_MusicEffect)

if(COMMA_BUILD_CLAP)
    clap_juce_extensions_plugin(TARGET Comma
        CLAP_ID "com.personal.comma"
        CLAP_FEATURES audio-effect pitch-shifter stereo)
endif()

set(COMMA_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Params.cpp
    Source/Presets.cpp
    Source/Randomizer.cpp
    Source/Rack/Modules.cpp
    Source/Rack/Rack.cpp
    Source/UI/LookAndFeel.cpp
    Source/UI/Widgets.cpp
    Source/UI/RackView.cpp
    Source/UI/MainView.cpp
    Source/UI/About.cpp)

target_sources(Comma PRIVATE ${COMMA_SOURCES})
target_include_directories(Comma PRIVATE Source)

set(COMMA_DEFS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_compile_definitions(Comma PUBLIC ${COMMA_DEFS})

# Шрифты Medeo (SIL OFL): Sans — интерфейс, Mono — числа, Pixel — логотип
juce_add_binary_data(CommaFonts
    NAMESPACE CommaFonts
    HEADER_NAME BinaryData.h
    SOURCES
        Resources/Fonts/MedeoSans-Regular.ttf
        Resources/Fonts/MedeoSans-SemiBold.ttf
        Resources/Fonts/MedeoMono-Regular.ttf
        Resources/Fonts/MedeoMono-SemiBold.ttf
        Resources/Fonts/MedeoPixel-Regular.ttf)
set_target_properties(CommaFonts PROPERTIES POSITION_INDEPENDENT_CODE ON)

set(COMMA_LIBS
    CommaFonts
    juce::juce_audio_utils
    juce::juce_dsp)

target_link_libraries(Comma
    PRIVATE ${COMMA_LIBS}
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Офлайн-тест: rack (mix, bypass, параллель, задержка), состояние, undo, кубик, частоты, скриншоты интерфейса
option(COMMA_BUILD_TESTS "Build offline test" OFF)
if(COMMA_BUILD_TESTS)
    juce_add_console_app(CommaTest PRODUCT_NAME "CommaTest")
    target_sources(CommaTest PRIVATE Tests/CommaTest.cpp ${COMMA_SOURCES})
    target_include_directories(CommaTest PRIVATE Source)
    target_compile_definitions(CommaTest PRIVATE ${COMMA_DEFS} COMMA_TEST_BUILD=1 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(CommaTest PRIVATE ${COMMA_LIBS} juce::juce_recommended_config_flags)
endif()
