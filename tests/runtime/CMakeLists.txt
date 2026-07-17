cmake_minimum_required(VERSION 3.16)

project(pairv_runtime_tests LANGUAGES C CXX)

set(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")

add_executable(runtime_codec_test
  test_runtime_codec.c
  "${REPO_ROOT}/Lib/debug.c"
  "${REPO_ROOT}/Lib/runtime/artifact_reader.cpp"
  "${REPO_ROOT}/Lib/runtime/frame_codec.c"
  "${REPO_ROOT}/Lib/runtime/managed_packet.c"
)

target_compile_features(runtime_codec_test PRIVATE c_std_17 cxx_std_17)

target_include_directories(runtime_codec_test PRIVATE
  "${REPO_ROOT}/Lib"
  "${REPO_ROOT}/Lib/runtime"
  "${REPO_ROOT}/third_party/flatbuffers/include"
)

target_compile_definitions(runtime_codec_test PRIVATE
  RVRT_TEST_FIXTURE_DIR="${CMAKE_CURRENT_LIST_DIR}/fixtures"
)

add_executable(runtime_control_test
  test_runtime_control.c
  mock_runtime_hw.c
  "${REPO_ROOT}/Lib/debug.c"
  "${REPO_ROOT}/Lib/runtime/artifact_executor.c"
  "${REPO_ROOT}/Lib/runtime/artifact_reader.cpp"
  "${REPO_ROOT}/Lib/runtime/frame_codec.c"
  "${REPO_ROOT}/Lib/runtime/session.c"
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
  RVRT_TEST_FIXTURE_DIR="${CMAKE_CURRENT_LIST_DIR}/fixtures"
)

enable_testing()
add_test(NAME runtime_codec_test COMMAND runtime_codec_test)
add_test(NAME runtime_control_test COMMAND runtime_control_test)

add_custom_target(run
  COMMAND runtime_codec_test
  COMMAND runtime_control_test
  DEPENDS runtime_codec_test runtime_control_test
)
