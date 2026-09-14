cmake_minimum_required(VERSION 3.16)
project(snn_head_resident_host_tests LANGUAGES C CXX)
enable_testing()
get_filename_component(SNN_HEAD_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
get_filename_component(REPO_ROOT "${SNN_HEAD_DIR}/../../.." ABSOLUTE)
set(SNN_HEAD_ASSET_DIR "${SNN_HEAD_DIR}/artifacts/three_chip" CACHE PATH "Assets under test")
add_executable(snn_head_resident_host_test
  run_resident_host.c noc_mock.c artifact_shim.c metadata.cpp
  "${SNN_HEAD_DIR}/host_test/host_artifact_support.cpp"
  "${SNN_HEAD_DIR}/src/snn_head.c"
  "${SNN_HEAD_DIR}/src/snn_head_resident.c"
  "${SNN_HEAD_DIR}/src/snn_head_profile.c"
  "${SNN_HEAD_DIR}/src/snn_head_fc1_lif.c"
  "${SNN_HEAD_DIR}/src/snn_head_block_lif.c"
  "${SNN_HEAD_DIR}/src/snn_head_fc2.c"
  "${SNN_HEAD_DIR}/src/snn_head_fc3.c"
  "${SNN_HEAD_DIR}/src/snn_head_params.c"
  "${REPO_ROOT}/Lib/debug.c"
  "${REPO_ROOT}/Lib/nn_layernorm.c"
  "${REPO_ROOT}/Lib/nn_quant.c"
  "${REPO_ROOT}/Lib/runtime/artifact_reader.cpp"
  "${REPO_ROOT}/Lib/runtime/frame_codec.c"
  "${REPO_ROOT}/Lib/runtime/paicore_runner.c"
  "${REPO_ROOT}/Lib/runtime/session.c"
  "${REPO_ROOT}/Lib/runtime/session_io.c")
target_include_directories(snn_head_resident_host_test PRIVATE
  "${SNN_HEAD_DIR}/include" "${SNN_HEAD_DIR}/src"
  "${REPO_ROOT}/tests/runtime/mocks" "${REPO_ROOT}/Lib"
  "${REPO_ROOT}/Lib/runtime" "${REPO_ROOT}/third_party/flatbuffers/include")
target_compile_features(snn_head_resident_host_test PRIVATE c_std_17 cxx_std_17)
target_compile_definitions(snn_head_resident_host_test PRIVATE
  SNN_HEAD_RESIDENT=1 SNN_HEAD_TIMING=1 RVRT_ENABLE_STATS=1
  SNN_HEAD_ASSET_DIR="${SNN_HEAD_ASSET_DIR}")
target_compile_options(snn_head_resident_host_test PRIVATE
  -O1 -g -ffp-contract=off -fno-omit-frame-pointer -fsanitize=address,undefined)
target_link_options(snn_head_resident_host_test PRIVATE -fsanitize=address,undefined -no-pie)
target_link_libraries(snn_head_resident_host_test PRIVATE m)
foreach(layer fc1_lif block0_lif block1_lif fc2 fc3)
  file(SIZE "${SNN_HEAD_ASSET_DIR}/${layer}/compile_artifacts.bin" asset_size)
  target_compile_definitions(snn_head_resident_host_test PRIVATE "SIZE_${layer}=${asset_size}")
  target_link_options(snn_head_resident_host_test PRIVATE
    "-Wl,--defsym,snn_head_${layer}_artifact_size=${asset_size}")
endforeach()
add_test(NAME resident_repeated_chunks COMMAND snn_head_resident_host_test)
add_test(NAME resident_failure_stops_chain COMMAND snn_head_resident_host_test fail)
