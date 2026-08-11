cmake_minimum_required(VERSION 3.25)
project(RenderOJNStep0Probe LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(SndFile CONFIG REQUIRED)
find_package(mp3lame CONFIG REQUIRED)
find_package(TagLib CONFIG REQUIRED)

add_executable(probe Probe.cpp)
target_link_libraries(probe PRIVATE SndFile::sndfile mp3lame::mp3lame TagLib::TagLib)

# Node runs the probe from the command line; no browser shell needed.  Memory
# growth mirrors what the real module will need for large sample banks.
set_target_properties(probe PROPERTIES SUFFIX ".js")
target_link_options(probe PRIVATE
    "-sALLOW_MEMORY_GROWTH=1"
    "-sEXIT_RUNTIME=1"
    "-sENVIRONMENT=node"
    # libvorbis's analysis path (_preextrapolate_helper -> vorbis_analysis_wrote)
    # blows Emscripten's 64KB default stack.  Native builds get 1-8MB from the
    # OS, which is why this never surfaces off the web.
    "-sSTACK_SIZE=5242880")
