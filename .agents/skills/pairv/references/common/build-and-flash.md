# 构建与下载

## 参数与环境

按 [项目 README](../../../../../README.md) 在当前终端准备工具环境。核对目标应用 `Makefile` 和 `SoC/evalsoc/build.mk`；当前目标为 `n307fd`，`DOWNLOAD`、配置帧路径和诊断宏以应用约束为准。交叉工具检查失败时停止，不用宿主编译器代替。

下列示例使用 Bash 变量语法；Windows 按 README 的终端约定传入相同 Make 参数。

## 构建与身份

先将 `PROGRAM` 和 `DOWNLOAD` 设为当前任务的已确认值，再执行：

```bash
make CORE=n307fd DOWNLOAD="$DOWNLOAD" PROGRAM="$PROGRAM" clean all
```

复现故障时按应用实际支持的选项启用日志。使用 C runtime 的应用可传 `RV_DEBUG_ENABLE_LOGGING=1 RVRT_ENABLE_STATS=1`；不要给不使用该库的应用添加无效宏。性能/尺寸测量按任务关闭诊断。不同宏值必须分别 `clean all`，对象目录不会自动区分。

确认无构建错误，记录实际 ELF 的 SHA-256、编译参数和 size。临时诊断或基线 ELF 须保留副本并与实际上传文件核对；诊断结束后恢复源码、重建普通 ELF。

## 下载与启动

将 `OPENOCD` 设为已验证的 PAIRV 定制 OpenOCD 可执行文件路径，在同一环境执行：

```bash
make CORE=n307fd DOWNLOAD="$DOWNLOAD" PROGRAM="$PROGRAM" OPENOCD="$OPENOCD" upload
```

除示例公共参数外，保留该次构建的全部应用参数。Windows 的可执行文件和路径按实际工具环境传入；不要把 `-x` 或固定目录当成跨平台安装检查。

`upload` 在加载后将 PC 设为 `_start` 并继续执行。若用户任务包含运行验证，先按[串口采集](serial-capture.md) 打开读取器，再上传。只要求编译时不访问板卡。

上传/启动失败时先检查目标、工具版本及重叠的 OpenOCD/GDB 会话。每轮构建、上传、观察串行执行；不根据旧 ELF 或“命令退出码为零”宣称固件正常运行。
