cmake_minimum_required(VERSION 3.22)

set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "macOS architectures")

project(Comma VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)   # tuning-library использует концепты C++20
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

# Строй: разбор Scala .scl / .kbm (surge-synthesizer/tuning-library, MIT, только заголовки)
FetchContent_Declare(tuning_library
    GIT_REPOSITORY https://github.com/surge-synthesizer/tuning-library.git
    GIT_TAG d6fcc08f938cc2ad812b82f83e59e2b4070afdb7
    SOURCE_SUBDIR do-not-add)
# MTS-ESP (ODDSound, 0BSD): клиент и мастер
FetchContent_Declare(mts_esp
    GIT_REPOSITORY https://github.com/ODDSound/MTS-ESP.git
    GIT_TAG f214739b8832e7f297cb9970d0c0efbf783f1462
    SOURCE_SUBDIR do-not-add)
FetchContent_MakeAvailable(tuning_library mts_esp)

add_library(CommaMTS STATIC
    ${mts_esp_SOURCE_DIR}/Client/libMTSClient.cpp
    ${mts_esp_SOURCE_DIR}/Master/libMTSMaster.cpp)
target_include_directories(CommaMTS PUBLIC ${mts_esp_SOURCE_DIR}/Client ${mts_esp_SOURCE_DIR}/Master)
set_target_properties(CommaMTS PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(CommaMTS PUBLIC ${CMAKE_DL_LIBS})

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
    Source/Tuning/Tuning.cpp
    Source/Tuning/MTS.cpp
    Source/DSP/DSP.cpp
    Source/Notes/NoteLayer.cpp
    Source/Rack/Modules.cpp
    Source/Rack/ModulesVoice.cpp
    Source/Rack/Spectral.cpp
    Source/Rack/Rack.cpp
    Source/UI/LookAndFeel.cpp
    Source/UI/Widgets.cpp
    Source/UI/RackView.cpp
    Source/UI/ModulePanel.cpp
    Source/UI/MainView.cpp
    Source/UI/About.cpp)

target_sources(Comma PRIVATE ${COMMA_SOURCES})
target_include_directories(Comma PRIVATE Source)
target_include_directories(Comma SYSTEM PRIVATE ${tuning_library_SOURCE_DIR}/include)   # чужие заголовки — без наших предупреждений

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
    CommaMTS
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
    target_include_directories(CommaTest SYSTEM PRIVATE ${tuning_library_SOURCE_DIR}/include)
    target_compile_definitions(CommaTest PRIVATE ${COMMA_DEFS} COMMA_TEST_BUILD=1 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(CommaTest PRIVATE ${COMMA_LIBS} juce::juce_recommended_config_flags)
endif()
