# 运行生命周期、进程与资源管理

> 源码核对日期 2026-11-25。基准为主工作区当前源码及配置。既有板端证据见 [验收索引](../validation/README.md)。

## 1. 主进程（verified）

[main.cpp](../../main.cpp) 完成参数/信号、文件描述符限制、GStreamer 初始化、快照加载、规划/导出、构图、运行模式与服务装配。`--dump-plan-only` 在构图前退出；`--build-only` 构图/导出后退出；实际运行由 run-seconds/run-forever 控制。

正常运行路径启动检测发布、缓存/快照与请求服务，确认 Pipeline PLAYING 后启动配置要求的 AnnotatedRtspServer。标注服务失败输出 ANNOTATED_RTSP_DEGRADED 并收尾退出 25；成功先输出 ANNOTATED_RTSP_READY，再输出 PIPELINE_READY。READY 不是客户端已成功拉流，也不是全部业务依赖已通过。

## 2. 统一 RTSP 启停

在主工程根目录执行：

```bash
bash scripts/start_rtsp.sh --check
bash scripts/start_rtsp.sh --print-rules
bash scripts/start_rtsp.sh
bash scripts/stop_rtsp.sh
```

- `--check` 解析主工作区的 `config/unified_rtsp.json` 并检查静态配置。 `--print-rules` 打印 SigLIP2 规则 TOML 片段。它们从脚本的快捷分支直接进入 Python。主工作区的 `--config` 参数可选择另一份 JSON；用同一配置启动和停止，以匹配 `state_dir`。11 月 25 日最简版已核对为 TXT 快照入口 `config_snapshots/unified_rtsp/pipeline_unified_rtsp.txt`。
- 正式启动先检查专用 `aibox-weston.service`，未运行时调用 `_start_adreno_weston_forceon.sh`；环境脚本设置 Adreno EGL 和独立 Wayland socket，gate 检查服务、运行目录归属、五次 socket 连接、KGSL 权限和 Adreno ICD。unit 使用 headless/Pixman、`/run/aibox-wayland/wayland-aibox`。CNN 模型描述仍为 GLES 预处理。
- Supervisor 预检模型及二进制路径、NATS TCP、标注 RTSP bind，以及运行中 Rule Engine 实际读取的 SigLIP2 TOML。PPE 配置由 Rule Engine 单独读取。Supervisor 为每次运行生成 config、`workers.json`、`policy.json`、`state.json`、独立 registry 和日志，记录在 `output/unified_rtsp/run-*`。
- 按配置顺序启动 SigLIP2 后启动 CNN，每个工作进程都等待健康证据。CNN 检查 PLAYING 和每相机 AGG 帧进展，SigLIP2 检查文本编码就绪及每个相机/场景的分数进展。启动错误或工作进程退出触发本次 worker 清理。普通运行返回 ready 后继续由 supervisor 监测停滞并标记 degraded；`bash scripts/start_rtsp.sh --verify-seconds 60` 在全体 ready 后进入专门的稳定性窗口，验证通过后结束该次运行。
- 停止按 `current.json` 找本次 supervisor 和 worker，校验 PID、启动时刻、可执行路径与 boot ID，通过 pidfd 向匹配进程发送信号。先 TERM 后限时检查，工作进程需要强制退出时记录 `forced_pids` 并标记失败。状态文件保留停止证据。

源码：[启动入口](../../scripts/start_rtsp.sh) · [停止入口](../../scripts/stop_rtsp.sh) · [Supervisor](../../tools/rtsp_supervisor.py) · [Weston unit](../../scripts/systemd/aibox-weston.service) · [图形检查](../../scripts/aibox_graphics_gate.sh)

## 3. 正常主进程收尾（verified）

控制 server 停止、移除主循环 watch/timer→标注 RTSP 服务 Stop→有录像时发送 EOS 并限时等待→batch teardown 准备→在 pads/probes 尚存时 SnapshotFrameCounters→设置 NULL 并最多等待 10 秒→停止图片/发布等服务并释放运行资源。

应以 main 的分支实现为准，错误提前退出路径不保证与正常路径完全一致。只有 `gst_element_get_state` 成功且 state==NULL 才打印 Pipeline reached NULL；超时/失败仅能报告不干净，不能据进程消失推断 VPU 已释放。

## 4. 现存场景脚本与告警图片 Worker

### 4.1 历史场景脚本的启动链

[start_all.sh](../../scripts/start_all.sh) 用 lifecycle.lock 串行组合操作：Worker prepare-consumer→YOLO26 场景启动→Worker 启动。CNN 失败返回 20；Worker 失败返回 22，并明确保留已运行的 CNN，不是自动回滚整组。

场景包装调用 [webrtc_start_common.sh](../../scripts/webrtc_start_common.sh)：

1. 选择项目 runtime/vendor 插件库及图形 registry，设置环境。
2. `pgrep -f` 找旧 CNN，`pkill -TERM -f` 终止并限时等待；此处不是逐个校验 executable/starttime 的精确 PID 模型。
3. 检查本机 4222 监听、图形 gate、VPU gate；没有 weston backend 时会尝试 force-on；VPU 默认策略 if-needed，可能执行 reset_vpu。
4. nohup/stdbuf 启动，优先用 `pgrep -x aibox_stream_pi | head -1` 得到 PID，退回 launch_pid。
5. 最多约 90 秒等待本次日志 PIPELINE_READY，再最多约 80 秒累计 AGG 行数达到 AGG_NEED。

AGG 行数阈值不检查每个唯一 camera；READY_FOR_BROWSER 只是历史脚本标记，没有提供 HTML/8080/WebRTC 服务。AGG_TIMEOUT 分支 TERM pidfile 进程并限时等待；BOOT_TIMEOUT/BOOT_GFX_ERROR 等分支没有相同集中清理，不能把注释中的“精确 PID/无孤儿”推广到所有分支。详见 [待审核问题](REVIEW.md)。

### 4.2 历史场景脚本及告警图片 Worker 的停止链

[webrtc_stop.sh](../../scripts/webrtc_stop.sh) 先调用 siglip_stop.sh，再**始终尝试** yolo_x_stacklight_stop.sh；两者都成功才 STOP_ALL_OK。

CNN 停止路径使用 pgrep/pkill -f；默认 TERM_GRACE=30，进程退出后仍调用 VPU probe，策略允许时可重置。`AIBOX_STOP_FORCE=1` 的 force 路径使用 fuser -k /dev/video32/33，影响范围不等同精确 CNN PID。build 脚本不重置 VPU/Weston，不能推出运行脚本也不重置。

[Worker 进程辅助](../../scripts/siglip/worker_process.sh) 与 [siglip_stop.sh](../../scripts/siglip/siglip_stop.sh) 会校验 PID、启动时间、可执行文件与配置身份，控制锁忙/身份不明/超时非零退出；TERM 后需要 SIGLIP_SHUTDOWN_OK state=NULL。它与 CNN 的宽匹配停止机制应分开描述。

Worker 的 durable/本地提交/ACK 顺序见 [数据契约](data_contracts.md)；SIGLIP_READY 只代表 consumer/图就绪，SIGLIP_FIRST_INFERENCE_OK 才证明出现一次实际推理。

## 5. 已有板端证据

2026-11-25 的 165 板端从最简 pipeline 目录走统一启动和停止。12 路均就绪后观测超过 120 秒。停止后确认本次 supervisor、CNN 与四个 Scene Worker 退出，检查时板端与该 RTSP 源服务器的连接已断开。资源上限、反复启停和所有故障注入路径仍需各自的运行证据。[验收索引](../validation/README.md)
