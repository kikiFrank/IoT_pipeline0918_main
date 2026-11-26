# AI Box Pipeline：精选工程知识库

> 整理前审计基线：`kikiFrank/IoT_pipeline0918_simplify` `b993627`；清理结果已记录于提交 `b878219`（2026-11-08）。旧仓库经验的历史来源为 `IoT_pipeline_0918` `486d0c3`。本文档集提炼历史实验与工程经验，不是原始日志备份。

| 文档 | 内容 |
|---|---|
| `01_models_preprocess_qnn.md` | PPE/Andon/Raisehands/1stage Andon 模型契约、GLES/ROI/NMS/量化、QNN 与模型替换经验 |
| `02_rtsp_codec_multistream_vpu.md` | H.264/H.265、TCP/UDP、颜色 caps、12/16 路 VPU 与延迟实验 |
| `03_siglip_worker_webrtc_debug.md` | SigLIP2 Worker 运行/停止 Runbook、预处理实测、幂等与 ACK、WebRTC/RTSP 冻结、GPU/Weston 排障 |
| [04 · 事件、媒体与 Andon](04_events_nats_media_andon.md) | 事件契约、逐事件图片可追溯、Events 截图 Zone 热更新、灯色和手势业务时序 |
| `05_validation_failures_and_checklists.md` | 已验收实验边界、已知失败/未闭环状态、回归矩阵和检查清单 |
| [06 · Face 复接手册](06_face_unified_pipeline_reintegration.md) | Face 的代码、插件、模型和配置依赖，以及接回统一 Pipeline 的步骤 |

## 原始 docs 的处理

原 10 篇 Markdown 已整理后删除，包括原 `docs/SigLIP2_Worker_Runbook.md` 和 Model Notes。Runbook 的部署入口、成功标记、停止语义、配置路径、模型 SHA、durable、outbox/failures、幂等/ACK、构建测试命令和验收边界已合并到 [Worker 专题](03_siglip_worker_webrtc_debug.md)。

六组实验输出目录中的原始日志、截图、probe、DOT 和 JSON 已提炼关键结论，原目录已删除。文中引用的本仓库已清理路径标为“历史来源（整理前文件，已删除）”，不是当前工作区文件或可打开的证据链接；需要查阅整理前内容时，可使用 `git show b993627:<原仓库相对路径>`。旧仓库路径另标历史来源，不属于本仓库当前文件树。

## tests 的原则

本次清理最终结果：原 50 个测试文件中，**38 个已删除、12 个测试源文件/脚本保留**，另新增 2 篇 Markdown。一次性部署、现场协议/组合验收、运行观测、Tensor 探针、抓帧抓包与故障复现工具均已退役，方法已归档；保留的是核心回归与现役 SigLIP 测试依赖。

- [当前保留的测试入口与任务目录规范](../../tests/README.md)：现存路径、用途及运行方法。
- [历史脚本方法与逐文件清理结果](../../tests/EXPERIMENTS_ARCHIVE.md)：已退役脚本的断言、实验逻辑与原 50 个文件的去向。

清理结果以以上仓库内文档为准，后续维护无需查找仓库外的 ZIP、CSV 或清理建议文件。删除历史脚本不表示其对应的未闭环故障已经修复。

## 结果可信度

- “通过”只适用于文档记录的板端、模型、拓扑和测试时间窗，不自动等于生产精度或所有摄像机已验收。
- 观察到但未定位的异常保留为事实，不推断根因。
- SigLIP2 的隔离测试通过，不代表真实业务告警到平台展示的完整链路已验收。
- 历史 Face Worker 与现役 alarm-ready 驱动的 SigLIP2 Worker 是不同实现，不能直接复用其线程/帧丢弃策略。
