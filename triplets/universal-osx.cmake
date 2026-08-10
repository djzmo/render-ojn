# vcpkg builds the dependency graph as static universal Mach-O libraries; the
# RenderOJN preset applies the same architecture list to the application.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Darwin)
set(VCPKG_CMAKE_CONFIGURE_OPTIONS "-DCMAKE_OSX_ARCHITECTURES=x86_64;arm64")
