#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"
BUILD_TYPE="Release"
RUN_APP=0
CLEAN=0

usage() {
  cat <<USAGE
Usage: scripts/build.sh [options]

Options:
  --clean            Remove build directory before configuring
  --debug            Build with Debug config (default: Release)
  --run              Run voxel_clone after successful build/tests
  --no-tests         Disable tests at configure time
  -h, --help         Show this help
USAGE
}

BUILD_TESTS=ON

while [[ $# -gt 0 ]]; do
  case "$1" in
    --clean)
      CLEAN=1
      shift
      ;;
    --debug)
      BUILD_TYPE="Debug"
      shift
      ;;
    --run)
      RUN_APP=1
      shift
      ;;
    --no-tests)
      BUILD_TESTS=OFF
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown option: $1"
      usage
      exit 1
      ;;
  esac
done

if [[ ${CLEAN} -eq 1 ]]; then
  rm -rf "${BUILD_DIR}"
fi

cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" \
  -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
  -DVOXEL_BUILD_TESTS="${BUILD_TESTS}"

cmake --build "${BUILD_DIR}" -j

if [[ "${BUILD_TESTS}" == "ON" ]]; then
  ctest --test-dir "${BUILD_DIR}" --output-on-failure
fi

if [[ ${RUN_APP} -eq 1 ]]; then
  "${BUILD_DIR}/voxel_clone"
fi
