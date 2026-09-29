#!/usr/bin/env sh
set -eu

compiler="${1:-gcc}"
build_type="${2:-Debug}"
build_dir="/build/${compiler}-$(echo "$build_type" | tr '[:upper:]' '[:lower:]')"
extra=""

case "$compiler" in
    gcc) cxx=g++ ;;
    clang) cxx=clang++ ;;
    asan) cxx=g++; extra="-DSNAILTRAIL_SANITIZE=ON" ;;
    mingw)
        cmake -S /src -B "$build_dir" -G Ninja \
            -DCMAKE_TOOLCHAIN_FILE=/src/cmake/mingw-w64.cmake \
            -DCMAKE_BUILD_TYPE="$build_type" \
            -DCMAKE_EXE_LINKER_FLAGS=-static \
            -DSNAILTRAIL_STATIC=ON \
            -DSNAILTRAIL_WARNINGS_AS_ERRORS=ON > /dev/null
        cmake --build "$build_dir"
        if [ -d /out ]; then
            cp "$build_dir/cli/snailtrail.exe" "$build_dir/tests/snailtrail_tests.exe" /out/
            echo "copied snailtrail.exe and snailtrail_tests.exe to /out"
        fi
        exit 0
        ;;
    *) echo "usage: check.sh [gcc|clang|asan|mingw] [Debug|Release]" >&2; exit 2 ;;
esac

cmake -S /src -B "$build_dir" -G Ninja \
    -DCMAKE_CXX_COMPILER="$cxx" \
    -DCMAKE_BUILD_TYPE="$build_type" \
    -DSNAILTRAIL_WARNINGS_AS_ERRORS=ON \
    $extra > /dev/null
cmake --build "$build_dir"
ctest --test-dir "$build_dir" --output-on-failure -j "$(nproc)"
