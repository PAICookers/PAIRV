cmake_minimum_required(VERSION 3.16)

project(pairv_runtime_tests LANGUAGES C CXX)

set(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")

add_executable(runtime_codec_test
  test_runtime_codec.c
  "${REPO_ROOT}/application/runtime/mnist/data.c"
  "${REPO_ROOT}/Lib/debug.c"
  "${REPO_ROOT}/Lib/runtime/artifact_reader.cpp"
  "${REPO_ROOT}/Lib/runtime/frame_codec.c"
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

set(RUNTIME_SOURCES
  "${REPO_ROOT}/Lib/debug.c"
  "${REPO_ROOT}/Lib/runtime/artifact_reader.cpp"
  "${REPO_ROOT}/Lib/runtime/frame_codec.c"
  "${REPO_ROOT}/Lib/runtime/runtime_session.c"
  "${REPO_ROOT}/Lib/runtime/transport.c"
  "${REPO_ROOT}/Lib/runtime/transport_io.c"
  "${REPO_ROOT}/Lib/runtime/thread_runner.c"
)

foreach(target runtime_control_test runtime_control_no_stats_test)
  add_executable(${target} test_runtime_control.c mock_runtime_hw.c ${RUNTIME_SOURCES})
endforeach()

foreach(target runtime_control_test runtime_control_no_stats_test)
  target_compile_features(${target} PRIVATE c_std_17 cxx_std_17)
  target_include_directories(${target} PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/mocks"
    "${CMAKE_CURRENT_LIST_DIR}"
    "${REPO_ROOT}/Lib"
    "${REPO_ROOT}/Lib/runtime"
    "${REPO_ROOT}/third_party/flatbuffers/include"
  )
  target_compile_definitions(${target} PRIVATE
    RVRT_TEST_ASSET_DIR="${CMAKE_CURRENT_LIST_DIR}/assets"
  )
endforeach()

# Keep the default regression target stats-enabled, while allowing the
# documented CMAKE_C_FLAGS/CMAKE_CXX_FLAGS variants to select 0 or 1 without
# duplicate compiler definitions.
if(NOT CMAKE_C_FLAGS MATCHES "-DRVRT_ENABLE_STATS=" AND
   NOT CMAKE_CXX_FLAGS MATCHES "-DRVRT_ENABLE_STATS=")
  target_compile_definitions(runtime_control_test PRIVATE RVRT_ENABLE_STATS=1)
endif()

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
target_compile_definitions(snn_head_artifact_minimal_test PRIVATE
  SNN_HEAD_ASSET_DIR="${REPO_ROOT}/application/baremetal/snn_head/assets"
)

add_executable(runtime_thread_runner_test
  test_runtime_thread_runner.cpp
  mock_runtime_hw.c
  ${RUNTIME_SOURCES}
)

target_compile_features(runtime_thread_runner_test PRIVATE c_std_17 cxx_std_17)
target_include_directories(runtime_thread_runner_test PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}/mocks"
  "${CMAKE_CURRENT_LIST_DIR}"
  "${REPO_ROOT}/Lib"
  "${REPO_ROOT}/Lib/runtime"
  "${REPO_ROOT}/Lib/runtime/generated"
  "${REPO_ROOT}/third_party/flatbuffers/include"
)

enable_testing()
add_test(NAME runtime_codec_test COMMAND runtime_codec_test)
add_test(NAME runtime_control_test COMMAND runtime_control_test)
add_test(NAME runtime_control_no_stats_test COMMAND runtime_control_no_stats_test)
add_test(NAME nn_quant_inplace_test COMMAND nn_quant_inplace_test)
add_test(NAME runtime_thread_runner_test COMMAND runtime_thread_runner_test)
add_test(NAME snn_head_artifact_minimal_test COMMAND snn_head_artifact_minimal_test)

add_custom_target(run
  COMMAND runtime_codec_test
  COMMAND runtime_control_test
  COMMAND runtime_control_no_stats_test
  COMMAND nn_quant_inplace_test
  COMMAND runtime_thread_runner_test
  COMMAND snn_head_artifact_minimal_test
  DEPENDS runtime_codec_test runtime_control_test runtime_control_no_stats_test nn_quant_inplace_test runtime_thread_runner_test snn_head_artifact_minimal_test
)
