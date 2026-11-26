# Face 依赖与统一 Pipeline 复接手册

2026 年 11 月 26 日。基于本地主工作区的代码和配置核对。统一配置静态校验识别到 12 路摄像头。板端 Face 混跑待实测。

## 当前状态

- [统一配置](../../config/unified_rtsp.json) 由 [统一 supervisor](../../tools/rtsp_supervisor.py) 读取，启动时生成 CNN 的 TXT 配置和各 SigLIP2 场景的 JSON。当前统一配置未启用 Face，也没有生成 SCRFD 模型 include 的逻辑。仅在统一 JSON 增加 `[face]` 对应的设置无法启用完整链路。
- 旧 Face Demo 同时使用 YOLO26 和 SCRFD，完整目录可以在备份提交 `5e40fe1` 查阅。当前保留的 SCRFD BIN 可以独立用于未来复接 PPE 或 Light。
- 当前 SCRFD 功能是人脸检测和白色遮罩。ArcFace 源码随插件编译，现有 `arcface_model` 为空。Face 元数据供 GStreamer 内部绘制使用，现有检测事件和 Rule Engine 可以沿用既有接口。

## 文件和依赖

| 位置 | 职责与依赖 |
| --- | --- |
| [`include/aibox/common/ConfigTypes.hpp`](../../include/aibox/common/ConfigTypes.hpp)、[`src/config/TxtSnapshotLoader.cpp`](../../src/config/TxtSnapshotLoader.cpp) | 定义并解析可选 `[face]`。启用状态和 `model_id` 决定是否生成 Face 计划。 |
| [`include/aibox/orchestrator/Plan.hpp`](../../include/aibox/orchestrator/Plan.hpp)、[`src/orchestrator/PipelineOrchestrator.cpp`](../../src/orchestrator/PipelineOrchestrator.cpp)、[`main.cpp`](../../main.cpp) | `FacePlan` 从 `[models]` 中匹配 SCRFD 条目。主程序以 CNN 可执行文件目录为基准解析模型和插件配置路径。 |
| [`src/pipeline/GstPipelineBuilder.cpp`](../../src/pipeline/GstPipelineBuilder.cpp) | 非 batch 的分组链路为每个 TimeMux 组插入一套 Face 元素。插件接收 `config-path`、`backend-device-id` 和 `pool-size`。这里也含共享的物体 ROI 处理和诊断代码。 |
| [`src/pipeline/FaceOcOverlay.cpp`](../../src/pipeline/FaceOcOverlay.cpp)、[`include/aibox/pipeline/FaceOcOverlay.hpp`](../../include/aibox/pipeline/FaceOcOverlay.hpp)、[`include/aibox/face/`](../../include/aibox/face/) | `overlaycomposition` 消费 `GstFaceDetectionMeta` 绘制白块。Face 头文件保存检测元数据、结果状态 ABI 和框处理逻辑。主程序动态查找元数据类型，源码 ABI 需与插件一致。 |
| [`plugins/qtifaceinfer/`](../../plugins/qtifaceinfer/) | 单独的 CMake 工程，生成 `libgstqtifaceinfer.so`。`gst_plugin/` 注册元素和元数据，`async/` 管理分摄像头的异步队列和结果，`FaceProcessor.cpp` 串联 OpenCL 预处理、QNN HTP 推理和 SCRFD 解码。 |
| [`plugins/qtifaceinfer/face_preprocess/kernels/`](../../plugins/qtifaceinfer/face_preprocess/kernels/) | 插件运行时读取两个 OpenCL 文件。现有预处理初始化会编译 `scrfd_preprocess.cl` 和 `arcface_preprocess.cl`，即使 ArcFace 模型为空也需要两份文件。 |
| [`plugins/aiboxframemeta_common/`](../../plugins/aiboxframemeta_common/)、[`scripts/board_build_unified.sh`](../../scripts/board_build_unified.sh)、[`CMakeLists.txt`](../../CMakeLists.txt) | Face 插件链接 `libgstaiboxframemeta.so`，共用 FrameMeta 与 TimeMux、demux 等插件。板端脚本先构建 common，再构建 Face 并执行 `gst-inspect-1.0 qtifaceinfer`。主工程编译 `FaceOcOverlay.cpp`。 |
| [SCRFD BIN](../../data/models/face/det_2.5g_qcs9075.bin)、[模型条目](../../config_snapshots/face/scrfd_face.txt)、[插件配置](../../config_snapshots/face/face_config.json) | 模型 ID 为 `scrfd_2_5g`，工厂名为 `qtifaceinfer`。Builder 把 `config_path` 传给插件。实际加载的 BIN 由 JSON 的 `scrfd_model` 指定，实际阈值也取 JSON，当前为 `conf_threshold=0.75`、`nms_threshold=0.4`、`max_faces=32`。 |
| [公共启动脚本](../../scripts/webrtc_start_common.sh) | 当前 PPE 1123 和 Light 1121 独立脚本共用。旧 YOLO26 启动链可在备份提交 `5e40fe1` 查阅。 |

