cmake_minimum_required(VERSION 3.16)

project(snn_head_host_layer_test LANGUAGES C CXX)

# host_test/ -> snn_head/ -> baremetal/ -> application/ -> repo root
set(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../..")
set(SNN_HEAD_DIR "${CMAKE_CURRENT_LIST_DIR}/..")

add_executable(snn_head_host_layer_test
  run_layers_host.c
  host_noc_mock.c
  host_artifact_shim.c
  # 被测：五层实现 + 核心（共享 buffer/helper）+ 参数
  "${SNN_HEAD_DIR}/snn_head.c"
  "${SNN_HEAD_DIR}/snn_head_fc1_lif.c"
  "${SNN_HEAD_DIR}/snn_head_block_lif.c"
  "${SNN_HEAD_DIR}/snn_head_fc2.c"
  "${SNN_HEAD_DIR}/snn_head_fc3.c"
  "${SNN_HEAD_DIR}/snn_head_params.c"
  "${SNN_HEAD_DIR}/board_test/snn_head_golden.c"
  # CPU 算子
  "${REPO_ROOT}/Lib/nn_layernorm.c"
  "${REPO_ROOT}/Lib/nn_quant.c"
  "${REPO_ROOT}/Lib/debug.c"
  # runtime 栈
  "${REPO_ROOT}/Lib/runtime/artifact_reader.cpp"
  "${REPO_ROOT}/Lib/runtime/frame_codec.c"
  "${REPO_ROOT}/Lib/runtime/managed_packet.c"
  "${REPO_ROOT}/Lib/runtime/session.c"
  "${REPO_ROOT}/Lib/runtime/session_io.c"
)

target_compile_features(snn_head_host_layer_test PRIVATE c_std_17 cxx_std_17)

target_include_directories(snn_head_host_layer_test PRIVATE
  "${SNN_HEAD_DIR}"
  "${SNN_HEAD_DIR}/board_test"
  "${REPO_ROOT}/tests/runtime/mocks"   # evalsoc_noc.h / nuclei_sdk_soc.h host shim
  "${REPO_ROOT}/Lib"
  "${REPO_ROOT}/Lib/runtime"
  "${REPO_ROOT}/third_party/flatbuffers/include"
)

target_compile_definitions(snn_head_host_layer_test PRIVATE
  SNN_HEAD_FIXTURE_DIR="${SNN_HEAD_DIR}/fixtures"
)

# 关键：ASan + UBSan 抓越界/覆盖/未对齐；-no-pie 让 --defsym 绝对符号生效。
set(SNN_HEAD_SAN_FLAGS
  -fsanitize=address,undefined
  -fno-omit-frame-pointer
  -fno-sanitize-recover=all
  -g -O1 -fno-pie
)
target_compile_options(snn_head_host_layer_test PRIVATE ${SNN_HEAD_SAN_FLAGS})

# _size 符号：地址即字节数（复刻目标板 objcopy 语义），由链接器绝对符号提供。
target_link_options(snn_head_host_layer_test PRIVATE
  -fsanitize=address,undefined
  -no-pie
  -Wl,--defsym,snn_head_fc1_lif_artifact_size=1301844
  -Wl,--defsym,snn_head_block0_lif_artifact_size=2535884
  -Wl,--defsym,snn_head_block1_lif_artifact_size=2535884
  -Wl,--defsym,snn_head_fc2_artifact_size=2536040
  -Wl,--defsym,snn_head_fc3_artifact_size=1
)

enable_testing()
add_test(NAME snn_head_host_layer_test COMMAND snn_head_host_layer_test)

# ---------------------------------------------------------------------------
# 第二个可执行：choreography 一致性校验（跑真实层 + 哨兵回显 + 逐元素断言）。
# 与 codec 压测共用 mock/shim/被测源；额外加 C++ 辅助（artifact 补丁 + 输出映射枚举）。
# ---------------------------------------------------------------------------
add_executable(snn_head_host_choreo_test
  run_choreo_host.c
  host_noc_mock.c
  host_artifact_shim.c
  host_artifact_support.cpp
  "${SNN_HEAD_DIR}/snn_head.c"
  "${SNN_HEAD_DIR}/snn_head_fc1_lif.c"
  "${SNN_HEAD_DIR}/snn_head_block_lif.c"
  "${SNN_HEAD_DIR}/snn_head_fc2.c"
  "${SNN_HEAD_DIR}/snn_head_fc3.c"
  "${SNN_HEAD_DIR}/snn_head_params.c"
  "${REPO_ROOT}/Lib/nn_layernorm.c"
  "${REPO_ROOT}/Lib/nn_quant.c"
  "${REPO_ROOT}/Lib/debug.c"
  "${REPO_ROOT}/Lib/runtime/artifact_reader.cpp"
  "${REPO_ROOT}/Lib/runtime/frame_codec.c"
  "${REPO_ROOT}/Lib/runtime/managed_packet.c"
  "${REPO_ROOT}/Lib/runtime/session.c"
  "${REPO_ROOT}/Lib/runtime/session_io.c"
)

target_compile_features(snn_head_host_choreo_test PRIVATE c_std_17 cxx_std_17)

target_include_directories(snn_head_host_choreo_test PRIVATE
  "${SNN_HEAD_DIR}"
  "${SNN_HEAD_DIR}/board_test"
  "${REPO_ROOT}/tests/runtime/mocks"
  "${REPO_ROOT}/Lib"
  "${REPO_ROOT}/Lib/runtime"
  "${REPO_ROOT}/third_party/flatbuffers/include"
)

target_compile_definitions(snn_head_host_choreo_test PRIVATE
  SNN_HEAD_FIXTURE_DIR="${SNN_HEAD_DIR}/fixtures"
)

target_compile_options(snn_head_host_choreo_test PRIVATE ${SNN_HEAD_SAN_FLAGS})

target_link_options(snn_head_host_choreo_test PRIVATE
  -fsanitize=address,undefined
  -no-pie
  -Wl,--defsym,snn_head_fc1_lif_artifact_size=1301844
  -Wl,--defsym,snn_head_block0_lif_artifact_size=2535884
  -Wl,--defsym,snn_head_block1_lif_artifact_size=2535884
  -Wl,--defsym,snn_head_fc2_artifact_size=2536040
  -Wl,--defsym,snn_head_fc3_artifact_size=1
)

add_test(NAME snn_head_host_choreo_test COMMAND snn_head_host_choreo_test)

add_custom_target(run
  COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
  DEPENDS snn_head_host_layer_test snn_head_host_choreo_test
)
