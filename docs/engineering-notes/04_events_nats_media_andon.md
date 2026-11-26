# NATS 事件、媒体图片、Andon 与 Raisehands 验收经验

> 把本仓库 2026-11-02/03 的历史链路验收与旧仓库 2026-08 的事件合约实验严格分开。原验收文档和实验输出已整理后删除，本文保留当时条件与结果。**检测结果传输成功，不等于颜色语义/报警规则/图片上传已完成**。

当前 CNN、SigLIP2、PPE 告警与媒体 v2 的端到端边界见 [数据契约](../architecture/data_contracts.md) 和 [微服务告警链路](../../../IoT_edge/aibox/arch_docs/Detection_Alarm_MediaUpload_Chain.md)。第 5 节继续描述 CNN 的检测触发及告警请求两条取图路径。

## 1. 三个不同层面的“事件通过”

| 层 | 典型数据 | 实测和未验证边界 |
|---|---|---|
| 检测传输 | `detection.frame` Protobuf v2，`coordinate_space=NORMALIZED`，`camera_id`、bbox、class | 历史 1stage Andon、Raisehands 各有四路订阅解码通过，覆盖对应版本的传输及标签检查 |
| 原生分类/画面 | `parent-id` 绑定的灯色/手势、appsink native metadata、overlay | 双模型 Andon 和 Raisehands 逐 ROI 绑定/画面可见；当前 detection.frame 不携带独立颜色/手势属性 |
| 业务告警/图片 | rule-engine → alarm/ready → data-manager/media-upload/云端 | 此仓库的 Pipeline 测试不能单独证明微服务、SigmaForge 页面每事件有图，需跨仓联测 |

不能把“视频抓到四路画面”和“规则已收到四路正确颜色”当成同一件事；同样，`FrameEvent` 空检测也不意味着消息丢失。

## 2. 历史 Andon V3 级联三轮的可复用断言

2026-11-02：修复版 YOLOX + MobileNetV3，四路、`batch=1`，零 ROI、单 ROI、多 ROI 三轮都报告 `ANDON_VIDEO_ACCEPTANCE_OK`。原 `tests/andon/run_video_acceptance.sh` 已退役，关键断言已归档：

```text
zero : 四路视频继续输出，0 ROI => 0 分类调用/输出
single: 四路有有效灯具，0 <= rois <= 1, rois == classified
multi : 四路可有多个灯具，rois == classified, no ANDON_BINDING_ERROR
all  : camera_id 有效、RTSP可抓帧、NATS 可解码、停止到 NULL、iris_vpu refcnt=0
```

历史 `video_acceptance.log`（整理前输出，已删除）记录零/单/多轮分别得到 `ANDON_ROUND_OK zero/single/multi`，四路 `FRAME_BINDING_OK`；并记录了不活跃的外部客户端请求其它 camera 导致 `no media/no factory for path`。旧测试只将**配对证明属于未发布路径**的外部请求归为无关噪声，不能全局忽略所有 RTSP ERROR。

**历史记录的实际误判**：原 `docs/andon_repaired_validation_20261102/captures/multi/cam-004.jpg`（已删除）曾记录一处亮红灯 Top1 为 `None`。这并未否定当时“数据通路/ROI 绑定”验收通过，但明确说明分类准确率不能被认为已通过。模型原始评测 mAP 或分类 accuracy 也不能代替摄像头端级联标注集评测。

## 3. 1stage Andon 八类与 Raisehands：事实与业务边界

**1stage Andon（2026-11-03）**：八类输出的合成单测 `COLOR8_POSTPROCESS_TEST_OK`；四路真实 H.265/TCP、抓帧、客户端重连、NATS Protobuf v2 四路收到非空检测、`ONE_STAGE_ANDON_ACCEPTANCE_OK`。120.032s 短观测 25 个 RSS 样本 545804～570564 KiB，不能说长时无泄漏。原 `docs/1stage_andon_validation_20261103/events.json`（已删除）的 Green/None/Red 样本只证明实际出现部分标签，不是八类真实场景召回/精度测试。

