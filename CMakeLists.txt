cmake_minimum_required(VERSION 3.22)
project(KompaLead VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
  JUCE
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG 7.0.12
  GIT_SHALLOW ON)
FetchContent_MakeAvailable(JUCE)

set(KOMPA_FORMATS VST3 Standalone)
if(APPLE)
  list(APPEND KOMPA_FORMATS AU)
endif()

juce_add_plugin(KompaLead
  COMPANY_NAME "KompaLabs"
  IS_SYNTH TRUE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  PLUGIN_MANUFACTURER_CODE Kmpl
  PLUGIN_CODE Kpld
  FORMATS ${KOMPA_FORMATS}
  PRODUCT_NAME "Kompa Lead")

target_sources(KompaLead PRIVATE
  PluginProcessor.cpp)

target_compile_definitions(KompaLead PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(KompaLead
  PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
  PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags)
