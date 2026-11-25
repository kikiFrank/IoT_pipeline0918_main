# 正式验证与板端验收索引

本目录记录明确版本与环境的测试矩阵和验收摘要。[工程知识库](../engineering-notes/README.md) 保留不同版本的历史结果。

## 2026-11-25 · 165 板端统一 RTSP 与 PPE 上报

验收从 `/mnt/workspace/1125test_simplify/ai-box_stream_pipeline` 运行 `bash scripts/start_rtsp.sh`。启动日志给出的 run 为 `output/unified_rtsp/run-a59wc6ar`。此次使用板端最简 TXT 快照入口，在全部就绪后观察超过 120 秒。运行和停止证据属于当次板端版本。

| 范围 | 当次证据 |
|---|---|
| 推理链进展 | 8 路 CNN 产生逐帧事件，4 路 SigLIP2 产生分数。观测结束时 supervisor 状态为 `ready` |
| PPE 业务事件 | 四路共 49 条规则事件，逐条对应 `alarm.event`、SQLite 告警行和 MQTT 客户端日志 |
| 推理帧关联 | 另一个采样窗口有 23 条 PPE 事件，人员检测 ID、类别和框均匹配对应源帧 |
| 本地图片、远端图片 | 40 条关联为 `ready`，9 条为 `ready_near`。当次图片上传状态尚为 `pending`，MQTT 客户端日志记录与远端图片回执分别核验 |
| 停止 | 执行 `bash scripts/stop_rtsp.sh` 后，本次 supervisor 和五个工作进程退出，检查时已无到 RTSP 源服务器的连接 |

规则验收未计算逐人识别精度。本次文档核对使用已有日志摘要。各条告警和上传证据口径见 [微服务端到端盘点](../../../IoT_edge/aibox/arch_docs/Detection_to_MediaUpload_E2E_Completeness.md)。

## 任务资料与正式摘要

当次脚本、每轮日志、原始结果和报告放在 `tests/<功能>/<日期_任务>/`，遵循 [任务目录规范](../../tests/README.md)。正式验证记录在这里概括范围、指标和结论，使用相对链接指向对应任务证据，不复制脚本/日志/截图或模型 BIN。

命名建议：`YYYYMMDD-feature-board.md`。不同运行批次分别标记，失败与未执行项也须保留，不以最后一次成功掩盖失败轮次。

## 最小记录模板

```markdown
# 日期 / 功能 / 验收场景

- Status: needs-confirmation
- 场景/功能与判据：
- 板卡/SoC、系统、驱动、SDK、GStreamer、插件身份：
- Git commit 及工作区差异：
- 配置快照、include、CLI 与环境：
- 模型文件 hash、输入输出契约：
- 测试命令、时间窗与每轮批次：
- 任务报告与原始证据链接：

## 测试矩阵、结果与指标
逐项注明通过 / 失败 / 未执行，采样数量、单位、测点和统计口径。

## 未覆盖范围与已知限制
拓扑、相机、负载、精度、资源、协议及跨服务边界。

## 结论与后续项
只有证据覆盖的板端范围才标 board-verified。
```

配置的 max_streams、历史 16 路或 Host CTest 不构成当前 HTP/VPU 容量验收。仅 plan 导出、进程存活、READY、一次 ffprobe 或 outbox 文件存在，也不能各自替代连续视频、精度和跨服务业务链的验证。