**Raisehands（2026-11-03）**：PPE YOLOX 只过滤 Human→ROI 外扩 5% 并裁剪→MobileNetV3 Top1 `Gesture/Normal`；四路零/单/多 ROI、`rois==classified`、parent-id 保持、NATS Human-only，`RAISEHANDS_ACCEPTANCE_OK`。多 ROI 轮额外约 120.041s，RSS 552100～570908 KiB；同样不证明长时稳定。原 `docs/raisehands_validation_20261103/events.json`（已删除）记录四路样本计数为 cam-001=5、cam-002=3、cam-003=2、cam-004=4；这只是当次采样，不能外推事件发生率。**告警协议没有增加 Gesture 字段，不能说微服务手势报警已验收**。

## 4. 旧 NATS Contract 测试：为什么比一条 `PASS` 更重要

旧 `test/aggregator_nats/IR-DATA-001_验收测试记录.md` 报告 3/3 CTest 通过（合约、隔离 NATS、重连）。具体实况记录：

| 实验 | 发布的 FrameEvent | 非空检测 | 正确表述 |
|---|---:|---:|---|
| 25s 初次实况 | 584 | 0 | 空帧发布验证通过；出框未通过 |
| 40s 第二轮 | 949 | 0 | 同样只是空帧传输验证 |
| 重启后 40s | 375 | 1484 detections | 解码与非空检测通过，UUID/bbox/timestamp 等字段校验无错 |
| 隔离 NATS 重连 | publisher 271 / subscriber 251 | publisher detections 1027 | latest-per-camera republish=1；**断窗允许丢包**，不是 exactly-once |

测试使用隔离端口，避免重启生产 `4222`。明确区分 Core NATS at-most-once、JetStream durable/ack 的传递语义；不同事件主题不一定由同一协议编码（Protobuf 与业务 JSON 不能乱用一个解析器）。

## 5. 当前 Pipeline 的两条抓拍路径（源码核对）

`main.cpp` 建立 `FrameEventPublisher`、`AnomalySnapshotService`、`FrameSnapshotCache` 与 `SnapshotRequestSubscriber`。缓存先安装到服务，随后启动服务和订阅者。订阅失败时程序仅打印 `alarm snapshot requests disabled`；此时不能凭 `detection.frame` 正常发布就认定告警抓拍可用。停止时先停抓拍服务，再停订阅者并清空缓存，避免失败回调访问已销毁对象。

### 5.1 检测触发：类别集合变化与 holdoff

`SnapshotTriggerPolicy` 按 camera 维护检测类别集合；集合为空、与上次写入相同或仍在 holdoff 内时不触发。触发时，`AnomalySnapshotService` 只编码/写入一次 JPEG，并对该帧非空的 detection `event_id` 分别发布 `event.stream.media_ready`；多个 ID 可以指向同一个 `file_path`。它**不是**每个检测帧、每个 detection 或每个业务 alarm 各生成一张图的保证。

**待审核的失败边界**：`SnapshotTriggerPolicy::Evaluate()` 在实际入队、编码、写盘和 ready 发布之前，就更新 `last_written`。若后续入队条件不满足、队列任务被淘汰或写盘/发布失败，而类别集合一直不变，后续帧可能不会再次触发检测路径抓拍。这是源码顺序推导，未做本次板端故障注入；不能把“失败后必自动重试”写成当前能力。告警请求路径有独立的失败回执与重投边界，也不能与该检测路径混淆。

### 5.2 告警请求：独立队列与帧匹配

`SnapshotRequestSubscriber` 订阅 `event.pipeline.snapshot.request`，解析必填 `alarm_id`、`camera_id`，以及可选 `frame_id`、`source_event_id`、`occurred_at_ms`。解析失败仅计数/记录日志，通常无法提取可用于失败回执的 alarm ID，不能说每个坏请求都发布 `event.pipeline.snapshot.failed`。服务失败回调可为已解析请求发布包含 `alarm_id`、`reason` 的失败消息；NATS 发布本身仍可能失败。

