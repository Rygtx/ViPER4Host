cmake_minimum_required(VERSION 3.22)

set(VIPER4HOST_PLUGIN_VERSION "2026.10.28")
project(ViPER4Host VERSION ${VIPER4HOST_PLUGIN_VERSION} LANGUAGES C CXX RC)

set(JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/extern/JUCE" CACHE PATH "Path to the JUCE repository")
set(FST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/extern/FST" CACHE PATH "Path to the FST repository")
set(VIPERDSP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/extern/ViPERDSP" CACHE PATH "Path to the ViPERDSP engine")

if(NOT EXISTS "${JUCE_DIR}/CMakeLists.txt")
  message(FATAL_ERROR "JUCE not found at ${JUCE_DIR}. Run 'git submodule update --init --recursive' first.")
endif()

if(NOT EXISTS "${FST_DIR}/fst/aeffectx.h")
  message(FATAL_ERROR "FST headers not found at ${FST_DIR}. Run 'git submodule update --init --recursive' first.")
endif()

if(NOT EXISTS "${VIPERDSP_DIR}/viper/ViPER.cpp")
  message(FATAL_ERROR "ViPERDSP not found at ${VIPERDSP_DIR}. Run 'git submodule update --init --recursive' first.")
endif()

# JUCE 的 VST2 后端需要老的 Steinberg VST2 SDK 头文件；FST 提供
# 无许可证问题的等价物，按 JUCE 期望的目录布局拷贝过去。
set(VST2_SDK_COMPAT_DIR "${CMAKE_CURRENT_BINARY_DIR}/juce_vst2_sdk")
set(VST2_SDK_COMPAT_HEADERS "${VST2_SDK_COMPAT_DIR}/pluginterfaces/vst2.x")
file(MAKE_DIRECTORY "${VST2_SDK_COMPAT_HEADERS}")
configure_file("${FST_DIR}/fst/aeffect.h" "${VST2_SDK_COMPAT_HEADERS}/aeffect.h" COPYONLY)
configure_file("${FST_DIR}/fst/aeffectx.h" "${VST2_SDK_COMPAT_HEADERS}/aeffectx.h" COPYONLY)
configure_file("${FST_DIR}/fst/fst.h" "${VST2_SDK_COMPAT_HEADERS}/fst.h" COPYONLY)

add_subdirectory("${VIPERDSP_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/ViPERDSP")

# ViPERDSP 正常情况下面向 gcc/clang；MSVC 需要 _USE_MATH_DEFINES 才有
# M_PI，它设置的 -O3/-flto 选项在这里只会产生警告。
if(MSVC)
  target_compile_definitions(ViPERDSP PRIVATE _USE_MATH_DEFINES NOMINMAX)
  set_property(TARGET ViPERDSP PROPERTY CXX_STANDARD 20) # Cure.cpp 用了指定初始化器
endif()
add_subdirectory("${JUCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/JUCE")

juce_set_vst2_sdk_path("${VST2_SDK_COMPAT_DIR}")

juce_add_plugin(ViPER4Host
  VERSION "${PROJECT_VERSION}"
  COMPANY_NAME "ViPER4Host"
  BUNDLE_ID "local.viper4host.plugin"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  COPY_PLUGIN_AFTER_BUILD FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  PLUGIN_MANUFACTURER_CODE Vp4h
  PLUGIN_CODE Vp4H
  FORMATS VST VST3 Standalone # 独立版 exe 兼作 UI 预览载体
  PRODUCT_NAME "ViPER4Host"
  VST2_CATEGORY kPlugCategEffect
  VST3_CATEGORIES Fx
)

juce_generate_juce_header(ViPER4Host)

file(GLOB VIPER4HOST_LOCALISATION_FILES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/Source/Localisation/*.json"
)

juce_add_binary_data(ViPER4HostLocalisationData
  NAMESPACE Viper4HostLocalisationData
  HEADER_NAME LocalisationBinaryData.h
  SOURCES
    ${VIPER4HOST_LOCALISATION_FILES}
)

target_sources(ViPER4Host
  PRIVATE
    Source/Localisation.cpp
    Source/Localisation.h
    Source/EqCurve.h
    Source/SiView.h
    Source/XoverStrip.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
)

target_include_directories(ViPER4Host
  PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/Source"
)

target_compile_definitions(ViPER4Host
  PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_REPORT_APP_USAGE=0
    # JuceHeader.h 会 `using namespace juce;`，把 juce::IIRFilter 带进
    # 非限定名字查找，与 ViPERDSP 的全局 IIRFilter 冲突。
    DONT_SET_USING_JUCE_NAMESPACE=1
)

target_compile_options(ViPER4Host
  PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
)

target_link_libraries(ViPER4Host
  PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp # 频谱分析器 FFT
    ViPER4HostLocalisationData
    ViPERDSP
  PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

set_target_properties(ViPER4Host PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED YES
  CXX_EXTENSIONS NO
)
