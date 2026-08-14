cmake_minimum_required(VERSION 3.25)

project(RenderOJN VERSION 1.0.2 LANGUAGES CXX)

if(MSVC)
  # Release and Debug both use the supported static /MT runtime.  mp3lame's
  # vcpkg port only provides that runtime, so map Debug imports accordingly.
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
  set(CMAKE_MAP_IMPORTED_CONFIG_DEBUG Release)
endif()

option(RENDEROJN_WITH_EXTERNAL_DEPS "Build MP3/Ogg/playback support" ON)
option(RENDEROJN_BUILD_TESTS "Build tests" ON)
option(RENDEROJN_BUILD_FUZZERS "Build libFuzzer entry points" OFF)
option(RENDEROJN_BUILD_PRIVATE_TOOLS "Build private corpus-validation tools (not installed)" OFF)
option(RENDEROJN_ENABLE_SANITIZERS "Enable Address/Undefined sanitizers" OFF)
set(RENDEROJN_PACKAGE_SUFFIX "${CMAKE_SYSTEM_NAME}" CACHE STRING "Release artifact suffix")

include(GNUInstallDirs)
include(CTest)
include(cmake/RenderOJNWarnings.cmake)
include(cmake/RenderOJNPackaging.cmake)

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated/app")
configure_file(resources/windows/RenderOJN.rc.in
               ${CMAKE_CURRENT_BINARY_DIR}/RenderOJN.rc @ONLY)
configure_file(src/app/Version.hpp.in
               ${CMAKE_CURRENT_BINARY_DIR}/generated/app/Version.hpp @ONLY)

set(RENDEROJN_CORE_SOURCES
    src/core/io/ByteReader.cpp
    src/core/io/File.cpp
    src/core/crypto/Sha256.cpp
    src/core/compat/CompatibilityProfile.cpp
    src/core/format/OjnParser.cpp
    src/core/format/PackageParser.cpp
    src/core/audio/Decoder.cpp
    src/core/audio/OggNormalizer.cpp
    src/core/render/Mixer.cpp
    src/core/output/Encoder.cpp
    src/app/Cli.cpp)

# Live playback drives an OS audio device through miniaudio, which the browser
# build has no use for and cannot link against.  Everything else in the core is
# platform-neutral.
if(NOT EMSCRIPTEN)
  list(APPEND RENDEROJN_CORE_SOURCES src/core/audio/Playback.cpp)
endif()

