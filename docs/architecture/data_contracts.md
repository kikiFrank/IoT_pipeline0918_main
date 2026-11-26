# 数据契约与跨模块接口

> 源码核对日期 2026-11-26。基准为主工作区当前源码及配置。既有板端证据见 [验收索引](../validation/README.md)。

## 1. 配置与执行计划（verified）

[TxtSnapshotLoader](../../src/config/TxtSnapshotLoader.cpp) 读取 TXT 主文件及 streams/algorithms/models include。统一入口从 JSON 渲染本次运行的 TXT 快照；[Plan](../../include/aibox/orchestrator/Plan.hpp) 定义规划数据，PipelineOrchestrator 生成，Builder 消费。字段改名/默认值变更需同时审查配置、loader、plan、builder 与启动包装。

## 2. FrameMeta：共享进程内 ABI（verified）

唯一现役结构定义为 [gstaiboxframemeta.h](../../plugins/aiboxframemeta_common/gstaiboxframemeta.h)，生命周期由 [common 实现](../../plugins/aiboxframemeta_common/gstaiboxframemeta.cpp) 注册到 GstMeta。按声明顺序：

| 字段 | C 类型 / 语义 |
|---|---|
| meta | GstMeta 基部 |
| camera_id / algorithm_group_id / time_mux_group_id | gchar*；由 meta 自身持有的字符串 |
| mux_slot_id / frame_id / pts | guint / guint64 / GstClockTime；路由 slot、帧身份、PTS |
| global_cam_index | guint；跨 mux group 相机索引 |
| batch_group_id / batch_stream_id | gchar* / guint；batch 身份，未设置 stream ID 为 0xFFFFFFFF |
| source_raw_caps | GstCaps*；源 raw caps，持有引用 |

init 将字符串/caps 置 null、pts=GST_CLOCK_TIME_NONE、batch ID 置无效 sentinel；free 释放字符串并 unref caps；transform 复制标识、batch 字段，并对 caps 引用计数。不得复制结构到其它插件形成第二份 ABI。头文件要求追加字段且保持旧偏移，但实际 sizeof/offset 依编译器/架构，需全部依赖共同重编译和板端验证，不宣称跨版本自动二进制兼容。

主要读写路径为 timemux/camsplit/streamdemux、Builder probes/appsink、qtifaceinfer；跨硬件 buffer 的 metadata 传播尚需当前板端证据。

## 3. 模型 Tensor：配置声明与实际边界分开（verified / needs-confirmation）

