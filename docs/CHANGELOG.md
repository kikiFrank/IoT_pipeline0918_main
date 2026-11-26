# Pipeline 变更日志

记录 2026-11-21 至 11-26 在主工作区完成的变更、QCS9075 165 板端的现场处理及已有验收。日期为现场记录时间（北京时间）。独立模型、最简交付版和统一入口的测试批次分别记录，便于对应到当时的代码和配置。[当前架构](architecture/README.md) · [板端验收索引](validation/README.md)

## 2026-11-26 · 统一入口整理与微服务重启

- **修改｜脚本和配置精简。** 统一入口迁到 `scripts/start_all/`，以各脚本位置解析项目根目录。统一启动与独立 PPE／Light 共用 Weston 启动逻辑，沿用板端已有的 `aibox-weston.service`，仅在服务未运行时启动。更新运维提示，退役旧组合停止脚本和帧缓存配置测试脚本及其 CMake 注册，保留帧缓存实现、策略单测和通用的 VPU 检查。清理对应模型已移除的旧快照与入口。将独立 PPE 快照目录改名为 `ppe_gesture1123`，保留原启停脚本名称。Face 保留组件、插件、模型和专用配置，并归档复接方法。[运行生命周期](architecture/runtime_lifecycle.md) · [当前入口](architecture/module_map.md#3-当前脚本入口verified不表示已执行) · [Face 复接](engineering-notes/06_face_unified_pipeline_reintegration.md)
- **修复｜微服务重启路径。** Rule Engine、Data Manager 与 Rule Projector 的 systemd 启动命令仍指向已删除的测试目录。将三个当前运行版本的二进制部署到 `/usr/local/libexec/aibox/`，用持久化覆盖更新 `ExecStart`，规则配置落在 `/etc/aibox/rule_engine.runtime.toml`。重启后 9 个应用微服务运行，`/health` 正常，12 路摄像头和 4 个 Zone 的 ID 一致，Projector consumer 可用，Pipeline 预检通过。[部署细节](architecture/runtime_lifecycle.md#5-165-板端微服务持久化部署) · [重启验收](validation/README.md#2026-11-26--165-板端微服务重启)
- **验收｜1126 目录构建与统一启停。** 首轮 Weston 冷启动通过图形检查，GDM 桌面会话保持活动。随后发生短暂 SSH/Ping 断连，SigLIP2 的 `cam-007` 无法打开源 RTSP，首轮未达到全路就绪，断连根因待确认。重试在 Ready 后连续观察 **60.9 秒**，8 路 CNN、4 路 SigLIP2 的 5 个场景持续出结果，8 路标注 RTSP 实拉确认 H.264／1080p。统一停止确认本次 supervisor 和 5 个工作进程退出、8555 监听释放。此轮覆盖构建、短时进展、标注流和停止。[1126 验收记录](validation/README.md#2026-11-26--165-板端统一-rtsp-启停)

## 2026-11-25 · SigLIP2 扩展、12 路混跑、PPE 告警和 Events Zone

- **新增与修改｜实时 SigLIP2。** 扩到人员聚集、物料阻塞和工作站整理等场景，每路加载自己的 ROI、候选文本及 token。每个场景进程启动时一次性计算文本向量并保存在内存，释放文本链后在同一选定 HTP 上启动实时图像编码。C++ 按实验方法计算正负分数，Rule Engine 按 `(camera_id, scene)` 维护连续样本、恢复和冷却状态。三路 W8A16 联跑中，物料阻塞触发分差从 `+15 pp` 调到 `+8 pp`，恢复仍为 `−15 pp`，三路均产生告警和恢复，共核对 22 条截图记录。该轮云端上传返回 `403 InvalidAccessKeyId`。后续为 `cam-021` 加入推车、纸箱两套候选词，共享一次图像编码，规则独立，板端连续两轮均各触发一次推车阻塞和一次纸箱阻塞告警。各场景现行参数以统一配置为准。[评分与规则契约](architecture/data_contracts.md#5-siglip2-实时场景的正负分数) · [统一配置](../config/unified_rtsp.json)
- **新增｜12 路统一启停。** Supervisor 直接启动可执行程序，一份 CNN 进程处理 4 路 PPE 和 4 路 Light，四份 SigLIP2 Scene 进程各处理一路，共 5 个场景。按相机检查帧或分数进展，部分失败时回收本次进程，停止时按进程身份释放本次 RTSP 和推理资源。校准 Pipeline 输入、微服务摄像头及转发映射，让原始流和标注流对应同一视频源。主工作区使用统一 JSON，最简版验收使用拆分的 TXT 快照。初版 165 混跑观察 250 秒，最简 TXT 版另测 60.2 秒，覆盖 12 路推理或评分及停止收尾。[摄像头分配](architecture/system_overview.md) · [统一生命周期](architecture/runtime_lifecycle.md)
- **新增｜PPE 逐人缺失告警。** Rust Rule Engine 对四路全画面逐人匹配帽和工服，按部位独立判定可见性，归属或可见性无法判断时暂缓该项。同一人同时缺两项时合并为一条告警。最简版另一次观测超过 120 秒，共核对 49 条规则事件、告警、SQLite 行和 MQTT 客户端日志。该轮没有人工逐人真值，图片关联状态为 40 条 `ready` 和 9 条 `ready_near`，远端图片当时仍为 `pending`。[规则与媒体链路](../../IoT_edge/aibox/arch_docs/Detection_Alarm_MediaUpload_Chain.md#四路-ppe-告警) · [PPE 验收](validation/README.md#2026-11-25--165-板端统一-rtsp-与-ppe-上报)
- **新增与回退｜Events 截图 Zone。** 回退直播编码链里的 Zone 绘制，改在 PPE、Light、SigLIP2 共用的截图编码路径按相机绘制。新生成的 JPEG 与缩略图使用最新生效的 Zone 快照，已写入的素材保留当时的样式。沿用 Live View 默认六色轮换，填充不透明度约 7.84%。12 路隔离验收检查新增、移动、删除，共核对 48 张原图和 48 张缩略图，抽查 Mock-03 标注 WebRTC 帧确认直播回退。后续 88 秒实流验收覆盖 Mock-02（PPE）、Mock-05（Light）和 Mock-11（SigLIP2）的真实告警图片及缩略图。本地生成和 Events 关联已验证，抽查的上传状态仍为 `pending`。[绘制和验收细节](engineering-notes/04_events_nats_media_andon.md#55-events-截图的-zone-热更新)

## 2026-11-24 · 新源接入、TimeMux 兼容与 SigLIP2 首轮验收

- **修改｜替换 12 路输入并兼容混合帧率。** 将独立 PPE／Light 和微服务摄像头、MediaMTX 映射同步到 `192.168.50.140:8556`。12 路均为 H.264 High、1920×1080 逐行，`cam-016` 为 15 fps 且未显式标注色彩矩阵，其余为 25 fps、BT.709。通过配置启用 TimeMux 已有的 `canonical_raw`，统一到 1920×1080 NV12。该次适配范围是配置文件。板端 12 路直拉及转发解码成功，PPE 隔离短测 12 路均进入推理，退出后释放硬解会话，Light 通过配置及执行计划检查。浏览器连续播放属于另一项验收。PPE 1123 权重和 `0.70` 置信度也同步到了本地。[TimeMux 拓扑](architecture/pipeline_topology.md#6-统一-rtsp-当前拓扑) · [模型契约](architecture/data_contracts.md)
- **新增与验收｜工作站整理单路。** `cam-048` 首轮验收沿用实验相似度计算，按板端 W8A16 结果采用 `+10 / −15 pp`、5 秒冷却。连续三个完整周期各出两条杂乱告警和两条恢复，12 条截图均就绪。既有较长回放仍有 1/13 轮缺第二条杂乱告警，当时的云端截图上传因 `403 InvalidAccessKeyId` 待复验。该轮文本、图像分用 HTP 0/1，后来切为同 HTP 顺序运行。5 秒为该单路验收批次的参数。[实时场景契约](architecture/data_contracts.md#5-siglip2-实时场景的正负分数)

## 2026-11-23 · 流切换、模型更新与现场故障

- **修改｜临时 RTSP 源和模型。** 原 RTSP 源失效期间在 WSL 发布 `192.168.50.59:8554/cam0` 至 `cam11`。同步板端及本地相应快照、板端微服务数据库与 MediaMTX 映射，板端 12 路直拉及转发解码均通过。Light 的标签 JSON 与模型描述引用同步为红、黄、绿、灰（`none`）。PPE 板端配置改用 1123 W8A16 模型，安排由用户手动启动验证。次日再切到 `192.168.50.140:8556`。[现行模型与标签](architecture/data_contracts.md)
- **现场修复｜管理页面连接。** Windows 访问 8444 报 `ERR_CONNECTION_CLOSED`。板端抓包未见对应连接，同机 WSL 访问 HTTPS 返回 200，Windows 测试连接被 `aTrustXtunnel` 接管到本机回环。用户关闭 aTrust 后能够打开 Live View。播放花屏作为下列独立故障跟踪。

### SQLite 数据库损坏与恢复

- **现象。** 微服务进程仍在运行，摄像头列表读取失败，页面出现 `Failed to load cameras`。`/var/lib/aibox/db/aibox.db` 的完整性检查失败，日志有 `database disk image is malformed`、`disk I/O error` 和 `database is locked`。只读查询仍能读到 12 路摄像头与 2 个算法。现场发现 `data_manager` 持有已删除的旧 WAL/SHM，目录里又出现同名新文件。
- **根因状态：待确认。** 日志文件状态异常已证实。删除或替换操作的来源、最初损坏的触发顺序仍未查明。此前修改 RTSP 地址使用微服务 RPC，现场记录没有证据将数据库损坏归因于该操作。
- **处理。** 暂停写入，保全原库与仍可读取的旧 WAL，离线从当前数据恢复并重建索引，再切换到通过检查的新库、恢复 `data_manager`。11 月 6 日旧备份仅作检查，配置没有回退。原库、旧日志、恢复记录和未归属片段当时保存在 `/mnt/workspace/db_repair_20261123_143556/`。
- **复验：恢复通过。** 完整性和外键检查通过。登录、摄像头和算法列表均返回 HTTP 200，读到 12 路摄像头及 2 个算法，原有用户、Zone 和摄像头地址得到保留。告警的媒体缺失引用为 0，新告警和媒体记录可写，复验窗口未再见同类错误或进程持有已删除的数据库文件。PPE PID 未改变。历史媒体是否完全无损仍待确认。

### Events 截图上的异常 `human` 红框

- **现象。** 多条图像详情在相同位置显示红色人员框。样本 `9b77aed7-44ba-484d-91ab-844fe8be0167` 在页面左侧显示 `human · 0.88`，原始 JPEG 同位置没有这条红框，画面右上仍有 Pipeline 烧录的彩色模型标注。
- **根因：固定框已定位。** 当时板端实际提供的前端 JS 与旧 `Events.tsx` 均在存在类别和分数时按 `left=8%`、`top=15%`、`width=10%`、`height=26%` 画框。样本类别 `human` 和分数 `0.876472…` 来自告警，样本接口 `metadata` 为空。同期抽样 300 条告警中 292 条的类别为 `human`，是另一项记录，不能用固定框解释告警类别数量。
- **处理。** 当前本地前端源码已移除这段固定坐标的红框，展示现有告警图片，类别和分数仍在详情区。11 月 24 日 Pipeline 源码同步时按要求跳过前端红框改动。[当前 Events 详情组件](../../IoT_edge/aibox-web/src/pages/Events.tsx)
- **复验：原问题已对照确认，修复后板端复验待确认。** 原始 JPEG、告警 API 与当时板端实际 JS 已对照核验。当前本地源码差异确认固定红框块已移除，现有记录未证明该前端版本已部署到 165，也未找到同一告警在新版本板端页面复验的结果。

### Live View 二次启动后花屏（11 月 21 日首报，11 月 23 日再现）

- **现象。** 用户报告首次正常，通过 `light1121_stop.sh` 停止后再启动，Live View 出现彩色宏块、拖影、卡顿，个别窗口黑屏。11 月 21 日现场检查时只有一个正在运行的 PPE Pipeline 进程，原始流和第 3 路 Pipeline 直出标注抓图清晰，MediaMTX 已记录队列满及 RTP 丢包。11 月 23 日页面恢复访问后再次出现花屏，该轮 WSL 推流端和板端 MediaMTX 反复报 `write queue is full`，板端解码日志有 H.264 参考帧缺失，浏览器当时同时维持 12 路原始流和 12 路标注流的 WebRTC 会话。
- **根因状态：二次启动触发条件待确认。** 已证实现场存在转发拥堵、RTP 丢包和双会话。没有同条件的停启对照，无法确定重连、关键帧恢复或残留会话在第二次启动时的具体作用，也没有证据确认 Light 停止脚本未清理干净。[当前 Live View 会话选择](../../IoT_edge/aibox-web/src/pages/LiveView.tsx)
- **处理。** 当时采用原流、Pipeline 直出和 MediaMTX 转发分段取样排查，未专门为花屏部署修补。提出关闭隐藏的原始 WebRTC 会话、对标注转发固定 TCP 并做连续停启对照，这些处理当时没有执行记录。次日把视频源迁到 `192.168.50.140:8556`，同时完成混合帧率的 TimeMux 配置适配。
- **复验：分段后续验证通过，二次启动花屏回归待确认。** 11 月 24 日新源逐路直拉和转发能解码，PPE 短测 12 路进入推理。11 月 26 日统一入口重试后 8 路标注 RTSP 均实拉成功。这些批次覆盖了新源下的拉流和 Pipeline 进展，现有记录缺少相同播放条件下的连续启停、浏览器 12 画面观察及丢包对照。[板端验收索引](validation/README.md)

## 2026-11-21 · 桌面与 GLES 共存、两套 YOLOX 模型接入

- **修复｜GDM 桌面与 GLES 同时可用。** 原 DRM Weston 路径与桌面争用物理显示设备，GDM 活动时还观察到用户桌面登录失败。Pipeline 改用独立 headless/Pixman Weston 和专用 Wayland socket，`qtimlvconverter` 的 GLES 转换仍使用 Adreno。桌面侧单独使用 Xorg 软件渲染恢复用户会话。165 板端验证桌面登录、GLES 转换、12 路推理及抽样标注输出，用户确认桌面和 Live View 检测框均可见。当次没有重启整板，显示器热插拔未实测。历史详见 11 月 21 日日常目录的《QCS9075／165 板端桌面与 GLES pipeline 共存修复报告》。[现行图形启动](architecture/runtime_lifecycle.md#2-统一-rtsp-启停)
- **修复｜Live View 旧输入地址。** Pipeline 正常推理时，微服务仍有前四路引用过期 RTSP，出现源 404 与 WHEP 502。通过摄像头 RPC 更新生效数据库并同步 MediaMTX，四路原始流恢复解码，抽样两路带标注流出帧，用户在 Live View 确认可见检测框。
- **新增与验收｜PPE＋Gesture 和 Light。** 两个微调 W8A16 YOLOX-S 模型分别接入独立单模型 Pipeline。PPE 使用同人 `human/gesture` 共享抑制组，帽和工服独立保留。165 在 `/mnt/workspace/1121new_model` 构建，两套各观察 20 秒，均为 12 路推理计数持续增长，抽样首尾标注流能解码，四项后处理测试通过。两套均正常停止。历史验收摘要为 `output/validation_1121.json`，本轮覆盖数据链路和关闭行为，识别精度未评估。[后处理模块](architecture/plugin_catalog.md) · [当前模型契约](architecture/data_contracts.md)
