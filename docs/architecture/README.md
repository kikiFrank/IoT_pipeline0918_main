# Pipeline 架构索引

> 代码核对基线：`b878219`；核对日期：2026-11-08。本文由外部架构草稿逐项对照本地源码后整理。
> `verified` = 当前源码/配置直接支持；`inferred` = 结构推断；`board-verified` = 当前版本有板端证据；`needs-confirmation` = 证据不足。本次只读代码，未新增 board-verified 结论。

## 阅读顺序

1. [系统总览](system_overview.md)：进程职责、微服务集成边界与数据流。
2. [模块与目录映射](module_map.md)：源码、头文件、插件、配置、脚本、测试各自归属。
3. [Pipeline 拓扑](pipeline_topology.md)：time-mux/serial、batch 与输出分支。
4. [插件目录](plugin_catalog.md)：factory、pads、关键属性、线程和共享 ABI。
5. [运行生命周期](runtime_lifecycle.md)：启动、READY、停止、PID 与资源恢复。
6. [数据契约](data_contracts.md)：FrameMeta、模型 Tensor、NATS、快照与 outbox。
7. [迁入核对报告](REVIEW.md)：草稿修正、源码内部不一致及待审核事项。

## 文档与实现的关系

`last_verified_commit: b878219`。工作区已有的工程知识库/实验归档修改只影响文档，未改变本次核对的业务源码。下次代码变更时，按实际影响更新对应文档及核对基线。

本目录采用指南指定的 `architecture/`，承接微服务项目中“arch 文档”的组织方式；不把外部微服务源码或整个产品架构搬进 Pipeline。外部服务端的契约兼容性、具体仓库归属与版本尚未核对，标为 `needs-confirmation`。

`verified` 仅说明源码或配置支持某个描述；不等于元素已在设备加载、模型已实测或端到端链路已通过。当前板端证据缺失的地方明确保持 `needs-confirmation`。完整运行拓扑以对应版本、快照及环境下的实际 DOT/日志为准。

## 关键来源

- [主构建](../../CMakeLists.txt)、[主进程](../../main.cpp)
- [Builder 接口](../../include/aibox/pipeline/GstPipelineBuilder.hpp)、[构图实现](../../src/pipeline/GstPipelineBuilder.cpp)
- [规划实现](../../src/orchestrator/PipelineOrchestrator.cpp)
- [板端构建脚本](../../scripts/board_build_unified.sh)、[测试索引](../../tests/README.md)
- [工程经验](../engineering-notes/README.md)、[决策规则](../decisions/README.md)、[验收规则](../validation/README.md)

修改架构文档时检查模块归属、拓扑、插件 ABI、模型边界、消息语义和生命周期是否一起变化，不机械更新所有文件。
