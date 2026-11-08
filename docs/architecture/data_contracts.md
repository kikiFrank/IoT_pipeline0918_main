# 数据契约与跨模块接口

> 代码核对基线：`b878219`；核对日期：2026-11-08。本文由外部架构草稿逐项对照本地源码后整理。
> `verified` = 当前源码/配置直接支持；`inferred` = 结构推断；`board-verified` = 当前版本有板端证据；`needs-confirmation` = 证据不足。本次只读代码，未新增 board-verified 结论。

## 1. 配置与执行计划（verified）

[TxtSnapshotLoader](../../src/config/TxtSnapshotLoader.cpp) 读取 config_snapshots 中的主文件及 streams/algorithms/models include；[Plan](../../include/aibox/orchestrator/Plan.hpp) 定义规划数据，PipelineOrchestrator 生成，Builder 消费。字段改名/默认值变更需同时审查配置、loader、plan、builder 与启动包装。

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
| [PPE descriptor](../../config_snapshots/ppe_4cam/models/yolox_ppe_w8a16.txt) | images，NHWC，640×640×3，uint16；output shape 1,8400,8，dtype=uint16；centre/reverse、gles、confidence=0.05、NMS=0.45 | 历史工程记录的 QNN 下游 FLOAT32 与这里的 uint16 声明不一致；当前实际输出 dtype、转换位置和量化参数需验证 |
| [八类 Andon descriptor](../../config_snapshots/1stage_andon_4cam/models/yolox_s_color8_640_w8a16.txt) | images，NHWC，640×640×3，uint16；output 1,8400,13、uint16；8 类、confidence=0.25、NMS=0.65 | 不从 W8A16 名字推断所有边界语义；当前实际 runtime Tensor/精度需验证 |
| [Worker config](../../config_snapshots/siglip_worker/worker.txt) | 模型预期 SHA 已配置；uint16 NHWC 1,224,224,3；expected_embedding_dim=768；HTP device=1 | 配置 SHA 不等于本次核验了真实 BIN；模型 raw output 量化须以运行时 tensor meta/实际模型核对 |

每个模型完整记录应有不可变文件身份、SDK/后端、输入/输出名称/shape/layout/dtype/scale/offset、预处理几何/颜色/mean/sigma、后处理类别/阈值/NMS及实际样本。历史量化数字保留在 [工程经验](../engineering-notes/01_models_preprocess_qnn.md)，不提升为当前板端 verified。

Worker [ImagePreprocess](../../src/siglip_worker/ImagePreprocess.cpp) 用 libjpeg 解码 RGB，双线性缩放至 224×224，生成无损 PNG；[SiglipPipelineBuilder](../../src/siglip_worker/SiglipPipelineBuilder.cpp) 的 appsrc→queue→pngdec→videoconvert/RGB→qtivtransform/DMA→qtimlvconverter→qtimlqnn→qtimlpostprocess_roi→text/appsink。后处理 image 模块按实际 tensor meta 反量化并 L2 归一化，EmbeddingJson 校验 768 个有限值及归一化边界；不是简单把 QNN U16 raw 数组直接写成向量。

## 4. NATS 主题与载荷（verified：本仓库端）

| 方向（以本仓库为准） | 主题 / 调用 | 载荷与交付边界 |
|---|---|---|
| CNN 发布 | detection.frame | FrameEvent Protobuf v2；Core NATS，有界 pending 与 latest-per-camera 重发，不是持久 exactly-once |
| CNN 订阅 | event.pipeline.snapshot.request | JSON，必填 alarm_id/camera_id；可选 frame_id/source_event_id/occurred_at_ms；回调解析并入队 |
| CNN 发布 | event.pipeline.snapshot.failed | JSON alarm_id/reason；无法解析并提取身份的非法请求只计数/日志，不保证发失败事件 |
| CNN 发布 | event.stream.media_ready | JSON，见 MediaReadyEvent；不得与 datamgr alarm_ready 混同 |
| CNN RPC 接收/回复 | rpc.pipeline.control / rpc.pipeline.control_state | Protobuf；支持 PAUSE/RESUME/状态，RELOAD_MODEL 明确拒绝 |
| Worker 拉取消费 | event.datamgr.alarm_ready | JetStream 专属 durable；必填 alarm_id、event_id、timestamp_ms，source_event_id 可选 |
| Worker RPC 请求 | rpc.datamgr.alarm.list / rpc.datamgr.media.get_meta | 本仓库按 JSON 请求/读取；对端 schema/服务版本仍需跨仓确认 |

来源：[Frame transport](../../include/aibox/detection/NatsFrameTransport.hpp)、[media transport](../../include/aibox/media/NatsMediaReadyTransport.hpp)、[snapshot subscriber](../../src/media/SnapshotRequestSubscriber.cpp)、[control server](../../src/control/PipelineControlServer.cpp)、[AlarmTaskConsumer](../../src/siglip_worker/AlarmTaskConsumer.cpp)。

[aibox_contracts.proto](../../contracts/imsdk/1.0/aibox_contracts.proto) 定义 FrameEvent tag：camera_id=1、timestamp_ms=2、detections=3、algorithm_code=4（string）、coordinate_space=5、frame_width=6、frame_height=7、frame_id=8（string）、schema_version=10；tag 9 按注释为 Rust 侧空位。源码只写注释，未用 proto reserved 语法，不能误称编译器已强制 reserved。Detection normalized_bbox=8。

[FrameEventBuilder](../../src/detection/FrameEventBuilder.cpp) 当前写 schema_version=2/NORMALIZED，归一化裁剪到 [0,1]，丢弃无效面积；confidence 从百分比转 0..1。空 proto3 ControlReq 可合法表示 enum=0 的 PAUSE，不能按空 payload 一概拒绝。

[MediaReadyEvent](../../include/aibox/media/MediaReadyEvent.hpp) JSON 含 event_id/file_path/size_bytes/thumbnail_path/camera_id/timestamp_ms/alarm_id/frame_id；thumbnail_path 始终写出，未知或旧触发路径的若干标识可为空。下游是否兼容必须对照实际微服务，不能从本仓库注释宣称对端已验收。

## 5. Worker durable、outbox 与 ACK（verified）

[SiglipNatsClient](../../src/siglip_worker/SiglipNatsClient.cpp) 要求 stream 已存在并包含 subject，拒绝 WorkQueue retention。新建专属 consumer 为 DeliverNew/AckExplicit/MaxDeliver=-1/MaxAckPending=1；已有 consumer 校验契约后绑定，不自动 reset。InProgress、NakWithDelay、Term、AckSync 分别有明确调用路径；服务端无限投递配置与应用 max_processing_attempts=10 是不同层次。

[EmbeddingOutbox](../../src/siglip_worker/EmbeddingOutbox.cpp) 提交为 O_EXCL 临时文件→write/fsync→同目录 link 创建最终文件且拒绝覆盖→unlink 临时文件→fsync 目录，不是 rename 覆盖。[SiglipWorker](../../src/siglip_worker/SiglipWorker.cpp) 在成功提交后 AckSync，已存在的幂等记录可直接确认；失败归档后 Term，写盘失败抛出不确认消息；停止/可重试错误 Nak。不同 alarm_id 分别落盘，同图缓存不合并业务记录。

当前 output.mode=local_outbox，无云端 embedding 上传器；outbox 落盘成功也不能证明外部平台消费完成。业务真实事件到页面展示的兼容性和当前板端结果均为 needs-confirmation。
