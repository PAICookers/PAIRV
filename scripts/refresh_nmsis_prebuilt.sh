#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"
UPSTREAM_REPO_DIR="${REPO_ROOT}/third_party/NMSIS"
UPSTREAM_NMSIS_DIR="${UPSTREAM_REPO_DIR}/NMSIS"
PREBUILT_DIR="${REPO_ROOT}/NMSIS"

ARCH="rv32imafdc"
ABI="ilp32d"
LIB_NAME="libnmsis_nn_${ARCH}.a"

if [[ ! -d "${UPSTREAM_NMSIS_DIR}" ]]; then
    echo "Upstream NMSIS source not found at ${UPSTREAM_NMSIS_DIR}" >&2
    exit 1
fi

if [[ -n "${NUCLEI_TOOL_ROOT:-}" ]]; then
    export PATH="${NUCLEI_TOOL_ROOT}/gcc/bin:${NUCLEI_TOOL_ROOT}/qemu/bin:${NUCLEI_TOOL_ROOT}/openocd/bin:${NUCLEI_TOOL_ROOT}/nucleimodel/bin:${PATH}"
fi

if ! command -v riscv64-unknown-elf-gcc >/dev/null 2>&1; then
    echo "riscv64-unknown-elf-gcc not found on PATH. Export NUCLEI_TOOL_ROOT or update PATH first." >&2
    exit 1
fi
if ! command -v riscv64-unknown-elf-g++ >/dev/null 2>&1; then
    echo "riscv64-unknown-elf-g++ not found on PATH. Export NUCLEI_TOOL_ROOT or update PATH first." >&2
    exit 1
fi

export CC="riscv64-unknown-elf-gcc"
export CXX="riscv64-unknown-elf-g++"

echo "Building NMSIS NN upstream library for ${ARCH}/${ABI}"
make -C "${UPSTREAM_NMSIS_DIR}" TARGET=NN RISCV_ARCH="${ARCH}" RISCV_ABI="${ABI}" clean all

SOURCE_LIB="${UPSTREAM_NMSIS_DIR}/Library/NN/GCC/${LIB_NAME}"
if [[ ! -f "${SOURCE_LIB}" ]]; then
    echo "Expected library not found: ${SOURCE_LIB}" >&2
    exit 1
fi

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

UPSTREAM_REPO_URL="$(git -C "${UPSTREAM_REPO_DIR}" remote get-url origin)"
UPSTREAM_COMMIT="$(git -C "${UPSTREAM_REPO_DIR}" rev-parse HEAD)"
UPSTREAM_TAG="$(git -C "${UPSTREAM_REPO_DIR}" describe --tags --exact-match 2>/dev/null || git -C "${UPSTREAM_REPO_DIR}" describe --tags --always)"
TOOLCHAIN_VERSION="$(riscv64-unknown-elf-gcc --version | head -n 1)"

cat > "${PREBUILT_DIR}/manifest.json" <<EOF
{
  "upstream_repo": "${UPSTREAM_REPO_URL}",
  "tag": "${UPSTREAM_TAG}",
  "commit": "${UPSTREAM_COMMIT}",
  "arch": "${ARCH}",
  "abi": "${ABI}",
  "toolchain": "${TOOLCHAIN_VERSION}",
  "dsp": false
}
EOF

echo "Refreshed prebuilt NMSIS mirror at ${PREBUILT_DIR}"
