vcpkg_from_sourceforge(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO lame/lame
    REF ${VERSION}
    FILENAME "lame-${VERSION}.tar.gz"
    SHA512 0844b9eadb4aacf8000444621451277de365041cc1d97b7f7a589da0b7a23899310afd4e4d81114b9912aa97832621d20588034715573d417b2923948c08634b
    PATCHES
        00001-msvc-upgrade-solution-up-to-vc11.patch
        remove_lame_init_old_from_symbol_list.patch # deprecated https://github.com/zlargon/lame/blob/master/include/lame.h#L169
        add-macos-universal-config.patch
        fix-mingw-w64-compatibility.patch
        fix-universal-sse-guard.patch # RenderOJN: see the patch header
)

if(VCPKG_TARGET_IS_WINDOWS AND NOT VCPKG_TARGET_IS_MINGW)

    if(VCPKG_TARGET_ARCHITECTURE STREQUAL "arm64")
        set(platform "ARM64")
        set(machine "ARM64")
    elseif(VCPKG_TARGET_ARCHITECTURE STREQUAL "arm")
        set(platform "ARM")
        set(machine "ARM")
    elseif(VCPKG_TARGET_ARCHITECTURE STREQUAL "x64")
        set(platform "x64")
        set(machine "x64")
    else()
        set(platform "Win32")
        set(machine "x86")
    endif()

    file(READ "${SOURCE_PATH}/vc_solution/vc11_lame.sln" sln_con)
    string(REPLACE "|Win32" "|${platform}" sln_con "${sln_con}")
    string(REPLACE "\"vc11_" "\"${machine}_vc11_" sln_con "${sln_con}")
    file(WRITE "${SOURCE_PATH}/vc_solution/${machine}_vc11_lame.sln" "${sln_con}")

    
    file(GLOB vcxprojs RELATIVE "${SOURCE_PATH}/vc_solution" "${SOURCE_PATH}/vc_solution/vc11_*.vcxproj")
    foreach(vcxproj ${vcxprojs})
        file(READ "${SOURCE_PATH}/vc_solution/${vcxproj}" vcxproj_con)
        
        if(NOT VCPKG_CRT_LINKAGE STREQUAL "dynamic")
            string(REPLACE "DLL</RuntimeLibrary>" "</RuntimeLibrary>" vcxproj_con "${vcxproj_con}")
        endif()

        string(REPLACE "/machine:x86" "/machine:${machine}" vcxproj_con "${vcxproj_con}")
        string(REPLACE "<Platform>Win32</Platform>" "<Platform>${platform}</Platform>" vcxproj_con "${vcxproj_con}")
        string(REPLACE "|Win32" "|${platform}" vcxproj_con "${vcxproj_con}")
        string(REPLACE "Include=\"vc11_" "Include=\"${machine}_vc11_" vcxproj_con "${vcxproj_con}")
 
        if(NOT VCPKG_TARGET_IS_UWP)
            string(REPLACE "/APPCONTAINER" "" vcxproj_con "${vcxproj_con}")
        endif()
        
        file(WRITE "${SOURCE_PATH}/vc_solution/${machine}_${vcxproj}" "${vcxproj_con}")
    endforeach()

    if(VCPKG_LIBRARY_LINKAGE STREQUAL "static")
        vcpkg_msbuild_install(
            SOURCE_PATH "${SOURCE_PATH}"
            PROJECT_SUBPATH "vc_solution/${machine}_vc11_lame.sln"
            TARGET "libmp3lame-static"
            PLATFORM "${platform}"
        )
    else()
        vcpkg_msbuild_install(
            SOURCE_PATH "${SOURCE_PATH}"
            PROJECT_SUBPATH "vc_solution/${machine}_vc11_lame.sln"
            TARGET "libmp3lame"
            PLATFORM "${platform}"
        )
    endif()
    if("frontend" IN_LIST FEATURES)
        vcpkg_msbuild_install(
            SOURCE_PATH "${SOURCE_PATH}"
            PROJECT_SUBPATH "vc_solution/${machine}_vc11_lame.sln"
            TARGET "lame"
            PLATFORM "${platform}"
        )
    endif()

    file(COPY "${SOURCE_PATH}/include/lame.h" DESTINATION "${CURRENT_PACKAGES_DIR}/include/lame")

    if(VCPKG_LIBRARY_LINKAGE STREQUAL "static")
        file(REMOVE_RECURSE
            "${CURRENT_PACKAGES_DIR}/bin"
            "${CURRENT_PACKAGES_DIR}/lib/libmp3lame.lib"
            "${CURRENT_PACKAGES_DIR}/debug/bin"
            "${CURRENT_PACKAGES_DIR}/debug/lib/libmp3lame.lib"
        )
    else()
        file(REMOVE
            "${CURRENT_PACKAGES_DIR}/lib/libmp3lame-static.lib"
            "${CURRENT_PACKAGES_DIR}/lib/libmpghip-static.lib"
            "${CURRENT_PACKAGES_DIR}/debug/lib/libmp3lame-static.lib"
            "${CURRENT_PACKAGES_DIR}/debug/lib/libmpghip-static.lib"
        )
    endif()

