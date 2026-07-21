cmake_minimum_required(VERSION 3.16)

project(snn_head_layer_tests LANGUAGES C CXX)

# tests/ -> snn_head/ -> baremetal/ -> application/ -> repo root
set(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../..")

set(SNN_HEAD_RUNTIME_SRCS
  "${REPO_ROOT}/Lib/debug.c"
  "${REPO_ROOT}/Lib/runtime/artifact_reader.cpp"
  "${REPO_ROOT}/Lib/runtime/frame_codec.c"
  "${REPO_ROOT}/Lib/runtime/managed_packet.c"
)

set(SNN_HEAD_TEST_INCLUDE_DIRS
  "${CMAKE_CURRENT_LIST_DIR}"
  "${REPO_ROOT}/Lib"
  "${REPO_ROOT}/Lib/runtime"
  "${REPO_ROOT}/third_party/flatbuffers/include"
)

set(SNN_HEAD_FIXTURE_DIR "${REPO_ROOT}/tests/runtime/fixtures/artifacts2")

# One executable per layer; each is a tiny main() over the shared helpers.
set(SNN_HEAD_LAYER_TESTS
  fc1_lif
  block0_lif
  block1_lif
  fc2
  fc3
)

enable_testing()

set(SNN_HEAD_TEST_TARGETS "")
foreach(layer ${SNN_HEAD_LAYER_TESTS})
  set(target "test_${layer}")
  add_executable(${target} "${target}.c" ${SNN_HEAD_RUNTIME_SRCS})
  target_compile_features(${target} PRIVATE c_std_17 cxx_std_17)
  target_include_directories(${target} PRIVATE ${SNN_HEAD_TEST_INCLUDE_DIRS})
  target_compile_definitions(${target} PRIVATE
    SNN_HEAD_FIXTURE_DIR="${SNN_HEAD_FIXTURE_DIR}")
  add_test(NAME ${target} COMMAND ${target})
  list(APPEND SNN_HEAD_TEST_TARGETS ${target})
endforeach()

add_custom_target(run
  COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
  DEPENDS ${SNN_HEAD_TEST_TARGETS}
)
