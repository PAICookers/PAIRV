# 资料查询与索引

仅在 SDK、器件手册、CPU/NICE 设计问题或资料索引维护任务中读取。按问题选择最小必要资料，不一次加载全部来源。当前实现以当前 checkout 源码为准。

## 选择来源

| 问题 | 来源与代码落点 |
| --- | --- |
| SDK 通用机制、构建、BSP、启动与驱动 | [Nuclei SDK 官方仓库](https://github.com/Nuclei-Software/nuclei-sdk)、[官方文档](https://doc.nucleisys.com/nuclei_sdk/)；对照 `Build/`、`SoC/evalsoc/` 与所选应用 |
| PAIRV 实际宏、地址、下载与 XIP | 当前 `README.md`、`Makefile`、`setup.sh`、`SoC/evalsoc/build.mk`、`evalsoc.h`、`system_evalsoc.c` 与板级 OpenOCD/linker 文件 |
| W25Q01NW 器件命令、电气、时序 | 对应型号与修订的数据手册正文；控制器地址和配置仍查当前源码 |
| CPU/NICE 架构与设计动机 | 用户指定的设计报告章节；实际编码和调用查 `Lib/nice_primitives.h`、`cpufeature.h`、`application/baremetal/nice/` |

查询 SDK API、配置或版本时，若环境有 Context7，先解析并查询 Nuclei SDK；不可用或无匹配时使用上述官方来源。记录实际查阅日期和 tag/commit，不把在线默认分支视为 PAIRV 当前版本。资料冲突时分别说明版本、页码和适用层次，不静默合并。

## 可选本地目录

需要外部 PDF 路径或本地页码索引时，先检查同目录下的 `local/reference-index.md` 是否存在，存在才读取。该文件可含本机绝对路径，由技能内的 `.gitignore` 排除，不加入发布文件；它不是本技能运行的必需依赖。

文件不存在时仍可查当前源码与官方在线资料；问题确实依赖特定 PDF 时，说明缺少哪份资料，并请求用户提供文件位置。不要猜测其他机器的目录，也不把同名型号或附近章节当作替代来源。列出的路径还需检查存在性；失效时报告缺口。

## 提取、引用与维护

仅提取回答需要的 PDF 页码。例如，具备 Poppler 时：

```bash
pdftotext -f "$FIRST_PAGE" -l "$LAST_PAGE" -layout "$PDF_FILE" -
```

工具缺失时使用当前环境可用的 PDF 阅读工具，不因资料查询要求板卡工具链。明确区分 PDF 物理页和正文印刷页，优先采用用户指定范围。

回答给出结论、来源名称/URL或文件位置、版本/页码/代码落点；推断明确标为推断。本地文件位置可在当前会话引用，发布或提交前移除机器标识。

扩展资料时只记录唯一 ID、名称/类型、版本/日期、位置、适用问题、页码规则与歧义，不复制大段正文。可移植官方条目维护在本文件；本机路径及私有资料目录只更新 `local/reference-index.md`。验证新增位置存在/可访问，不将本地目录加入版本控制。
