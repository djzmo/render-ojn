# vcpkg builds the dependency graph as static universal Mach-O libraries; the
# RenderOJN preset applies the same architecture list to the application.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Darwin)

# VCPKG_OSX_ARCHITECTURES is the only place the architecture list belongs. It
# reaches every port regardless of build system: vcpkg_cmake_configure forwards
# it as -DCMAKE_OSX_ARCHITECTURES, and vcpkg-make reads it directly for the
# autotools ports -- mp3lame among them.
#
# Do not also set it through VCPKG_CMAKE_CONFIGURE_OPTIONS. A semicolon makes
# that a two-element list, so
#     "-DCMAKE_OSX_ARCHITECTURES=x86_64;arm64"
# expands to the two arguments `-DCMAKE_OSX_ARCHITECTURES=x86_64` and `arm64`.
# vcpkg appends those *after* its own forwarded value, so the truncated one won,
# every dependency was built x86_64-only, and the universal link failed with
# each LAME, libsndfile and Catch2 symbol "not found for architecture arm64".
set(VCPKG_OSX_ARCHITECTURES "x86_64;arm64")
