# =============================================================================
#  TWISTED BREAKS - MPC EDITION
#  Builds Twisted Breaks as a native MPC OS plugin (Linux VST2, armv7 for Gen1
#  MPCs and the Force, x86_64 for the offline host test).
#
#  The engine is the real Twisted Breaks processor (submodule
#  external/twisted-breaks, untouched). src/MpcHost.cpp hosts it behind the
#  small engine interface of the community MPC plugin toolkit
#  (submodule external/mpc-vst-plugins), whose VST2 wrapper MPC loads.
#
#  JUCE modules are compiled directly (not through juce_add_plugin) so the
#  build controls exactly what the plugin links: no ALSA, no FreeType, no
#  fontconfig, X11 only as headers (JUCE loads it at runtime, and never does
#  here because nothing opens a window).
# =============================================================================
cmake_minimum_required(VERSION 3.22)
project(TwistedMpcBreaks VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(TB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/twisted-breaks)
set(MV_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/mpc-vst-plugins)
set(VST_DIR ${CMAKE_CURRENT_SOURCE_DIR}/vst)

if(NOT EXISTS ${TB_DIR}/Source/PluginProcessor.cpp OR NOT EXISTS ${MV_DIR}/wrapper/vst2_wrap.c)
    message(FATAL_ERROR "Submodules missing: run  git submodule update --init")
endif()

set(TBM_JUCE_PATH "" CACHE PATH "Local JUCE 8.0.4 checkout (empty = download)")
set(TBM_X11_INCLUDE "" CACHE PATH "Directory holding X11/ headers (cross builds: the toolchain sysroot has none)")
option(TBM_HOST_TEST "Build the offline host tests (native builds only)" ON)
option(TBM_SANITIZE "Build with ASan/UBSan (host test builds)" OFF)

# -----------------------------------------------------------------------------
#  JUCE: sources only, the same version Twisted Breaks builds with
# -----------------------------------------------------------------------------
if(TBM_JUCE_PATH)
    set(JUCE_DIR ${TBM_JUCE_PATH})
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.4
        GIT_SHALLOW    TRUE)
    FetchContent_GetProperties(JUCE)
    if(NOT juce_POPULATED)
        FetchContent_Populate(JUCE)
    endif()
    set(JUCE_DIR ${juce_SOURCE_DIR})
endif()
set(JM ${JUCE_DIR}/modules)

# -----------------------------------------------------------------------------
#  FreeType, linked in statically: JUCE's Linux font code needs it to compile.
#  Nothing draws text here (MPC draws the page from the skin), and a static
#  copy means the plugin needs nothing from the device's own libraries.
# -----------------------------------------------------------------------------
set(TBM_FREETYPE_PATH "" CACHE PATH "Local FreeType 2.13.3 checkout (empty = download)")
if(TBM_FREETYPE_PATH)
    set(FT_DIR ${TBM_FREETYPE_PATH})
else()
    include(FetchContent)
    FetchContent_Declare(freetype
        GIT_REPOSITORY https://github.com/freetype/freetype.git
        GIT_TAG        VER-2-13-3
        GIT_SHALLOW    TRUE)
    FetchContent_GetProperties(freetype)
    if(NOT freetype_POPULATED)
        FetchContent_Populate(freetype)
    endif()
    set(FT_DIR ${freetype_SOURCE_DIR})
endif()
foreach(dep ZLIB BZIP2 PNG HARFBUZZ BROTLI)
    set(FT_DISABLE_${dep} TRUE CACHE BOOL "" FORCE)
endforeach()
set(BUILD_SHARED_LIBS OFF)
set(SKIP_INSTALL_ALL ON CACHE BOOL "" FORCE)
add_subdirectory(${FT_DIR} ${CMAKE_BINARY_DIR}/freetype EXCLUDE_FROM_ALL)
set_target_properties(freetype PROPERTIES POSITION_INDEPENDENT_CODE ON C_VISIBILITY_PRESET hidden)

set(TBM_JUCE_MODULES
    juce_core juce_events juce_data_structures juce_graphics juce_gui_basics juce_gui_extra
    juce_audio_basics juce_audio_devices juce_audio_formats juce_audio_processors juce_audio_utils juce_dsp)

