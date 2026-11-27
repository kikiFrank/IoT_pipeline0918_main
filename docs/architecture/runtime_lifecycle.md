# 运行生命周期、进程与资源管理

> 源码核对日期 2026-11-26。基准为主工作区当前源码及配置。既有板端证据见 [验收索引](../validation/README.md)。

## 1. 主进程（verified）

[main.cpp](../../main.cpp) 完成参数/信号、文件描述符限制、GStreamer 初始化、快照加载、规划/导出、构图、运行模式与服务装配。`--dump-plan-only` 在构图前退出；`--build-only` 构图/导出后退出；实际运行由 run-seconds/run-forever 控制。

正常运行路径启动检测发布、帧缓存、抓拍服务和 Zone 配置订阅，确认 Pipeline PLAYING 后启动配置要求的 AnnotatedRtspServer。标注服务失败输出 `ANNOTATED_RTSP_DEGRADED` 并收尾退出 25。成功时依次输出 `ANNOTATED_RTSP_READY` 和 `PIPELINE_READY`，客户端拉流及业务依赖需单独检查。[抓拍 Zone](../engineering-notes/04_events_nats_media_andon.md#55-events-截图的-zone-热更新)

## 2. 统一 RTSP 启停

在主工程根目录执行：

```bash
bash scripts/start_all/start_rtsp.sh --check
bash scripts/start_all/start_rtsp.sh --print-rules
bash scripts/start_all/start_rtsp.sh
bash scripts/start_all/stop_rtsp.sh
```

- `--check` 默认解析 `config_snapshots/unified_rtsp/pipeline_unified_rtsp.txt` 和相对路径 `include`，并检查静态配置。`--print-rules` 从各场景的 `[rule]` 打印 SigLIP2 规则 TOML 片段。`--config` 可显式选择另一套 TXT 快照或兼容的 JSON。启停时使用同一配置，确保 `state_dir` 相同。
- 统一启动和独立 PPE／Light 启动共用 `_start_adreno_weston_forceon.sh`。该脚本仅在专用 `aibox-weston.service` 未运行时启动它。Weston 使用 headless/Pixman 和独立的 `/run/aibox-wayland/wayland-aibox` socket，GDM 继续负责实体桌面。环境脚本设置 Adreno EGL，图形检查校验服务、运行目录归属、五次 socket 连接、KGSL 权限和 Adreno ICD。
- Supervisor 预检模型及二进制路径、NATS TCP、标注 RTSP bind，以及运行中 Rule Engine 实际读取的 SigLIP2 TOML。PPE 配置由 Rule Engine 单独读取。Supervisor 为每次运行生成 config、`workers.json`、`policy.json`、`state.json`、独立 registry 和日志，记录在 `output/unified_rtsp/run-*`。
- 有共享帧的配置先启动 CNN，再启动消费这些帧的 SigLIP2 Scene。独立 RTSP 场景沿用原启动顺序。CNN 检查 PLAYING 和每相机 AGG 帧进展，SigLIP2 检查文本编码就绪及每个相机、场景的分数进展。启动错误或工作进程退出触发本次 worker 清理。普通运行在 ready 后继续监测停滞并标记 degraded。`bash scripts/start_all/start_rtsp.sh --verify-seconds 60` 在全体 ready 后进入稳定性窗口，并在验收结束时停止本次运行。
- 停止按 `current.json` 找本次 supervisor 和 worker，校验 PID、启动时刻、可执行路径与 boot ID，通过 pidfd 向匹配进程发送信号。先 TERM 后限时检查，工作进程需要强制退出时记录 `forced_pids` 并标记失败。状态文件保留停止证据。

源码：[启动入口](../../scripts/start_all/start_rtsp.sh) · [停止入口](../../scripts/start_all/stop_rtsp.sh) · [Supervisor](../../tools/rtsp_supervisor.py) · [Weston unit](../../scripts/systemd/aibox-weston.service) · [图形检查](../../scripts/aibox_graphics_gate.sh)

同步摄像头地址时，在 `config_snapshots/unified_rtsp/streams/` 按相机维护 `rtsp_url`。若使用 `setup_source_ip.sh` 更新微服务摄像头地址，统一部署应传入 `--skip-pipeline`，再单独更新上述相机文件。

## 3. 正常主进程收尾（verified）

控制 server 停止、移除主循环 watch/timer→标注 RTSP 服务 Stop→有录像时发送 EOS 并限时等待→batch teardown 准备→在 pads/probes 尚存时 SnapshotFrameCounters→设置 NULL 并最多等待 10 秒→停止图片/发布等服务并释放运行资源。

应以 main 的分支实现为准，错误提前退出路径不保证与正常路径完全一致。只有 `gst_element_get_state` 成功且 state==NULL 才打印 Pipeline reached NULL；超时/失败仅能报告不干净，不能据进程消失推断 VPU 已释放。

## 4. 现存场景脚本与告警图片 Worker

### 4.1 独立 CNN 启动链

备份提交 `5e40fe1` 中的旧组合启动先准备 Worker consumer，再启动 YOLO26 和 Worker。当前现场统一运行使用上文的启停入口。

独立 PPE 和 Light 的启动都调用 [公共运行脚本](../../scripts/webrtc_start_common.sh)。启动前检查现有 CNN、NATS、Weston、图形运行时和 VPU。发现已有 CNN 时提示先用其对应的停止入口。启动后使用本次子进程 PID，记录其启动时间，并等待本次日志出现 `PIPELINE_READY` 及所需的 AGG 行数。启动未达到就绪时，按进程身份发送 TERM 并限时等待。AGG 行数阈值仍按累计行数计算。

### 4.2 独立 CNN 与告警图片 Worker 的停止链

[PPE 停止入口](../../scripts/ppe_gesture/ppe_gesture_stop.sh) 和 [Light 停止入口](../../scripts/light/light_stop.sh) 各自读取对应运行目录的 PID 记录，校验启动时间、可执行文件和快照路径，再发 TERM，默认等待 30 秒。若超时报出该 PID 并返回失败。VPU 检查和恢复策略仍由独立启动前的检查处理。

[告警图片 Worker 停止入口](../../scripts/siglip/siglip_stop.sh) 校验 PID、启动时间、可执行文件与配置身份，TERM 后检查 `SIGLIP_SHUTDOWN_OK state=NULL`。

Worker 的 durable/本地提交/ACK 顺序见 [数据契约](data_contracts.md)；SIGLIP_READY 只代表 consumer/图就绪，SIGLIP_FIRST_INFERENCE_OK 才证明出现一次实际推理。

## 5. 165 板端微服务持久化部署

2026-11-26 修正了 Rule Engine、Data Manager 和 Rule Projector 的 systemd 启动路径。对应 unit 的 `99-persistent-runtime.conf` 位于 `/etc/systemd/system/<unit>.service.d/`，`ExecStart` 使用 `/usr/local/libexec/aibox/` 下的 `aibox-rule_engine`、`aibox-data_manager` 和 `aibox-rule-projector`。这些是 165 板端的实际部署路径。

Rule Engine 和 Rule Projector 使用 `/etc/aibox/rule_engine.runtime.toml`。当次部署含 5 个 SigLIP2 场景和 4 路 PPE 规则。微服务重启后可用服务健康检查、摄像头和 Zone 查询、JetStream Projector consumer 以及统一 Pipeline 预检验证依赖。[板端验收](../validation/README.md#2026-11-26--165-板端微服务重启)

## 6. 已有板端证据

2026-11-26 在 `/mnt/workspace/1126/ai-box_stream_pipeline` 板端构建并执行统一启停。重试轮 Ready 后连续观察 60.9 秒，8 路 CNN 标注流可拉取，4 路 SigLIP2 的 5 个场景持续产出分数。停止后确认本次 supervisor 与 5 个工作进程退出、8555 释放。首轮断连、重试和微服务重启分别记录。[验收索引](../validation/README.md)
