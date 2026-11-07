# 构建与板卡运行

环境准备见 [项目 README](../../../../README.md)。核对应用 `Makefile` 的 `PROGRAM`、`CORE`、`DOWNLOAD` 和诊断开关，以当前应用与[构建规则](../../../../Build/Makefile.rules) 为准。

- 编译、重建或上传时读 [构建与下载](common/build-and-flash.md)。
- 需要运行证据时才读 [串口操作](common/serial-capture.md)，先捕获再上传。

只清理选定应用的目标；构建失败不上传，上传失败先排错。源码或宏改变后，用同一组参数执行 `clean all` 和后续 `upload`。使用匹配宿主系统/架构的 PAIRV 定制 OpenOCD，验证后显式传入；缺失时停止下载，不用未经验证的系统版本替代。

记录实际 ELF hash、参数与原始日志。区分构建、下载、启动、协议完成和数值正确；上传成功、空日志或单次恢复不能替代运行结论。临时诊断结束后恢复源码并重建。
