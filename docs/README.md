# 文档总索引

| 目录 | 回答的问题 | 入口 |
|---|---|---|
| `CHANGELOG.md` | 按时间查阅功能新增、修改、故障修复和板端验收结果 | [变更日志](CHANGELOG.md) |
| `architecture/` | 当前代码由哪些模块组成，进程、数据流、生命周期与契约如何协作 | [架构索引](architecture/README.md) |
| `engineering-notes/` | 历史踩坑、实验条件、结果与可复用处理方法 | [工程知识库](engineering-notes/README.md) |
| `decisions/` | 为什么选择某项架构方案，替代方案与代价是什么 | [ADR 规则](decisions/README.md) |
| `validation/` | 某个版本、板卡、模型与配置到底验证了什么 | [验收记录规则](validation/README.md) |
| `../tests/` | 可执行回归入口、单次任务脚本/日志/报告与实验归档 | [测试索引](../tests/README.md) |

架构描述以当前代码为依据；历史经验保留原时间与适用边界；正式验收必须链接对应任务证据。四者互相引用，不复制同一份内容。项目源码目录保持现有职责划分；本次调整的是文档信息架构。

## 当前运行入口

主工作区使用 `scripts/start_all/start_rtsp.sh` 和 `scripts/start_all/stop_rtsp.sh` 管理 12 路。默认读取 `config/unified_rtsp.json`。进程、拓扑和跨服务契约见架构文档，既有板端记录见验收索引。[运行生命周期](architecture/runtime_lifecycle.md) · [系统总览](architecture/system_overview.md) · [板端验收](validation/README.md)

2026-11-08 的草稿迁入记录和当时待核对项见 [历史审核报告](architecture/REVIEW.md)。

## 后续同步规则

- 新增/删除模块：更新 [module_map](architecture/module_map.md)。
- 改元素连接、拓扑或模式分派：更新 [pipeline_topology](architecture/pipeline_topology.md)。
- 改插件 pads/properties/caps/线程：更新 [plugin_catalog](architecture/plugin_catalog.md)。
- 改启动、停止、资源释放：更新 [runtime_lifecycle](architecture/runtime_lifecycle.md)。
- 改 FrameMeta、模型或 NATS 契约：更新 [data_contracts](architecture/data_contracts.md)。
- 新的持久架构取舍：提交 Proposed ADR，经明确决策后再标 Accepted。
- 新的已验证经验或正式验收：分别更新工程知识库或 validation 索引，并链接功能任务报告。

普通局部修复只更新受影响文档。历史文件状态与清理去向以现有 [实验归档](../tests/EXPERIMENTS_ARCHIVE.md) 为准，不依赖仓库外 ZIP/CSV。