add_library(renderojn_core STATIC ${RENDEROJN_CORE_SOURCES})
target_include_directories(renderojn_core PUBLIC src "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_compile_features(renderojn_core PUBLIC cxx_std_17)
target_compile_definitions(renderojn_core PUBLIC RENDEROJN_VERSION="${PROJECT_VERSION}")
renderojn_apply_warnings(renderojn_core)

if(WIN32)
  target_compile_definitions(renderojn_core PUBLIC _WIN32_WINNT=0x0601)
endif()

if(RENDEROJN_ENABLE_SANITIZERS AND NOT MSVC)
  target_compile_options(renderojn_core PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(renderojn_core PUBLIC -fsanitize=address,undefined)
endif()

if(RENDEROJN_WITH_EXTERNAL_DEPS)
  find_package(SndFile CONFIG REQUIRED)
  find_package(mp3lame CONFIG REQUIRED)
  find_package(TagLib CONFIG REQUIRED)
  if(NOT EMSCRIPTEN)
    # miniaudio 0.11.25 is a header-only vcpkg port without a CMake config package.
    find_path(MINIAUDIO_INCLUDE_DIR NAMES miniaudio.h REQUIRED)
    target_include_directories(renderojn_core PRIVATE ${MINIAUDIO_INCLUDE_DIR})
  endif()
  target_link_libraries(renderojn_core PUBLIC SndFile::sndfile mp3lame::mp3lame TagLib::TagLib)
  if(WIN32)
    target_link_libraries(renderojn_core PRIVATE winmm ole32 avrt)
  elseif(APPLE)
    find_library(AUDIOTOOLBOX_FRAMEWORK AudioToolbox REQUIRED)
    find_library(COREAUDIO_FRAMEWORK CoreAudio REQUIRED)
    find_library(COREFOUNDATION_FRAMEWORK CoreFoundation REQUIRED)
    target_link_libraries(renderojn_core PRIVATE ${AUDIOTOOLBOX_FRAMEWORK} ${COREAUDIO_FRAMEWORK} ${COREFOUNDATION_FRAMEWORK})
  elseif(UNIX)
    target_link_libraries(renderojn_core PRIVATE dl pthread m)
  endif()
  target_compile_definitions(renderojn_core PUBLIC RENDEROJN_EXTERNAL_DEPS=1)
endif()

if(EMSCRIPTEN)
  # The browser build ships the renderer as a module rather than a CLI: no
  # argv, no filesystem, no audio device.  Everything it exposes lives in
  # src/wasm/Bindings.cpp.
  add_executable(renderojn_wasm src/wasm/Bindings.cpp)
  target_link_libraries(renderojn_wasm PRIVATE renderojn_core)
  renderojn_apply_warnings(renderojn_wasm)
  set_target_properties(renderojn_wasm PROPERTIES OUTPUT_NAME renderojn)
  # Emscripten discards `catch` blocks unless exception catching is enabled at
  # *compile* time; the matching link flag alone is not enough.  Without this
  # the binding's translation of core errors into JavaScript Errors is silently
  # compiled away, and every failure reaches the UI as an opaque handle.  The
  # core is compiled with it too, since that is where the throws originate.
  target_compile_options(renderojn_core PUBLIC "-sDISABLE_EXCEPTION_CATCHING=0")
  target_link_options(renderojn_wasm PRIVATE
      "--bind"
      "-sMODULARIZE=1"
      "-sEXPORT_ES6=1"
      "-sEXPORT_NAME=createRenderOJN"
      "-sALLOW_MEMORY_GROWTH=1"
      # A large sample bank plus the mixed output is the memory high-water mark;
      # starting small and growing repeatedly is slower than starting here.
      "-sINITIAL_MEMORY=268435456"
      # libvorbis's analysis path (_preextrapolate_helper -> vorbis_analysis_wrote)
      # overruns Emscripten's 64KB default stack.  Native builds get 1-8MB from
      # the OS, which is why this only ever surfaces in the browser build.
      "-sSTACK_SIZE=5242880"
      # Node is included so the module can be exercised headlessly (corpus
      # verification, CI) with the same binary the browser loads.  Its loader
      # branch is guarded by a runtime environment check, so it never executes
      # in a browser; it does make the glue reference node:module, which
      # bundlers report as an externalized import.  That warning is expected.
      "-sENVIRONMENT=web,worker,node"
      # getExceptionMessage turns a thrown renderojn::Error into readable text on
      # the JavaScript side, which is what the UI shows when a file is malformed.
      "-sEXPORTED_RUNTIME_METHODS=['HEAPU8','getExceptionMessage','decrementExceptionRefcount']"
      # Errors cross into JavaScript as exceptions rather than aborting the
      # module, so a bad file fails one render instead of the whole session.
      "-sDISABLE_EXCEPTION_CATCHING=0")
else()
  add_executable(RenderOJN src/app/Main.cpp)
  target_link_libraries(RenderOJN PRIVATE renderojn_core)
  if(WIN32)
    target_sources(RenderOJN PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/RenderOJN.rc)
  endif()
  set_target_properties(RenderOJN PROPERTIES OUTPUT_NAME RenderOJN)
endif()

if(BUILD_TESTING AND RENDEROJN_BUILD_TESTS)
  find_package(Catch2 3 CONFIG REQUIRED)
  add_executable(renderojn_tests tests/unit/CoreTests.cpp tests/integration/FlowTests.cpp)
  target_link_libraries(renderojn_tests PRIVATE renderojn_core Catch2::Catch2WithMain)
  add_test(NAME renderojn_tests COMMAND renderojn_tests)
endif()

# Private verification harness for real game corpora.  It is deliberately not a
# public API and is never installed or packaged.
if(RENDEROJN_BUILD_PRIVATE_TOOLS)
  add_executable(renderojn_corpus_validate tools/corpus/CorpusValidate.cpp)
  target_link_libraries(renderojn_corpus_validate PRIVATE renderojn_core)
  renderojn_apply_warnings(renderojn_corpus_validate)
endif()

if(RENDEROJN_BUILD_FUZZERS)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "RENDEROJN_BUILD_FUZZERS requires a Clang compiler with libFuzzer support")
  endif()
  target_compile_options(renderojn_core PRIVATE -fsanitize=fuzzer-no-link,address,undefined -fno-omit-frame-pointer)
  foreach(kind IN ITEMS ojn ojm)
    add_executable(renderojn_fuzz_${kind} fuzz/${kind}_fuzz.cpp)
    target_link_libraries(renderojn_fuzz_${kind} PRIVATE renderojn_core)
    target_compile_options(renderojn_fuzz_${kind} PRIVATE -fsanitize=fuzzer,address,undefined)
    target_link_options(renderojn_fuzz_${kind} PRIVATE -fsanitize=fuzzer,address,undefined)
  endforeach()
endif()

if(NOT EMSCRIPTEN)
  install(TARGETS RenderOJN RUNTIME DESTINATION .)
  install(FILES README.md LICENSE THIRD_PARTY_NOTICES.md DESTINATION .)
endif()
