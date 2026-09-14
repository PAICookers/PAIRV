cmake_minimum_required(VERSION 3.16)
project(runtime_output_layout_tests LANGUAGES C CXX)

set(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
enable_testing()
foreach(STATS IN ITEMS 0 1)
  set(TARGET runtime_output_layout_stats${STATS})
  add_executable(${TARGET}
    test_output_layout.cpp
    noc_mock.c
    "${REPO_ROOT}/Lib/debug.c"
    "${REPO_ROOT}/Lib/runtime/artifact_reader.cpp"
    "${REPO_ROOT}/Lib/runtime/frame_codec.c"
    "${REPO_ROOT}/Lib/runtime/paicore_runner.c"
    "${REPO_ROOT}/Lib/runtime/session.c"
    "${REPO_ROOT}/Lib/runtime/session_io.c")
  target_compile_features(${TARGET} PRIVATE c_std_17 cxx_std_17)
  target_include_directories(${TARGET} PRIVATE
    "${REPO_ROOT}/tests/runtime/mocks"
    "${REPO_ROOT}/Lib"
    "${REPO_ROOT}/Lib/runtime"
    "${REPO_ROOT}/third_party/flatbuffers/include")
  target_compile_definitions(${TARGET} PRIVATE RVRT_ENABLE_STATS=${STATS})
  target_compile_options(${TARGET} PRIVATE
    -fsanitize=address,undefined -fno-sanitize-recover=all
    -fno-omit-frame-pointer -fno-pie -g -O1)
  target_link_options(${TARGET} PRIVATE -fsanitize=address,undefined -no-pie)
  add_test(NAME ${TARGET} COMMAND ${TARGET})
endforeach()