| 模型配置 | 已核对的配置事实 | 尚不能作出的结论 |
|---|---|---|
| [统一 PPE 1123](../../config/unified_rtsp.json) | YOLOX-S W8A16，标签顺序 human/white_hat/white_cloth/gesture，配置 output 1,8400,9，confidence=0.70，NMS=0.45，results=50，模块 `yolox-ppe-gesture1121` | 原始 Tensor dtype、量化与真值识别指标需用对应 BIN 和运行样本核验 |
| [统一 Light 1121](../../config/unified_rtsp.json) | YOLOX-S W8A16，标签顺序 red/yellow/green/none，配置 output 1,8400,9，confidence=0.25，NMS=0.65，results=50，模块 `yolox-light1121`。框颜色对应红、黄、绿、灰 | 单帧灯框可供下游评估，灯色持续时长和触发行为由规则配置决定 |
| [历史 PPE descriptor](https://github.com/kikiFrank/IoT_pipeline0918_main/blob/5e40fe10b2d3057f59f3b40e2403f7e6680cfee3/config_snapshots/ppe_4cam/models/yolox_ppe_w8a16.txt) | `images`，NHWC，640×640×3，uint16。输出 `[1,8400,8]`，uint16。配置为 centre/reverse、GLES、confidence=0.05、NMS=0.45 | 历史工程记录的 QNN 下游 dtype 为 FLOAT32，具体转换位置和量化参数需按当时版本验证 |
| [历史八类 Andon descriptor](https://github.com/kikiFrank/IoT_pipeline0918_main/blob/5e40fe10b2d3057f59f3b40e2403f7e6680cfee3/config_snapshots/1stage_andon_4cam/models/yolox_s_color8_640_w8a16.txt) | `images`，NHWC，640×640×3，uint16。输出 `[1,8400,13]`，uint16。8 类，confidence=0.25、NMS=0.65 | 运行期 Tensor 契约与精度按具体模型和后端验证 |
| [Worker config](../../config_snapshots/siglip_worker/worker.txt) | 模型预期 SHA 已配置；uint16 NHWC 1,224,224,3；expected_embedding_dim=768；HTP device=1 | 配置 SHA 不等于本次核验了真实 BIN；模型 raw output 量化须以运行时 tensor meta/实际模型核对 |

每个模型完整记录应有不可变文件身份、SDK/后端、输入/输出名称/shape/layout/dtype/scale/offset、预处理几何/颜色/mean/sigma、后处理类别/阈值/NMS及实际样本。历史量化数字保留在 [工程经验](../engineering-notes/01_models_preprocess_qnn.md)，不提升为当前板端 verified。

Worker [ImagePreprocess](../../src/siglip_worker/ImagePreprocess.cpp) 用 libjpeg 解码 RGB，双线性缩放至 224×224，生成无损 PNG；[SiglipPipelineBuilder](../../src/siglip_worker/SiglipPipelineBuilder.cpp) 的 appsrc→queue→pngdec→videoconvert/RGB→qtivtransform/DMA→qtimlvconverter→qtimlqnn→qtimlpostprocess_roi→text/appsink。后处理 image 模块按实际 tensor meta 反量化并 L2 归一化，EmbeddingJson 校验 768 个有限值及归一化边界；不是简单把 QNN U16 raw 数组直接写成向量。

## 4. NATS 主题、载荷与 Rust 对端

| 方向（以本仓库为准） | 主题 / 调用 | 载荷与交付边界 |
|---|---|---|
| CNN 发布 | detection.frame | FrameEvent Protobuf v2；Core NATS，有界 pending 与 latest-per-camera 重发，不是持久 exactly-once |
| SigLIP2 Scene 发布 | `scene.siglip2.score` | Core JSON，包含相机、场景、帧、采集时间、stream_epoch、sample_sequence、monotonic_ms、prompt_version 和正负分数 |
| SigLIP2 Scene 订阅 | `event.pipeline.snapshot.request.siglip2` | 相机过滤，按 frame_id 取缓存。该 subject 关闭时间回退 |
| CNN 订阅 | event.pipeline.snapshot.request | JSON，必填 alarm_id/camera_id；可选 frame_id/source_event_id/occurred_at_ms；回调解析并入队 |
| CNN 发布 | event.pipeline.snapshot.failed | JSON alarm_id/reason；无法解析并提取身份的非法请求只计数/日志，不保证发失败事件 |
| CNN 发布 | event.stream.media_ready | JSON，见 MediaReadyEvent；不得与 datamgr alarm_ready 混同 |
| CNN、SigLIP2 Scene 订阅 | `event.cloud.zones` | 接收版本化 Zone 全量快照，按相机更新抓拍绘制配置 |
| CNN、SigLIP2 Scene RPC 请求 | `rpc.datamgr.zone.list` | 启动时拉取 Zone 全量快照，此后每 5 秒尝试刷新，用于补齐漏收的更新 |
| CNN RPC 接收/回复 | rpc.pipeline.control / rpc.pipeline.control_state | Protobuf；支持 PAUSE/RESUME/状态，RELOAD_MODEL 明确拒绝 |
| Worker 拉取消费 | event.datamgr.alarm_ready | JetStream 专属 durable；必填 alarm_id、event_id、timestamp_ms，source_event_id 可选 |
| Worker RPC 请求 | rpc.datamgr.alarm.list / rpc.datamgr.media.get_meta | 本仓库按 JSON 请求/读取；对端类型和实现见 IoT_edge 的告警与媒体 RPC |

来源：[Frame transport](../../include/aibox/detection/NatsFrameTransport.hpp)、[media transport](../../include/aibox/media/NatsMediaReadyTransport.hpp)、[snapshot subscriber](../../src/media/SnapshotRequestSubscriber.cpp)、[control server](../../src/control/PipelineControlServer.cpp)、[AlarmTaskConsumer](../../src/siglip_worker/AlarmTaskConsumer.cpp)。

[aibox_contracts.proto](../../contracts/imsdk/1.0/aibox_contracts.proto) 定义 FrameEvent tag：camera_id=1、timestamp_ms=2、detections=3、algorithm_code=4（string）、coordinate_space=5、frame_width=6、frame_height=7、frame_id=8（string）、schema_version=10、source_profile_code=11、snapshot_cached=12（optional bool）；tag 9 按注释为 Rust 侧空位。源码只写注释，未用 proto reserved 语法，不能误称编译器已强制 reserved。Detection normalized_bbox=8。

[FrameEventBuilder](../../src/detection/FrameEventBuilder.cpp) 当前写 schema_version=2/NORMALIZED，归一化裁剪到 [0,1]，丢弃无效面积；confidence 从百分比转 0..1。算法分组 ID 写入 `algorithm_code`，终端按相机和分组从执行计划查找稳定的 profile code。缓存成功时写入 `snapshot_cached`。当前 PPE Rust 过滤依据相机 ID 与 `expected_group`，比对当次 `execution_plan.json` 可以确认编号。空 proto3 ControlReq 可合法表示 enum=0 的 PAUSE，不能按空 payload 一概拒绝。

[MediaReadyEvent](../../include/aibox/media/MediaReadyEvent.hpp) JSON 含 event_id/file_path/size_bytes/thumbnail_path/camera_id/timestamp_ms/alarm_id/frame_id；thumbnail_path 始终写出，未知或旧触发路径的若干标识可为空。C++ 以 Core 发布，data_manager 用 MEDIA_READY_PIPELINE 的 durable 消费。媒体关联按 alarm_id，物理素材可共享；普通 CNN 允许就近绑定。PPE observation 的人框目前留在规则事件，AlarmEvent 未承载 PPE 专用人员框。[微服务告警链路](../../../IoT_edge/aibox/arch_docs/Detection_Alarm_MediaUpload_Chain.md)

Zone 快照包含非负整数 `config_version` 和 `zones` 列表。启用的条目包含 `camera_id`、`enabled` 和至少三个 `points`，顶点接受 `{x,y}` 或 `[x,y]`，坐标范围为 `[0,1]`。按相机保留启用条目的快照顺序。收到旧版本、同版本或无效快照时保留当前有效配置。现有 `MediaReadyEvent` 仍按原格式发送，Zone 随 JPEG 和缩略图交付。[Zone 解析和更新实现](../../src/media/SnapshotZoneOverlay.cpp) · [截图绘制说明](../engineering-notes/04_events_nats_media_andon.md#55-events-截图的-zone-热更新)

## 5. SigLIP2 实时场景的正负分数

JSON 配置写入每个场景的正负文本、各 64 个预分词 token 和 prompt_version。Supervisor 将同相机多组场景写入一个进程配置，要求使用同一 ROI。Scene 进程在同一个 HTP 先完成 Text Encoder 推理并释放它，然后开启实时 Image Encoder。优先选择设备节点 `/dev/fastrpc-cdsp1` 对应的 HTP 1，存在 `/dev/fastrpc-cdsp` 时可回退到 HTP 0。候选向量留在进程内，流重连继续复用。

SceneMain 使用两种 L2 归一化向量的点积为 cosine，以 `100 / (1 + exp(-(112.66890226978423 * cosine - 16.771724700927734)))` 分别计算正负分数。Wire 字段沿用 `clutter_score`（正）和 `clean_score`（负）。Rust 按两分之差和对应场景的阈值、连续样本、恢复与冷却判定。阈值见 [统一配置](../../config/unified_rtsp.json)，行为见 [C++ 场景主程序](../../src/siglip_worker/SceneMain.cpp) 与 [Rust 场景监测](../../../IoT_edge/aibox/crates/aibox-rule_engine/src/rule/workstation_organization_monitor.rs)。

## 6. 告警图片 Worker 的 durable、outbox 与 ACK（verified）

[SiglipNatsClient](../../src/siglip_worker/SiglipNatsClient.cpp) 要求 stream 已存在并包含 subject，拒绝 WorkQueue retention。新建专属 consumer 为 DeliverNew/AckExplicit/MaxDeliver=-1/MaxAckPending=1；已有 consumer 校验契约后绑定，不自动 reset。InProgress、NakWithDelay、Term、AckSync 分别有明确调用路径；服务端无限投递配置与应用 max_processing_attempts=10 是不同层次。

[EmbeddingOutbox](../../src/siglip_worker/EmbeddingOutbox.cpp) 提交为 O_EXCL 临时文件→write/fsync→同目录 link 创建最终文件且拒绝覆盖→unlink 临时文件→fsync 目录，不是 rename 覆盖。[SiglipWorker](../../src/siglip_worker/SiglipWorker.cpp) 在成功提交后 AckSync，已存在的幂等记录可直接确认；失败归档后 Term，写盘失败抛出不确认消息；停止/可重试错误 Nak。不同 alarm_id 分别落盘，同图缓存不合并业务记录。

当前 output.mode=local_outbox，无云端 embedding 上传器；outbox 落盘成功也不能证明外部平台消费完成。业务真实事件到页面展示的兼容性和当前板端结果均为 needs-confirmation。