else()

    vcpkg_list(SET OPTIONS)
    if("frontend" IN_LIST FEATURES)
        list(APPEND OPTIONS --enable-frontend)
    else()
        list(APPEND OPTIONS --disable-frontend)
    endif()

    if(NOT VCPKG_TARGET_IS_MINGW)
        list(APPEND OPTIONS --with-pic=yes)
    endif()

    # LAME 3.100 vendors config.sub/config.guess from 2015, which predates
    # emscripten's entry in the autotools system list.  Configure therefore dies
    # with "Invalid configuration `wasm32-unknown-emscripten': system
    # `emscripten' not recognized" before it compiles a single line -- the C
    # sources are fine, only the platform-detection scripts are stale.  Refresh
    # them from the autoconf that vcpkg's own msys2 already ships.
    if(VCPKG_TARGET_IS_EMSCRIPTEN)
        vcpkg_acquire_msys(MSYS_ROOT PACKAGES autoconf)
        file(GLOB _renderojn_config_sub "${MSYS_ROOT}/usr/share/autoconf-*/build-aux/config.sub")
        file(GLOB _renderojn_config_guess "${MSYS_ROOT}/usr/share/autoconf-*/build-aux/config.guess")
        if(NOT _renderojn_config_sub OR NOT _renderojn_config_guess)
            message(FATAL_ERROR "Unable to locate an autoconf config.sub/config.guess to refresh LAME's 2015 copies")
        endif()
        list(GET _renderojn_config_sub 0 _renderojn_config_sub)
        list(GET _renderojn_config_guess 0 _renderojn_config_guess)
        configure_file("${_renderojn_config_sub}" "${SOURCE_PATH}/config.sub" COPYONLY)
        configure_file("${_renderojn_config_guess}" "${SOURCE_PATH}/config.guess" COPYONLY)
    endif()

    # A universal macOS slice hands the compiler both architectures at once, and
    # clang refuses to preprocess for more than one:
    #
    #     clang: error: cannot use 'cpp-output' output with multiple -arch options
    #
    # Autoconf's default preprocessor is "$CC -E", so its check fails, it falls
    # back to /lib/cpp -- which macOS has not shipped for years -- and configure
    # dies with `C preprocessor "/lib/cpp" fails sanity check` before compiling
    # anything. vcpkg leaves CPP unset on non-Windows targets, so pin it here to
    # a single architecture. Only the compile and link steps have to be
    # universal; the headers configure inspects are the same for both slices.
    list(LENGTH VCPKG_OSX_ARCHITECTURES renderojn_osx_arch_count)
    if(VCPKG_TARGET_IS_OSX AND renderojn_osx_arch_count GREATER 1)
        vcpkg_cmake_get_vars(renderojn_cmake_vars_file)
        include("${renderojn_cmake_vars_file}")
        list(GET VCPKG_OSX_ARCHITECTURES 0 renderojn_cpp_arch)
        list(APPEND OPTIONS "CPP=${VCPKG_DETECTED_CMAKE_C_COMPILER} -arch ${renderojn_cpp_arch} -E")
    endif()

    vcpkg_make_configure(
        SOURCE_PATH "${SOURCE_PATH}"
        OPTIONS
            ${OPTIONS}
    )
    vcpkg_make_install()

    file(REMOVE_RECURSE
        "${CURRENT_PACKAGES_DIR}/debug/include"
        "${CURRENT_PACKAGES_DIR}/debug/share"
        "${CURRENT_PACKAGES_DIR}/share/${PORT}/doc"
        "${CURRENT_PACKAGES_DIR}/share/${PORT}/man1"
    )

endif()

# unofficial, but port legacy
file(COPY "${CMAKE_CURRENT_LIST_DIR}/mp3lame-config.cmake" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/COPYING")
