# 运行生命周期、进程与资源管理

> 代码核对基线：`b878219`；核对日期：2026-11-08。本文由外部架构草稿逐项对照本地源码后整理。
> `verified` = 当前源码/配置直接支持；`inferred` = 结构推断；`board-verified` = 当前版本有板端证据；`needs-confirmation` = 证据不足。本次只读代码，未新增 board-verified 结论。

## 1. 主进程（verified）

[main.cpp](../../main.cpp) 完成参数/信号、文件描述符限制、GStreamer 初始化、快照加载、规划/导出、构图、运行模式与服务装配。`--dump-plan-only` 在构图前退出；`--build-only` 构图/导出后退出；实际运行由 run-seconds/run-forever 控制。

正常运行路径启动检测发布、缓存/快照与请求服务，确认 Pipeline PLAYING 后启动配置要求的 AnnotatedRtspServer。标注服务失败输出 ANNOTATED_RTSP_DEGRADED 并收尾退出 25；成功先输出 ANNOTATED_RTSP_READY，再输出 PIPELINE_READY。READY 不是客户端已成功拉流，也不是全部业务依赖已通过。

## 2. Shell 启动链（verified）

[start_all.sh](../../scripts/start_all.sh) 用 lifecycle.lock 串行组合操作：Worker prepare-consumer→YOLO26 场景启动→Worker 启动。CNN 失败返回 20；Worker 失败返回 22，并明确保留已运行的 CNN，不是自动回滚整组。

场景包装调用 [webrtc_start_common.sh](../../scripts/webrtc_start_common.sh)：

1. 选择项目 runtime/vendor 插件库及图形 registry，设置环境。
2. `pgrep -f` 找旧 CNN，`pkill -TERM -f` 终止并限时等待；此处不是逐个校验 executable/starttime 的精确 PID 模型。
3. 检查本机 4222 监听、图形 gate、VPU gate；没有 weston backend 时会尝试 force-on；VPU 默认策略 if-needed，可能执行 reset_vpu。
4. nohup/stdbuf 启动，优先用 `pgrep -x aibox_stream_pi | head -1` 得到 PID，退回 launch_pid。
5. 最多约 90 秒等待本次日志 PIPELINE_READY，再最多约 80 秒累计 AGG 行数达到 AGG_NEED。

AGG 行数阈值不检查每个唯一 camera；READY_FOR_BROWSER 只是历史脚本标记，没有提供 HTML/8080/WebRTC 服务。AGG_TIMEOUT 分支 TERM pidfile 进程并限时等待；BOOT_TIMEOUT/BOOT_GFX_ERROR 等分支没有相同集中清理，不能把注释中的“精确 PID/无孤儿”推广到所有分支。详见 [待审核问题](REVIEW.md)。

## 3. 正常主进程收尾（verified）

控制 server 停止、移除主循环 watch/timer→标注 RTSP 服务 Stop→有录像时发送 EOS 并限时等待→batch teardown 准备→在 pads/probes 尚存时 SnapshotFrameCounters→设置 NULL 并最多等待 10 秒→停止图片/发布等服务并释放运行资源。

应以 main 的分支实现为准，错误提前退出路径不保证与正常路径完全一致。只有 `gst_element_get_state` 成功且 state==NULL 才打印 Pipeline reached NULL；超时/失败仅能报告不干净，不能据进程消失推断 VPU 已释放。

## 4. Shell 停止与 Worker 身份（verified）

[webrtc_stop.sh](../../scripts/webrtc_stop.sh) 先调用 siglip_stop.sh，再**始终尝试** yolo_x_stacklight_stop.sh；两者都成功才 STOP_ALL_OK。

CNN 停止路径使用 pgrep/pkill -f；默认 TERM_GRACE=30，进程退出后仍调用 VPU probe，策略允许时可重置。`AIBOX_STOP_FORCE=1` 的 force 路径使用 fuser -k /dev/video32/33，影响范围不等同精确 CNN PID。build 脚本不重置 VPU/Weston，不能推出运行脚本也不重置。

[Worker 进程辅助](../../scripts/siglip/worker_process.sh) 与 [siglip_stop.sh](../../scripts/siglip/siglip_stop.sh) 会校验 PID、启动时间、可执行文件与配置身份，控制锁忙/身份不明/超时非零退出；TERM 后需要 SIGLIP_SHUTDOWN_OK state=NULL。它与 CNN 的宽匹配停止机制应分开描述。

Worker 的 durable/本地提交/ACK 顺序见 [数据契约](data_contracts.md)；SIGLIP_READY 只代表 consumer/图就绪，SIGLIP_FIRST_INFERENCE_OK 才证明出现一次实际推理。

## 5. 改动检查与运行证据

新增 thread/task/probe/timer/request pad 时，逐项检查停止唤醒、join、释放、EOS/FLUSH、失败回滚与重复启动；来源包括 Builder、main 和各插件，而非仅脚本注释。本次没有执行服务操作或板端部署；重复启停、断流恢复、VPU 占用和强制恢复的当前板端结果均是 needs-confirmation。未来验证按 [validation 规范](../validation/README.md) 保存证据。