set(JUCE_SOURCES
    ${JM}/juce_core/juce_core.cpp
    ${JM}/juce_core/juce_core_CompilationTime.cpp
    ${JM}/juce_events/juce_events.cpp
    ${JM}/juce_data_structures/juce_data_structures.cpp
    ${JM}/juce_graphics/juce_graphics.cpp
    ${JM}/juce_graphics/juce_graphics_Harfbuzz.cpp
    ${JM}/juce_graphics/juce_graphics_Sheenbidi.c
    ${JM}/juce_gui_basics/juce_gui_basics.cpp
    ${JM}/juce_gui_extra/juce_gui_extra.cpp
    ${JM}/juce_audio_basics/juce_audio_basics.cpp
    ${JM}/juce_audio_devices/juce_audio_devices.cpp
    ${JM}/juce_audio_formats/juce_audio_formats.cpp
    ${JM}/juce_audio_processors/juce_audio_processors.cpp
    ${JM}/juce_audio_utils/juce_audio_utils.cpp
    ${JM}/juce_dsp/juce_dsp.cpp)

set(TBM_DEFINES
    NDEBUG=1
    JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1
    JUCE_STANDALONE_APPLICATION=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_ALSA=0
    JUCE_JACK=0
    JUCE_BELA=0
    JUCE_USE_FREETYPE=1
    JUCE_USE_FONTCONFIG=0
    JUCE_USE_XRANDR=0
    JUCE_USE_XINERAMA=0
    JUCE_USE_XSHM=0
    JUCE_USE_XRENDER=0
    JUCE_USE_XCURSOR=0
    JUCE_MODAL_LOOPS_PERMITTED=0
    JUCE_PLUGINHOST_VST3=0
    JUCE_PLUGINHOST_LV2=0
    JUCE_PLUGINHOST_LADSPA=0
    JUCE_PLUGINHOST_ARA=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_CHECK_MEMORY_LEAKS=0
    JUCE_LOG_ASSERTIONS=0
    # Twisted Breaks reads these the way a plugin build defines them
    JucePlugin_Name="TwistedBreaks"
    JucePlugin_IsSynth=1
    JucePlugin_WantsMidiInput=1
    JucePlugin_ProducesMidiOutput=0
    JucePlugin_IsMidiEffect=0
    JucePlugin_VersionString="${PROJECT_VERSION}"
    TB_MPC=1)
foreach(m ${TBM_JUCE_MODULES})
    list(APPEND TBM_DEFINES JUCE_MODULE_AVAILABLE_${m}=1)
endforeach()

# -----------------------------------------------------------------------------
#  Twisted Breaks: every source, as its own build compiles them (the editor is
#  linked but never opened: MPC draws the plugin's page from the skin)
# -----------------------------------------------------------------------------
include(${TB_DIR}/cmake/Sources.cmake)

# -----------------------------------------------------------------------------
#  The wrapper's parameter table, from vst/vst.json + vst/params.json
# -----------------------------------------------------------------------------
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(PARAMS_H ${VST_DIR}/build/params.h)
add_custom_command(OUTPUT ${PARAMS_H}
    COMMAND ${Python3_EXECUTABLE} ${MV_DIR}/tools/gen_vst.py ${VST_DIR}/vst.json --params-h
    DEPENDS ${VST_DIR}/vst.json ${VST_DIR}/params.json ${VST_DIR}/layout.conf
    COMMENT "params.h from vst/params.json")
add_custom_target(tbm_params DEPENDS ${PARAMS_H})

# The wrapper keeps the plugin state (a project's save) in a fixed 8 KB text
# buffer; Twisted Breaks' state (patterns, slices, snapshots) is larger. Build a
# copy with the buffer size set here. Fails loudly if the wrapper changes.
set(WRAP_SRC ${MV_DIR}/wrapper/vst2_wrap.c)
set(WRAP_PATCHED ${CMAKE_CURRENT_BINARY_DIR}/vst2_wrap_tbm.c)
file(READ ${WRAP_SRC} WRAP_TEXT)
string(FIND "${WRAP_TEXT}" "char chunk[8192];" WRAP_AT)
if(WRAP_AT EQUAL -1)
    message(FATAL_ERROR "vst2_wrap.c no longer has 'char chunk[8192];': check its state handling and update this patch")
endif()
string(REPLACE "char chunk[8192];" "char chunk[TBM_CHUNK_MAX];" WRAP_TEXT "${WRAP_TEXT}")
file(WRITE ${WRAP_PATCHED}.tmp "${WRAP_TEXT}")
configure_file(${WRAP_PATCHED}.tmp ${WRAP_PATCHED} COPYONLY)
set(TBM_CHUNK_MAX 4194304)   # 4 MB: compressed state of a full kit with every pattern is far smaller