告警队列优先于检测队列处理。源码默认告警队列深度 32、检测队列深度 2；**告警队列满时淘汰最旧的已排队请求并对它报告 `queue_full`，然后接收新请求**。同一 `alarm_id` 在队列中或刚成功完成后会去重，完成后的窗口为 10 秒。服务先按 camera/request frame/timestamp 尝试复用近期已编码 asset；否则从短期帧缓存取图，先精确匹配 `frame_id`，未匹配时按发生时间找默认 500 ms 范围内的最近帧。过期或找不到帧时返回 `no_camera_frame`，不生成伪 ready。

`FrameSnapshotCache` 将样本复制为 packed NV12，不长期钉住 `GstSample`；代码默认每相机最多 16 帧、TTL 3000 ms、最小采样间隔 0、全局预算 256 MiB。实际值受 `[frame_cache]` 配置影响，不能把头文件默认值写成现场参数。

### 5.3 ready 字段与已覆盖范围

| 字段或行为 | 检测触发 | 告警请求 |
|---|---|---|
| `MediaReadyEvent.event_id` | 当前 detection 的 `event_id` | `asset_id`：优先用 camera+timestamp 命名，缺少时间戳才退回 camera+frame ID；不等同于原 detection ID |
| `alarm_id` / `frame_id` | 未由该路径填写 | 请求 alarm ID / 实际匹配并编码的帧 ID |
| `source_event_id` | 不适用 | subscriber 已解析，但当前未复制进 `SnapshotTask` 或 ready 消息 |
| 同帧复用 | 多条 ready 可引用同一 JPEG | 不同 alarm 可分别收到 ready 并引用同一物理 JPEG |

当前 [帧缓存单测](../../tests/unit/frame_cache_policy_test.cpp) 覆盖采样、匹配与预算等边界；[抓拍服务单测](../../tests/unit/snapshot_service_test.cpp) 覆盖两个 alarm 共用编码/文件、分别发布 ready 及停止竞态。它们不能代替真实告警、data_manager 关联、media-upload 和 UI 的跨仓验收。

需要图片的业务事件，应按**每个业务 ID 独立查到正确媒体关联**验收，而不是只数 JPEG。共享文件是否允许、告警 ready 应用哪个关联键，必须按外部平台 API 契约确认；本仓库不能证明云端展示已完成。失败、重投、不同 camera/帧错绑、文件大小与路径有效性都应逐项对账。

### 5.4 旧仓媒体与上传记录的证据边界

本次资料包称：`IoT_pipeline_0918/test/media_snapshot/IR-DATA-002_验收测试记录.md` 与 `test/media_upload/IR-UPLOAD-001_验收测试记录.md` 是空副本，而更早的 `IoT_pipeline` 同名记录有内容。本次工作区未提供这两个旧仓库原件，因此**不能独立确认文件大小、原报告数字或通过状态**；此前“两个历史仓库的记录均为空”的概括也不应继续作为定论。

资料包摘录的旧 `IoT_pipeline` 报告称：2026-08-20 的 40 秒单路实验有 13 张 JPEG、53 条 `media_ready`；2026-08-27 的 60 秒 mock S3 实验有 49 条 `event.media.uploaded`，并观察到约 0.6% 的 alarm→upload 比例。这些是**待原报告核对的历史摘录**，不代表每个 detection/alarm 都有图，也不代表当前生产云端上传通过；0.6% 不能当作当前目标或验收阈值。若需作为正式验收证据，应取得原报告、相应提交与日志后核对测试环境、消息关联键和计数口径。

### 5.5 Events 截图的 Zone 热更新

