#!/usr/bin/env bash
set -euo pipefail

if [[ ! -f /src/CMakePresets.json ]]; then
    echo "Expected the repository to be mounted at /src." >&2
    exit 2
fi

cd /src
if [[ ! -x /opt/vcpkg/vcpkg ]]; then
    git clone --depth 1 https://github.com/microsoft/vcpkg.git /opt/vcpkg
    git -C /opt/vcpkg fetch --depth 1 origin ea1a7396b05637a53bf23c078647ecc0edee4b80
    git -C /opt/vcpkg checkout --detach ea1a7396b05637a53bf23c078647ecc0edee4b80
    /opt/vcpkg/bootstrap-vcpkg.sh -disableMetrics
fi

export VCPKG_ROOT=/opt/vcpkg
cmake --preset linux-x64-manylinux2014
cmake --build --preset linux-x64-manylinux2014-debug --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-2}"
cmake --build --preset linux-x64-manylinux2014-release --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-2}"
ctest --preset linux-x64-manylinux2014-test
cmake --build out/build/linux-x64-manylinux2014 --config Release --target renderojn_package

cmake --preset linux-x64-asan
cmake --build --preset linux-x64-asan-debug --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-2}"
ctest --preset linux-x64-asan-test
