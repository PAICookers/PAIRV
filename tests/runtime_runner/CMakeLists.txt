cmake_minimum_required(VERSION 3.16)
project(runtime_runner_tests LANGUAGES C CXX)
set(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
set(RUNTIME "${REPO_ROOT}/Lib/runtime")
set(RVRT_TEST_STATS 1 CACHE STRING "Enable runtime transport statistics (0 or 1)")
add_library(runtime_runner_support STATIC
  artifact_fixture.cpp host_noc_mock.c
  "${REPO_ROOT}/Lib/debug.c"
  "${RUNTIME}/artifact_reader.cpp" "${RUNTIME}/frame_codec.c"
  "${RUNTIME}/paicore_runner.c" "${RUNTIME}/session.c"
  "${RUNTIME}/session_io.c")
target_compile_features(runtime_runner_support PUBLIC c_std_17 cxx_std_17)
target_include_directories(runtime_runner_support PUBLIC
  "${CMAKE_CURRENT_LIST_DIR}" "${REPO_ROOT}/tests/runtime/mocks"
  "${REPO_ROOT}/Lib" "${RUNTIME}"
  "${REPO_ROOT}/third_party/flatbuffers/include")
target_compile_definitions(runtime_runner_support PUBLIC RVRT_ENABLE_STATS=${RVRT_TEST_STATS})
target_compile_options(runtime_runner_support PUBLIC
  -fsanitize=address,undefined -fno-omit-frame-pointer
  -fno-sanitize-recover=all -g -O1 -fno-pie)
target_link_options(runtime_runner_support PUBLIC -fsanitize=address,undefined -no-pie)
enable_testing()
function(runner_test name)
  add_executable(${name} ${name}.c)
  target_link_libraries(${name} PRIVATE runtime_runner_support)
  add_test(NAME ${name} COMMAND ${name})
endfunction()
runner_test(test_lifecycle)
runner_test(test_exact)
