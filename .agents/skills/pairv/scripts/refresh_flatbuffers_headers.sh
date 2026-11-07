#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "Usage: $0 <pairv-repo-root> <flatbuffers-tag>" >&2
    exit 2
fi

REPO_ROOT="$(cd -- "$1" && pwd)"
TAG="$2"
UPSTREAM_URL="${FLATBUFFERS_UPSTREAM_URL:-https://github.com/google/flatbuffers.git}"
VENDOR_ROOT="${REPO_ROOT}/third_party/flatbuffers"
DEST_DIR="${VENDOR_ROOT}/include/flatbuffers"
VERSION_FILE="${VENDOR_ROOT}/README.md"

if [[ ! "${TAG}" =~ ^v[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "FlatBuffers version must be an explicit release tag such as v25.12.19" >&2
    exit 1
fi

GIT_ROOT="$(git -C "${REPO_ROOT}" rev-parse --show-toplevel)"
if [[ "$(cd -- "${GIT_ROOT}" && pwd)" != "${REPO_ROOT}" ]]; then
    echo "Not a PAIRV repository root: ${REPO_ROOT}" >&2
    exit 1
fi
if [[ ! -d "${DEST_DIR}" || ! -f "${VERSION_FILE}" ]]; then
    echo "PAIRV FlatBuffers vendor tree is incomplete under ${VENDOR_ROOT}" >&2
    exit 1
fi
VENDOR_STATUS="$(git -C "${REPO_ROOT}" status --porcelain --untracked-files=all --ignored -- third_party/flatbuffers)"
if [[ -n "${VENDOR_STATUS}" ]]; then
    echo "third_party/flatbuffers has local changes" >&2
    exit 1
fi

TMP_DIR="$(mktemp -d)"
STAGE_DIR=""
BACKUP_DIR=""
BACKUP_ROOT=""
SUCCESS=0

cleanup() {
    exit_code=$?
    if [[ ${SUCCESS} -ne 1 && -n "${BACKUP_DIR}" && -d "${BACKUP_DIR}" ]]; then
        rm -rf "${DEST_DIR}"
        mv "${BACKUP_DIR}" "${DEST_DIR}"
        cp "${TMP_DIR}/README.md.original" "${VERSION_FILE}"
    fi
    [[ -z "${STAGE_DIR}" ]] || rm -rf "${STAGE_DIR}"
    [[ -z "${BACKUP_ROOT}" ]] || rmdir "${BACKUP_ROOT}"
    rm -rf "${TMP_DIR}"
    exit "${exit_code}"
}
trap cleanup EXIT

git -C "${TMP_DIR}" init --quiet
git -C "${TMP_DIR}" remote add origin "${UPSTREAM_URL}"
git -C "${TMP_DIR}" fetch --quiet --depth=1 origin \
    "refs/tags/${TAG}:refs/tags/${TAG}"
COMMIT="$(git -C "${TMP_DIR}" rev-parse --verify "refs/tags/${TAG}^{commit}")"

mkdir -p "${TMP_DIR}/export"
git -C "${TMP_DIR}" archive "${COMMIT}" include/flatbuffers |
    tar -x -C "${TMP_DIR}/export"

if [[ ! -f "${TMP_DIR}/export/include/flatbuffers/base.h" ||
      ! -f "${TMP_DIR}/export/include/flatbuffers/flatbuffers.h" ]]; then
    echo "Official tag ${TAG} does not contain the expected FlatBuffers headers" >&2
    exit 1
fi
if find "${TMP_DIR}/export/include/flatbuffers" -type l | grep -q .; then
    echo "Refusing a FlatBuffers header tree containing symbolic links" >&2
    exit 1
fi

git -C "${TMP_DIR}" ls-tree -r --name-only "${COMMIT}" -- include/flatbuffers |
    LC_ALL=C sort > "${TMP_DIR}/upstream.manifest"
(cd "${TMP_DIR}/export/include/flatbuffers" &&
    find . -type f -print | sed 's|^\./|include/flatbuffers/|' | LC_ALL=C sort) \
    > "${TMP_DIR}/export.manifest"
if [[ ! -s "${TMP_DIR}/upstream.manifest" ]] ||
   ! cmp -s "${TMP_DIR}/upstream.manifest" "${TMP_DIR}/export.manifest"; then
    echo "Exported FlatBuffers header manifest is incomplete" >&2
    exit 1
fi

BASE_H="${TMP_DIR}/export/include/flatbuffers/base.h"
MAJOR="$(awk '$2 == "FLATBUFFERS_VERSION_MAJOR" { print $3; exit }' "${BASE_H}")"
MINOR="$(awk '$2 == "FLATBUFFERS_VERSION_MINOR" { print $3; exit }' "${BASE_H}")"
REVISION="$(awk '$2 == "FLATBUFFERS_VERSION_REVISION" { print $3; exit }' "${BASE_H}")"
VERSION="${MAJOR}.${MINOR}.${REVISION}"
if [[ "v${VERSION}" != "${TAG}" ]]; then
    echo "Tag ${TAG} does not match base.h version ${VERSION}" >&2
    exit 1
fi
if [[ "$(grep -c '^- Version:' "${VERSION_FILE}")" -ne 1 ]]; then
    echo "Expected exactly one FlatBuffers version record in ${VERSION_FILE}" >&2
    exit 1
fi

cp "${VERSION_FILE}" "${TMP_DIR}/README.md.original"
sed "s/^- Version: .*/- Version: ${VERSION}/" "${VERSION_FILE}" \
    > "${TMP_DIR}/README.md.updated"

STAGE_DIR="$(mktemp -d "${VENDOR_ROOT}/include/.flatbuffers-new.XXXXXX")"
cp -a "${TMP_DIR}/export/include/flatbuffers/." "${STAGE_DIR}/"
BACKUP_ROOT="$(mktemp -d "${VENDOR_ROOT}/include/.flatbuffers-old.XXXXXX")"
mv "${DEST_DIR}" "${BACKUP_ROOT}/original"
BACKUP_DIR="${BACKUP_ROOT}/original"
mv "${STAGE_DIR}" "${DEST_DIR}"
STAGE_DIR=""
cp "${TMP_DIR}/README.md.updated" "${VERSION_FILE}"

(cd "${DEST_DIR}" &&
    find . -type f -print | sed 's|^\./|include/flatbuffers/|' | LC_ALL=C sort) \
    > "${TMP_DIR}/installed.manifest"
if ! cmp -s "${TMP_DIR}/upstream.manifest" "${TMP_DIR}/installed.manifest"; then
    echo "Installed FlatBuffers header manifest does not match tag ${TAG}" >&2
    exit 1
fi
if ! cmp -s "${BASE_H}" "${DEST_DIR}/base.h"; then
    echo "Installed FlatBuffers base.h does not match tag ${TAG}" >&2
    exit 1
fi
if ! grep -qx -- "- Version: ${VERSION}" "${VERSION_FILE}"; then
    echo "FlatBuffers README version record was not updated" >&2
    exit 1
fi

rm -rf "${BACKUP_DIR}"
BACKUP_DIR=""
SUCCESS=1
echo "Updated FlatBuffers headers to ${TAG} (${COMMIT})"
