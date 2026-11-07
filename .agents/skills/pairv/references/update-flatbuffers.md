# FlatBuffers 更新

仅在用户要求升级并指定 `vX.Y.Z` 正式 tag 时执行。工具准备见[项目 README](../../../../README.md)；脚本使用 Bash，获取头文件不需要交叉工具链。

先检查 `third_party/flatbuffers/` 与 `Lib/runtime/generated/compile_artifacts_generated.h` 的状态及版本。镜像有修改、未跟踪或忽略文件时停止，保留现场。

在仓库根将 `tag` 设为用户指定的正式发布标签，执行：

```bash
bash .agents/skills/pairv/scripts/refresh_flatbuffers_headers.sh "$PWD" "$tag"
```

默认从官方 `https://github.com/google/flatbuffers.git` 获取精确 tag。只有用户指定镜像时才设置 `FLATBUFFERS_UPSTREAM_URL`；报告实际来源，不内置某台机器的代理。脚本验证版本与 Git tree 清单，完整替换头文件树，更新 vendored README 的版本记录；替换失败时恢复原目录和记录。无需先加载 SDK 环境。

按 [绑定生成说明](../../../../Lib/runtime/generated/README.md) 检查版本断言；不一致时使用同一版本 `flatc` 和权威 schema 重新生成，不能只改断言数字。schema/工具缺失时保留未完成状态，不猜测字段。

用已跟踪的测试构建实际消费者。将 `BUILD_DIR` 设为新的、可写且不覆盖现有结果的目录，选择本机 C/C++ 编译器与 CMake generator：

```bash
cmake -S tests/runtime -B "$BUILD_DIR"
cmake --build "$BUILD_DIR" --config Debug
ctest --test-dir "$BUILD_DIR" -C Debug --output-on-failure
```

Windows 多配置 generator 的构建与 CTest 使用相同 `--config` / `-C`。静态库、sanitizer 或编译器不适用于当前环境时报告实际缺口，不把未执行的测试记为通过。

再按 [构建与下载](common/build-and-flash.md) 准备交叉工具链并 `clean all` 构建 `application/baremetal/flatbuffers`（`DOWNLOAD=ilmflashxip`）。不触发上传。报告版本、tag/commit、文件数量、绑定处理、Host 测试与交叉构建，保留失败日志。

不更新其他依赖，不自动提交或推送。
