# vcpkg builds the dependency graph as static universal Mach-O libraries; the
# RenderOJN preset applies the same architecture list to the application.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Darwin)

# VCPKG_OSX_ARCHITECTURES is the variable vcpkg's own toolchain reads, and it
# reaches every port regardless of build system. Passing the architecture list
# only through VCPKG_CMAKE_CONFIGURE_OPTIONS reached the CMake-based ports and
# silently missed the autotools ones -- mp3lame among them -- so the link
# failed with every LAME and libsndfile symbol "not found for architecture
# arm64" once the runner image moved to Apple Silicon.
set(VCPKG_OSX_ARCHITECTURES "x86_64;arm64")
set(VCPKG_CMAKE_CONFIGURE_OPTIONS "-DCMAKE_OSX_ARCHITECTURES=x86_64;arm64")