# -----------------------------------------------------------------------------
#  Objects shared by the plugin and the host tests
# -----------------------------------------------------------------------------
add_library(tbm_objs OBJECT ${JUCE_SOURCES} ${TB_PLUGIN_SOURCES} src/MpcHost.cpp)
add_dependencies(tbm_objs tbm_params)
target_include_directories(tbm_objs PRIVATE ${JM} ${TB_DIR}/Source ${MV_DIR}/wrapper ${VST_DIR}/build src)
if(TBM_X11_INCLUDE)
    target_include_directories(tbm_objs SYSTEM PRIVATE ${TBM_X11_INCLUDE})
endif()
target_compile_definitions(tbm_objs PRIVATE ${TBM_DEFINES})
target_include_directories(tbm_objs PRIVATE $<TARGET_PROPERTY:freetype,INTERFACE_INCLUDE_DIRECTORIES>)
target_compile_options(tbm_objs PRIVATE
    -fvisibility=hidden -fvisibility-inlines-hidden -ffunction-sections -fdata-sections
    $<$<COMPILE_LANGUAGE:CXX>:-Wno-deprecated-declarations>)

add_library(tbm_wrap OBJECT ${WRAP_PATCHED})
add_dependencies(tbm_wrap tbm_params)
set_source_files_properties(${WRAP_PATCHED} PROPERTIES LANGUAGE C)
target_include_directories(tbm_wrap PRIVATE ${VST_DIR}/build ${MV_DIR}/wrapper)
target_compile_definitions(tbm_wrap PRIVATE TBM_CHUNK_MAX=${TBM_CHUNK_MAX})
target_compile_options(tbm_wrap PRIVATE -std=gnu11 -fvisibility=hidden)

if(TBM_SANITIZE)
    foreach(t tbm_objs tbm_wrap)
        target_compile_options(${t} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    endforeach()
endif()

set(TBM_LINK_LIBS pthread dl rt m)

# -----------------------------------------------------------------------------
#  The plugin: one .so exporting only VSTPluginMain
# -----------------------------------------------------------------------------
add_library(twistedbreaks SHARED $<TARGET_OBJECTS:tbm_objs> $<TARGET_OBJECTS:tbm_wrap>)
set_target_properties(twistedbreaks PROPERTIES PREFIX "" OUTPUT_NAME "twistedbreaks")
target_link_libraries(twistedbreaks PRIVATE freetype ${TBM_LINK_LIBS})
target_link_options(twistedbreaks PRIVATE
    -Wl,--no-undefined -Wl,--gc-sections -Wl,-Bsymbolic
    -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/src/exports.map
    -static-libstdc++ -static-libgcc)
if(TBM_SANITIZE)
    target_link_options(twistedbreaks PRIVATE -fsanitize=address,undefined)
endif()

# -----------------------------------------------------------------------------
#  Offline tests (x86_64): the toolkit's generic host test, and our own
# -----------------------------------------------------------------------------
if(TBM_HOST_TEST AND NOT CMAKE_CROSSCOMPILING)
    # The toolkit's host test drives VSTPluginMain the way MPC does (params, options, state, notes).
    add_executable(toolkit_host_test ${MV_DIR}/tools/host_test.c)
    add_dependencies(toolkit_host_test tbm_params)
    target_include_directories(toolkit_host_test PRIVATE ${VST_DIR}/build)
    target_compile_options(toolkit_host_test PRIVATE -std=gnu11)
    target_link_libraries(toolkit_host_test PRIVATE twistedbreaks m pthread)
    set_target_properties(toolkit_host_test PROPERTIES BUILD_RPATH "$ORIGIN")

    # Twisted Breaks specific: kits, pads, sequencer from the song position, state size.
    add_executable(tbm_test tests/tbm_test.c)
    add_dependencies(tbm_test tbm_params)
    target_include_directories(tbm_test PRIVATE ${VST_DIR}/build)
    target_compile_options(tbm_test PRIVATE -std=gnu11)
    target_link_libraries(tbm_test PRIVATE twistedbreaks m pthread)
    set_target_properties(tbm_test PROPERTIES BUILD_RPATH "$ORIGIN")
    if(TBM_SANITIZE)
        target_link_options(toolkit_host_test PRIVATE -fsanitize=address,undefined)
        target_link_options(tbm_test PRIVATE -fsanitize=address,undefined)
    endif()
endif()
