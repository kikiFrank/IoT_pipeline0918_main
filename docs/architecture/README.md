# Pipeline 架构索引

> 源码核对日期 2026-11-25。基准为主工作区当前源码及配置。既有板端证据见 [验收索引](../validation/README.md)。

## 阅读顺序

1. [系统总览](system_overview.md)：进程职责、微服务集成边界与数据流。
2. [模块与目录映射](module_map.md)：源码、头文件、插件、配置、脚本、测试各自归属。
3. [Pipeline 拓扑](pipeline_topology.md)：time-mux/serial、batch 与输出分支。
4. [插件目录](plugin_catalog.md)：factory、pads、关键属性、线程和共享 ABI。
5. [运行生命周期](runtime_lifecycle.md)：启动、READY、停止、PID 与资源恢复。
6. [数据契约](data_contracts.md)：FrameMeta、模型 Tensor、NATS、快照与 outbox。
7. [迁入核对报告](REVIEW.md)：草稿修正、源码内部不一致及待审核事项。

## 文档与实现的关系

本目录按主工作区的 C++、配置及 supervisor 描述架构，跨服务行为对照同一工作区的 `IoT_edge/aibox`。统一入口、旧场景脚本、实时 SigLIP2 Scene 和告警图片 Worker 分别记录其职责、配置与交付语义。

文中的源码事实以对应链接为依据。现场运行结果关联实际运行目录和观测范围，见 [板端验收](../validation/README.md)。单次实测的进程进展和告警证据分别对照其对应日志。旧版迁入过程见 [历史审核报告](REVIEW.md)。

## 关键来源

- [主构建](../../CMakeLists.txt)、[主进程](../../main.cpp)
- [Builder 接口](../../include/aibox/pipeline/GstPipelineBuilder.hpp)、[构图实现](../../src/pipeline/GstPipelineBuilder.cpp)
- [规划实现](../../src/orchestrator/PipelineOrchestrator.cpp)
- [板端构建脚本](../../scripts/board_build_unified.sh)、[测试索引](../../tests/README.md)
- [工程经验](../engineering-notes/README.md)、[决策规则](../decisions/README.md)、[验收规则](../validation/README.md)

修改架构文档时检查模块归属、拓扑、插件 ABI、模型边界、消息语义和生命周期是否一起变化，不机械更新所有文件。