CNN 与实时 SigLIP2 Scene 使用同一个抓拍服务。`AnomalySnapshotService::EncodeWriteAndThumbnail` 在自己的 packed NV12 图像副本上按相机绘制启用的 Zone，再编码 JPEG 并由同一画面生成缩略图。检测触发和告警请求的新编码路径都经过这里。已有抓拍的复用、精确帧与就近帧匹配沿用各自的策略。[抓拍实现](../../src/media/AnomalySnapshotService.cpp) · [Zone 绘制实现](../../src/media/SnapshotZoneOverlay.cpp)

每路按启用 Zone 的顺序循环使用 `#0A84FF`、`#A855F7`、`#00D4AA`、`#FFA502`、`#FF4757`、`#2ED573`。边线完全不透明，填充的不透明度为 `0x14 / 255`，约 7.84%，与 Live View 的 SVG 样式一致。颜色按顺序计算，因此移除或禁用某个 Zone 后，后续 Zone 的颜色可能顺移。[Live View Zone 组件](../../../IoT_edge/aibox-web/src/components/ZoneOverlay.tsx)

新增、修改、禁用或删除 Zone 在新版本快照生效后反映到新编码的图片上。已有 JPEG 和缩略图保留写入时的 Zone，复用旧素材时也保留原样。Live View 的 `display zones` 由前端独立绘制，标注 RTSP／WebRTC 沿用原有的推理叠加链路。抓拍功能复用现有 Zone 配置和媒体接口。[Zone 消息契约](../architecture/data_contracts.md#4-nats-主题载荷与-rust-对端)

2026-11-25 板端 12 路受控测试检查了 48 张 JPEG 和对应的 48 张缩略图。样式实测覆盖六色循环、回绕与填充。业务样本覆盖 Mock-02（PPE，`ready_near`）、Mock-05（Light）和 Mock-11（SigLIP2，`ready`），验证范围为本地生成和 Events 媒体关联。

## 6. Andon 时序历史计划：只留边界案例，**不是已验收结果**

旧 `scripts/rule_lights/AD_2_1_test_plan.md` 中 `problem_classes=[Red,Red-green]`、problem=3.0s、recovery=2.0s、cooldown=5.0s，结果列原本为空：

- 红灯持续 ≥3.0s：预期 problem；红 2.5～2.8s 就消失：不应 problem。
- 红/绿 1.5s 交替：不应累积成一次持续红灯；红+绿同时出现则仍满足存在红灯的条件（须核对业务定义）。
- problem 后无红 ≥2s：预期 recovered；无红不足 2s 又变红：不能提前 recovered。
- 一只灯灭但**另一只仍红**：不能错误 recovered；空帧连续超过恢复时间时的行为需核对当前规则引擎。

历史脚本还存在 conf≥0.3 与另一次配置使用 0.5 的版本差异；不能直接当成当前线上固定阈值。**重新验证时应使用当前 `rule_engine` 配置并明确输出事件主题和时间窗口**。

### 历史来源与当前测试入口

- 历史来源（整理前文件，已删除）：`docs/` 下的 `Andon_Model_Replacement_20261102.md`、`Andon_YOLOX_MobileNetV2_4cam_Status.md`、`1stage_andon_4cam_20261103.md`、`Raisehands_4cam_20261103.md` 及六组原始实验目录中的事件 JSON、日志和截图；可在本仓库 `b993627` 查阅，当前不再保留原始附件。
- 历史脚本（已退役，方法已归档）：四路抓帧、NATS 短采集、零/单/多 ROI 验收包装，见 [实验逻辑归档](../../tests/EXPERIMENTS_ARCHIVE.md)。
- 当前保留的媒体服务回归：[snapshot_service_test.cpp](../../tests/unit/snapshot_service_test.cpp)；其它算法与事件相关测试入口见 [测试索引](../../tests/README.md)。
- 旧仓库历史来源（非本仓库当前路径）：`IoT_pipeline_0918` `486d0c3` 的 `test/aggregator_nats/IR-DATA-001_验收测试记录.md`、`test/media_snapshot/*`、`test/media_upload/*`、`scripts/rule_lights/AD_2_1_test_plan.md`。
