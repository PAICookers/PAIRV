# NMSIS 更新

仅在用户要求升级并指定发布 tag 时执行，不自行选择版本、分支或浮动提交。未初始化时按 `.gitmodules` 初始化目标子模块；工具准备见[项目 README](../../../../README.md)。刷新脚本用 Bash 执行，交叉工具链须在该 Bash 中可用。

从仓库根检查主仓库及子模块状态。目标范围有暂存、工作树或未跟踪源文件时先保留现场，不通过清理用户文件绕过检查。顶层镜像的忽略文件也会阻止刷新；子模块允许已忽略的正常构建输出，需另行确认其中没有用户源码。

将 `tag` 设为用户指定的发布标签，只获取该引用：

```bash
git -C third_party/NMSIS fetch origin "refs/tags/${tag}:refs/tags/${tag}"
git -C third_party/NMSIS switch --detach "refs/tags/${tag}"
git -C third_party/NMSIS describe --tags --exact-match HEAD
```

验证 HEAD 精确等于请求 tag 的 commit，且不是分支；失败不回退到 `main`。确认上述工具环境后执行：

```bash
bash .agents/skills/pairv/scripts/refresh_nmsis_prebuilt.sh "$PWD" "$tag"
```

脚本固定构建 `rv32imafdc/ilp32d` 的 NMSIS-NN，刷新以下受控镜像并记录版本：

- `NMSIS/Core/Include/`、`NMSIS/NN/Include/`、`NMSIS/build.mk`。
- `NMSIS/Library/NN/GCC/libnmsis_nn_rv32imafdc.a`。
- `NMSIS_VERSION`：使用 `v` 前缀的版本记录。

不要混用不同 tag 的头文件、build.mk 与库。检查 submodule diff、库内容/哈希以及源库和镜像库逐字节一致，然后构建实际消费者：

```bash
make CORE=n307fd DOWNLOAD=ilm PROGRAM=application/baremetal/simple_nn clean all
```

本脚本清理并复制受控镜像，不提供多文件事务回滚。复制失败时保存日志和 diff，检查受影响的镜像范围后恢复；禁止 `git reset --hard` 或清理无关文件。报告旧/新 commit、请求 tag、版本记录、库 SHA-256 和链接结果。依赖构建、离线链接与真实板卡分别记录；此流程不触发下载。

不自动提交或推送；构建失败不得复制不完整库。
