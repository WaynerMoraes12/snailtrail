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
    *) echo "usage: check.sh [gcc|clang|asan] [Debug|Release]" >&2; exit 2 ;;
esac

cmake -S /src -B "$build_dir" -G Ninja \
    -DCMAKE_CXX_COMPILER="$cxx" \
    -DCMAKE_BUILD_TYPE="$build_type" \
    -DSNAILTRAIL_WARNINGS_AS_ERRORS=ON \
    $extra > /dev/null
cmake --build "$build_dir"
ctest --test-dir "$build_dir" --output-on-failure -j "$(nproc)"
