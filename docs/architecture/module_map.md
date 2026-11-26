# 模块与项目目录映射

> 源码核对日期 2026-11-26。基准为主工作区当前源码及配置。既有板端证据见 [验收索引](../validation/README.md)。

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
| `src/media/` | AnnotatedRtspServer、FrameSnapshotCache、AnomalySnapshotService、SnapshotZoneOverlay、编码/文件写入、SnapshotRequestSubscriber、NatsMediaReadyTransport | 标注 RTSP、帧所有权、抓拍 Zone 绘制、图片关联和媒体消息 |
| `src/control/` | PipelineControlServer | PAUSE/RESUME、状态查询；当前拒绝 RELOAD_MODEL |
| `src/siglip_worker/` | 告警图片 main、实时场景 SceneMain、SiglipWorker、SiglipPipelineBuilder、ImagePreprocess、AlarmTaskConsumer、SiglipNatsClient、EmbeddingJson/Outbox、配置/JSON 辅助 | 独立进程、durable、幂等、落盘 |
| `include/aibox/` | 对应子系统接口 | 跨模块契约 |
| `plugins/` | 独立 CMake 插件项目及后处理模块 | ABI、factory、pads/caps、线程 |
| `contracts/imsdk/1.0/` | aibox_contracts.proto | 根 CMake 生成 Protobuf，跨语言 tag 兼容 |
| `tools/rtsp_supervisor.py` | 统一启停、配置生成、进程所有权与每路进展监测 | 启动门禁与状态、日志 |
| `config/unified_rtsp.json` | 主工作区统一 RTSP 配置，含模型、场景、摄像头 | 运行分配和配置渲染 |
| `config_snapshots/` | 按场景拆分 pipeline/stream/algorithm/model/labels | 示例配置不是任意设备默认值 |
| `data/` | 模型/资源；部分大文件未入库 | 配置路径、真实资产身份 |
| `output/` | 本地产物 | 不当作源码/当前板端验收证据 |

## 2. 构建与插件边界（verified）

根 [CMakeLists.txt](../../CMakeLists.txt) 构建 CNN 主程序、SigLIP2 Scene、告警图片 Worker、共享库与可选测试；插件没有统一通过根 `add_subdirectory` 纳入，而由 [board_build_unified.sh](../../scripts/board_build_unified.sh) 遍历独立项目构建。插件名称及代码来源见 [插件目录](plugin_catalog.md)。

FrameMeta common 是共享库与元数据注册插件，不是带 sink/src pad 的视频处理元素。QTI 系统元素如 qtimlqnn、qtimlvconverter、qtimetamux、qtivoverlay 的实际加载实现还依赖板端 SDK/runtime；不可把全部供应商源码归为本仓库实现。

## 3. 当前脚本入口（verified，不表示已执行）

| 路径 | 调用职责 |
|---|---|
| `scripts/board_build_unified.sh` | CNN / Scene / 告警图片 Worker 与各独立插件构建和拷贝 |
| `scripts/start_all/start_rtsp.sh`、`scripts/start_all/stop_rtsp.sh` | 当前统一 RTSP 启停入口，调用 supervisor 管理本次运行 |
| `scripts/siglip/scene_start.sh`、`scene_stop.sh` | 独立 SigLIP2 场景入口 |
| `scripts/systemd/aibox-weston.service` | 供 GLES 预处理使用的专用 headless Wayland 服务 |
| `scripts/webrtc_start_common.sh` | 场景公共启动、图形/VPU gate、READY 与 AGG 检查 |
| `scripts/_start_adreno_weston_forceon.sh` | 统一与独立 CNN 入口共用的专用 Weston 服务检查及按需启动 |
| `scripts/ppe_gesture1121/ppe_gesture1121_start.sh`、`scripts/ppe_gesture1121/ppe_gesture1121_stop.sh` | 独立 PPE／手势启停，加载 `config_snapshots/ppe_gesture1123/pipeline_ppe_gesture1121.txt` |
| `scripts/light1121/light1121_start.sh`、`scripts/light1121/light1121_stop.sh` | 独立 Light 启停，加载 `config_snapshots/light1121/pipeline_light1121.txt` |
| `scripts/siglip/siglip_start.sh`、`siglip_stop.sh`、`worker_process.sh` | Worker 独立启动/停止及进程身份辅助 |
| `scripts/siglip/run_board_tests.sh` | 当前 SigLIP 板端测试入口 |
| `scripts/vpu_health.sh` | 资源检查与按策略调用 reset_vpu.sh，不能只按名字理解为只读 |

## 4. 当前测试与文档（verified）

`tests/unit/` 保留缓存、媒体和控制相关测试。`tests/ppe/`、`tests/1stage_andon/`、`tests/raisehands/` 保留算法回归。`tests/siglip_worker/` 保留告警图片 Worker 测试。当前 PPE 使用 1123 模型，后处理模块和对应回归仍沿用 `1121` 命名。入口和历史实验分别见 [测试 README](../../tests/README.md) 与 [实验归档](../../tests/EXPERIMENTS_ARCHIVE.md)。

新需求/修复按功能任务目录组织脚本、日志和报告，具体规则沿用 tests README。架构、经验、ADR、正式验收分别由 [docs 总索引](../README.md) 定义，不移动现有业务目录来迁就文档。

## 5. 变更影响定位

改配置→parser/Plan/快照；改拓扑→builder/registry/plugin；改元数据→所有读写方/共享库构建；改模型→descriptor/converter/QNN/postprocess/算法测试；改消息→本仓库 producer/consumer 与对端版本；改生命周期→main/脚本/任务线程及释放；新增正式验证→validation 索引与对应任务证据。
