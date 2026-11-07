#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "Usage: $0 <pairv-repo-root> <nmsis-tag>" >&2
    exit 2
fi

REPO_ROOT="$(cd -- "$1" && pwd)"
TARGET_TAG="$2"
UPSTREAM_REPO_DIR="${REPO_ROOT}/third_party/NMSIS"
UPSTREAM_NMSIS_DIR="${UPSTREAM_REPO_DIR}/NMSIS"
PREBUILT_DIR="${REPO_ROOT}/NMSIS"
VERSION_FILE="${REPO_ROOT}/NMSIS_VERSION"

ARCH="rv32imafdc"
ABI="ilp32d"
LIB_NAME="libnmsis_nn_${ARCH}.a"

GIT_ROOT="$(git -C "${REPO_ROOT}" rev-parse --show-toplevel)"
if [[ "$(cd -- "${GIT_ROOT}" && pwd)" != "${REPO_ROOT}" ]]; then
    echo "Not a PAIRV repository root: ${REPO_ROOT}" >&2
    exit 1
fi
if [[ ! -f "${REPO_ROOT}/.gitmodules" || ! -d "${UPSTREAM_NMSIS_DIR}" ]]; then
    echo "NMSIS submodule is not initialized under ${REPO_ROOT}" >&2
    exit 1
fi
if ! git check-ref-format "refs/tags/${TARGET_TAG}" >/dev/null 2>&1; then
    echo "Invalid NMSIS tag: ${TARGET_TAG}" >&2
    exit 1
fi

TAG_COMMIT="$(git -C "${UPSTREAM_REPO_DIR}" rev-parse --verify "refs/tags/${TARGET_TAG}^{commit}")"
HEAD_COMMIT="$(git -C "${UPSTREAM_REPO_DIR}" rev-parse HEAD)"
if [[ "${HEAD_COMMIT}" != "${TAG_COMMIT}" ]]; then
    echo "NMSIS HEAD is not the requested tag ${TARGET_TAG}" >&2
    exit 1
fi
CURRENT_BRANCH="$(git -C "${UPSTREAM_REPO_DIR}" branch --show-current)"
if [[ -n "${CURRENT_BRANCH}" ]]; then
    echo "NMSIS must be checked out at tag ${TARGET_TAG} in detached HEAD state" >&2
    exit 1
fi
UPSTREAM_STATUS="$(git -C "${UPSTREAM_REPO_DIR}" status --porcelain --untracked-files=all)"
if [[ -n "${UPSTREAM_STATUS}" ]]; then
    echo "NMSIS submodule has local changes" >&2
    exit 1
fi
MIRROR_STATUS="$(git -C "${REPO_ROOT}" status --porcelain --untracked-files=all --ignored -- NMSIS NMSIS_VERSION)"
if [[ -n "${MIRROR_STATUS}" ]]; then
    echo "Top-level NMSIS mirror or version record has local changes" >&2
    exit 1
fi

if [[ -n "${NUCLEI_TOOL_ROOT:-}" ]]; then
    export PATH="${NUCLEI_TOOL_ROOT}/gcc/bin:${NUCLEI_TOOL_ROOT}/qemu/bin:${NUCLEI_TOOL_ROOT}/openocd/bin:${NUCLEI_TOOL_ROOT}/nucleimodel/bin:${PATH}"
fi

for tool in make cmake riscv64-unknown-elf-gcc riscv64-unknown-elf-g++ \
            riscv64-unknown-elf-ar riscv64-unknown-elf-strip; do
    if ! command -v "${tool}" >/dev/null 2>&1; then
        echo "Required tool not found on PATH: ${tool}" >&2
        exit 1
    fi
done

export CC="riscv64-unknown-elf-gcc"
export CXX="riscv64-unknown-elf-g++"

echo "Building NMSIS NN ${TARGET_TAG} for ${ARCH}/${ABI}"
make -C "${UPSTREAM_NMSIS_DIR}" TARGET=NN RISCV_ARCH="${ARCH}" RISCV_ABI="${ABI}" clean all

SOURCE_LIB="${UPSTREAM_NMSIS_DIR}/Library/NN/GCC/${LIB_NAME}"
if [[ ! -f "${SOURCE_LIB}" ]]; then
    echo "Expected library not found: ${SOURCE_LIB}" >&2
    exit 1
fi
riscv64-unknown-elf-strip -D -g "${SOURCE_LIB}"

mkdir -p \
    "${PREBUILT_DIR}/Core/Include" \
    "${PREBUILT_DIR}/NN/Include" \
    "${PREBUILT_DIR}/Library/NN/GCC"

rm -rf "${PREBUILT_DIR}/Core/Include"/*
rm -rf "${PREBUILT_DIR}/NN/Include"/*
rm -f "${PREBUILT_DIR}/Library/NN/GCC/"*.a

cp -a "${UPSTREAM_NMSIS_DIR}/Core/Include/." "${PREBUILT_DIR}/Core/Include/"
cp -a "${UPSTREAM_NMSIS_DIR}/NN/Include/." "${PREBUILT_DIR}/NN/Include/"
cp "${UPSTREAM_NMSIS_DIR}/build.mk" "${PREBUILT_DIR}/build.mk"
cp "${SOURCE_LIB}" "${PREBUILT_DIR}/Library/NN/GCC/${LIB_NAME}"

if ! cmp -s "${SOURCE_LIB}" "${PREBUILT_DIR}/Library/NN/GCC/${LIB_NAME}"; then
    echo "Copied NMSIS NN archive does not match the upstream build" >&2
    exit 1
fi

printf 'v%s\n' "${TARGET_TAG#v}" > "${VERSION_FILE}"

echo "Refreshed NMSIS ${TARGET_TAG} mirror at ${PREBUILT_DIR}"
