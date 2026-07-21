cmake_minimum_required(VERSION 3.16)

project(pairv_runtime_tests LANGUAGES C CXX)

set(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")

add_executable(runtime_codec_test
  test_runtime_codec.c
  "${REPO_ROOT}/application/runtime/mnist/data.c"
  "${REPO_ROOT}/Lib/debug.c"
  "${REPO_ROOT}/Lib/runtime/artifact_reader.cpp"
  "${REPO_ROOT}/Lib/runtime/frame_codec.c"
  "${REPO_ROOT}/Lib/runtime/managed_packet.c"
)

target_compile_features(runtime_codec_test PRIVATE c_std_17 cxx_std_17)

target_include_directories(runtime_codec_test PRIVATE
  "${REPO_ROOT}/Lib"
  "${REPO_ROOT}/Lib/runtime"
  "${REPO_ROOT}/application/runtime/mnist"
  "${REPO_ROOT}/third_party/flatbuffers/include"
)

target_compile_definitions(runtime_codec_test PRIVATE
  RVRT_TEST_ASSET_DIR="${CMAKE_CURRENT_LIST_DIR}/assets"
  RVRT_MNIST_ASSET_DIR="${REPO_ROOT}/application/runtime/mnist/assets"
)

add_executable(runtime_control_test
  test_runtime_control.c
  mock_runtime_hw.c
  "${REPO_ROOT}/Lib/debug.c"
  "${REPO_ROOT}/Lib/runtime/artifact_executor.c"
  "${REPO_ROOT}/Lib/runtime/artifact_reader.cpp"
  "${REPO_ROOT}/Lib/runtime/frame_codec.c"
  "${REPO_ROOT}/Lib/runtime/session.c"
  "${REPO_ROOT}/Lib/runtime/session_io.c"
)

target_compile_features(runtime_control_test PRIVATE c_std_17 cxx_std_17)

target_include_directories(runtime_control_test PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}/mocks"
  "${CMAKE_CURRENT_LIST_DIR}"
  "${REPO_ROOT}/Lib"
  "${REPO_ROOT}/Lib/runtime"
  "${REPO_ROOT}/third_party/flatbuffers/include"
)

target_compile_definitions(runtime_control_test PRIVATE
  RVRT_TEST_ASSET_DIR="${CMAKE_CURRENT_LIST_DIR}/assets"
)

add_executable(nn_quant_inplace_test
  test_nn_quant_inplace.c
  "${REPO_ROOT}/Lib/nn_quant.c"
)

target_compile_features(nn_quant_inplace_test PRIVATE c_std_17)

target_include_directories(nn_quant_inplace_test PRIVATE
  "${REPO_ROOT}/Lib"
)

target_link_libraries(nn_quant_inplace_test PRIVATE m)

add_executable(snn_head_artifact_minimal_test
  test_snn_head_artifact_minimal.c
  "${REPO_ROOT}/Lib/debug.c"
  "${REPO_ROOT}/Lib/runtime/artifact_reader.cpp"
)

target_compile_features(snn_head_artifact_minimal_test PRIVATE c_std_17 cxx_std_17)

target_include_directories(snn_head_artifact_minimal_test PRIVATE
  "${REPO_ROOT}/Lib"
  "${REPO_ROOT}/Lib/runtime"
  "${REPO_ROOT}/third_party/flatbuffers/include"
)

enable_testing()
add_test(NAME runtime_codec_test COMMAND runtime_codec_test)
add_test(NAME runtime_control_test COMMAND runtime_control_test)
add_test(NAME nn_quant_inplace_test COMMAND nn_quant_inplace_test)
add_test(NAME snn_head_artifact_minimal_test COMMAND snn_head_artifact_minimal_test)

add_custom_target(run
  COMMAND runtime_codec_test
  COMMAND runtime_control_test
  COMMAND nn_quant_inplace_test
  COMMAND snn_head_artifact_minimal_test
  DEPENDS runtime_codec_test runtime_control_test nn_quant_inplace_test snn_head_artifact_minimal_test
)
