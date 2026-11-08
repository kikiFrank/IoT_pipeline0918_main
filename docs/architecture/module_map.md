# 模块与项目目录映射

> 代码核对基线：`b878219`；核对日期：2026-11-08。本文由外部架构草稿逐项对照本地源码后整理。
> `verified` = 当前源码/配置直接支持；`inferred` = 结构推断；`board-verified` = 当前版本有板端证据；`needs-confirmation` = 证据不足。本次只读代码，未新增 board-verified 结论。

## 1. 源码职责（verified）

| 路径 | 当前职责 / 主要文件 | 变更关联 |
|---|---|---|
| `main.cpp` | 参数、信号、加载/规划/构图、服务装配、主循环与收尾 | 生命周期与启动脚本 |
| `src/config/` | IniParser、TxtSnapshotLoader | 快照格式、模型/流字段 |
| `src/orchestrator/` | PipelineOrchestrator、PlanExporter | 分组、stage、ExecutionPlan 与图导出 |
| `src/pipeline/` | GstPipelineBuilder、GstElementHelper、GstLinkHelper、AppSinkCallbacks、结构/DOT 导出 | 元素工厂、连接、探针、推理及媒体分支 |
| `include/aibox/runtime/` | RuntimeRegistry 等运行句柄记录 | 元素、pads、probes、释放顺序 |
| `src/aggregator/` | ResultAggregator | 每帧检测聚合 |
| `src/detection/` | FrameEventBuilder、FrameEventPublisher、NatsFrameTransport、CaptureTimestampTracker、Uuid | Protobuf、时间、ID、发布语义 |
| `src/adapter/` | NatsAdapter | Core NATS 连接、发布、订阅和有界待发缓存 |
| `src/media/` | AnnotatedRtspServer、FrameSnapshotCache、AnomalySnapshotService、编码/文件写入、SnapshotRequestSubscriber、NatsMediaReadyTransport | 帧所有权、图片关联、RTSP 和媒体消息 |
| `src/control/` | PipelineControlServer | PAUSE/RESUME、状态查询；当前拒绝 RELOAD_MODEL |
| `src/siglip_worker/` | 独立 main、SiglipWorker、SiglipPipelineBuilder、ImagePreprocess、AlarmTaskConsumer、SiglipNatsClient、EmbeddingJson/Outbox、配置/JSON 辅助 | 独立进程、durable、幂等、落盘 |
| `include/aibox/` | 对应子系统接口 | 跨模块契约 |
| `plugins/` | 独立 CMake 插件项目及后处理模块 | ABI、factory、pads/caps、线程 |
| `contracts/imsdk/1.0/` | aibox_contracts.proto | 根 CMake 生成 Protobuf，跨语言 tag 兼容 |
| `config_snapshots/` | 按场景拆分 pipeline/stream/algorithm/model/labels | 示例配置不是任意设备默认值 |
| `data/` | 模型/资源；部分大文件未入库 | 配置路径、真实资产身份 |
| `output/` | 本地产物 | 不当作源码/当前板端验收证据 |

## 2. 构建与插件边界（verified）

根 [CMakeLists.txt](../../CMakeLists.txt) 构建主程序、Worker、共享库与可选测试；插件没有统一通过根 `add_subdirectory` 纳入，而由 [board_build_unified.sh](../../scripts/board_build_unified.sh) 遍历独立项目构建。插件名称及代码来源见 [插件目录](plugin_catalog.md)。

FrameMeta common 是共享库与元数据注册插件，不是带 sink/src pad 的视频处理元素。QTI 系统元素如 qtimlqnn、qtimlvconverter、qtimetamux、qtivoverlay 的实际加载实现还依赖板端 SDK/runtime；不可把全部供应商源码归为本仓库实现。

## 3. 当前脚本入口（verified，不表示已执行）

| 路径 | 调用职责 |
|---|---|
| `scripts/board_build_unified.sh` | 主程序/Worker + 各独立插件构建和拷贝 |
| `scripts/start_all.sh` | prepare-consumer→YOLO26 CNN→Worker；Worker 失败可留下 CNN |
| `scripts/webrtc_start_common.sh` | 场景公共启动、图形/VPU gate、READY 与 AGG 检查 |
| `scripts/webrtc_stop.sh` | 先 Worker，再 yolo_x_stacklight_stop.sh 的 CNN 停止路径 |
| `scripts/ppe/yolox_ppe_start.sh` | PPE 场景 |
| `scripts/1stage_andon/1stage_andon_start.sh` | 单阶段 Andon 场景 |
| `scripts/andon/andon_start.sh` | 两阶段 Andon 场景 |
| `scripts/raisehands/raisehands_start.sh` | 手势级联场景 |
| `scripts/yolo26/webrtc_start12.sh` | YOLO26 12 路配置入口；名称不是验收证明 |
| `scripts/siglip/siglip_start.sh`、`siglip_stop.sh`、`worker_process.sh` | Worker 独立启动/停止及进程身份辅助 |
| `scripts/siglip/run_board_tests.sh` | 当前 SigLIP 板端测试入口 |
| `scripts/test_frame_cache_config.sh` | CMake/CTest 调用的缓存配置测试 |
| `scripts/vpu_health.sh` | 资源检查与按策略调用 reset_vpu.sh，不能只按名字理解为只读 |

## 4. 当前测试与文档（verified）

`tests/unit/` 保留缓存/媒体/控制；`tests/ppe/`、`tests/1stage_andon/`、`tests/raisehands/` 保留算法回归；`tests/siglip_worker/` 保留现役 Worker 链。当前 12 个源文件/脚本，加 [README](../../tests/README.md) 与 [实验归档](../../tests/EXPERIMENTS_ARCHIVE.md) 共 14 个文件。

新需求/修复按功能任务目录组织脚本、日志和报告，具体规则沿用 tests README。架构、经验、ADR、正式验收分别由 [docs 总索引](../README.md) 定义，不移动现有业务目录来迁就文档。

## 5. 变更影响定位

改配置→parser/Plan/快照；改拓扑→builder/registry/plugin；改元数据→所有读写方/共享库构建；改模型→descriptor/converter/QNN/postprocess/算法测试；改消息→本仓库 producer/consumer 与对端版本；改生命周期→main/脚本/任务线程及释放；新增正式验证→validation 索引与对应任务证据。