插件侧的构建和运行条件见 [Face 插件 CMake](../../plugins/qtifaceinfer/CMakeLists.txt) 与 [插件配置加载器](../../plugins/qtifaceinfer/config/FaceConfigLoader.hpp)。板端要核对 GStreamer 的 video、base、allocators 和 JSON-GLib，QNN 头文件及 runtime，动态加载的 `libOpenCL.so`，以及 `/dev/dma_heap/qcom,system` 的访问权限。现有 Face JSON 指向 `/usr/lib/libQnnHtp.so` 和 `/usr/lib/libQnnSystem.so`，复接时以目标板实际路径为准。协商到 NV12 后，还要确认送给插件的 buffer 实际满足单 FD DMA-BUF 布局。

## 如何加回统一 Pipeline

1. **恢复 Face 专用文件**。若已清理，从备份分支 `dev_20261125_simplify_before` 的 `5e40fe1` 取回 Face 插件、overlay、元数据头文件、SCRFD BIN、旧模型条目和插件配置。共享的 Builder、主程序、CMake 和构建脚本按文件差异合并 Face 相关段落，保留当前 PPE、Light 和告警截图 Zone 改动。模型、插件 JSON 和 kernel 目录可以迁到干净的位置，再重新设置路径。
2. **补统一 supervisor 的可选配置**。建议新增 `cnn.face`，包含 `enabled`、`model_id`、`element`、`model_path`、`config_path`。默认关闭。启用时静态校验字段、CNN 摄像头和执行模式，启动 preflight 检查 Face 文件及插件。渲染时输出 `[face]` 和独立的 SCRFD 模型 TXT，并把该 TXT 纳入 CNN 的 `[models]` include。沿用 PPE／Light 的 stage 配置，SigLIP2 继续使用各自的场景入口。
3. **对齐三处路径基准**。运行 TXT 的 `include` 相对生成的 `pipeline.txt`。模型条目的 `model_path` 和 `config_path` 相对 CNN 可执行文件目录，或者渲染成绝对路径。插件 JSON 内的 `scrfd_model`、`opencl_kernel_path` 相对 JSON 文件目录。移动 JSON 时同步更新这两个值，并保留现有的简单多行 JSON 格式。
4. **在原 CNN 构图启用 Face**。沿用下面的插入位置，每个 TimeMux 组一套插件。当前 `FacePlan` 是全局开关，启用后覆盖该 CNN 进程的各组。只给部分摄像头启用时，需要另行设计分组或按摄像头的启用策略。当前多 HTP 时 Face 按组使用 `(组序号 + 1) % 2`，还要实测与 PPE、Light、SigLIP2 混跑时的资源分配。

```text
RTSP 解码 → TimeMux → PPE 或 Light stage → 最后一个 metamux
  → q_face_in → qtifaceinfer → q_face_out → qtivoverlay
  → overlaycomposition → tee_post → 每路 demux、编码和截图相关支路
```

5. **区分推理和遮罩开关**。现有 `FaceOcEnabled()` 默认开启，因此 Face 模型未启用时也可能建出 `overlaycomposition`。复接建议让正常 Face 遮罩与 `face_plan.enabled` 显式关联，把 `AIBOX_FACE_OC=static` 留作诊断。现有 `AIBOX_FACE_OC=off` 只关白块绘制，`AIBOX_FLOWTAP` 的诊断分支也会绕过该绘制路径。预期看见白块时应检查这些环境变量。
6. **板端验收从一路开始**。用单独的统一配置选一条含人脸的 CNN 摄像头，先执行 `bash scripts/start_all/start_rtsp.sh --check --config <配置>`，再编译并确认 `gst-inspect-1.0 qtifaceinfer` 和 `overlaycomposition` 正常。运行用 `bash scripts/start_all/start_rtsp.sh --config <配置>`，结束用同配置的 `bash scripts/start_all/stop_rtsp.sh --config <配置>`。检查 Face 初始化、实际 HTP、结果的摄像头关联、无脸时白块清除、标注 RTSP/WebRTC 和告警截图，再扩大到 12 路并对比帧率、推理进度和掉帧。是否需要遮罩出现在告警截图要以落盘 JPEG 实测确认。插件异步取近期结果，现有行为允许无有效结果时继续输出帧，不能据此承诺逐帧匿名化。
