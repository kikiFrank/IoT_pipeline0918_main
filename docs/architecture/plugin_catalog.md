# 自定义插件与共享 ABI 目录

> 源码核对日期 2026-11-25。基准为主工作区当前源码及配置。既有板端证据见 [验收索引](../validation/README.md)。

## 1. 清单与 factory/pads（verified）

| 项目路径（相对 plugins） | factory / pads | 当前边界 |
|---|---|---|
| `aiboxtimemux/` | aiboxtimemux；request `sink_%u`、always `src` | NV12 raw 多输入 latest 调度 |
| `aiboxstreamdemux/` | aiboxstreamdemux；always `sink`、request `src_%u` | NV12 raw 按 metadata 分流；每输出 task/queue |
| `aiboxcamsplit/` | aiboxcamsplit；always sink、request src_%u | GstBin；appsink/appsrc 分流路径 |
| `aiboxbatchgate/` | aiboxbatchgate；always sink/src、request video_%u | batch 准入、credit/inflight 与 partial/overload 计数 |
| `aiboxbatchsplit/` | aiboxbatchsplit；always sink/src、request video_%u | 拆批及各视频支路对接 |
| `aiboxflowtap/` | aiboxflowtap；always sink/src | 诊断流量/事件，不是必要推理阶段 |
| `aiboxframemeta_common/` | aiboxframemeta 注册插件 + libgstaiboxframemeta.so；无视频 pads/factory | 注册共享 GstMeta，供各插件链接 |
| `mlpostprocess_roi/` | qtimlpostprocess_roi；always sink/src | Tensor→按所选模块输出元数据/文本；实际 caps 由模块支持范围决定 |
| `qtifaceinfer/` | qtifaceinfer；always sink/src，BaseTransform | 人脸推理实现及独立配置，不是所有 Qualcomm 推理元素的统一接口 |

清单来自各项目 `gst_element_register`、pad template 与类定义。除下面两项深入核对外，其它项目完整的 caps、属性默认值、锁竞争和状态释放路径仍是 `needs-confirmation`，不能据本清单宣布全 ABI 审计完成。

## 2. aiboxtimemux（verified）

来源：[头文件](../../plugins/aiboxtimemux/gstaiboxtimemux.hpp)、[实现](../../plugins/aiboxtimemux/gstaiboxtimemux.cpp)。pads template 为 `video/x-raw,format=NV12`；运行协商还受 caps-mode/group-caps 限制。

| 属性 | 当前声明 / 初始化 |
|---|---|
| max-sinks | 范围 1..16，GParamSpec 默认 16，但 init 写入 4；需审核统一 |
| wait-timeout-ms | 范围 0..1000，默认/初始化 200 |
| drop-policy | 默认 latest；fifo 注册为枚举但 header 标 reserved，chain 无独立 FIFO 实现 |
| attach-meta | 默认 true |
| mux-group-id / algorithm-group-id | 默认空串，输出 metadata 标识 |
| silent | 默认 false |
| caps-mode | 默认 strict；另有 canonical_raw |
| group-caps | GstCaps boxed；init 为 null，canonical_raw 使用共享 raw caps |

属性在 class_init 中以 READWRITE 声明；这本身不证明任意 PLAYING 时动态变更都安全。READY→PAUSED 创建 scheduler GThread；PAUSED→READY 置 running=false、广播 cond、join，并清空 latest buffers。chain 覆盖旧 buffer；request/release、EOS/FLUSH 与 caps 路径共同管理 sink context。slot 分配是环形空闲槽搜索，header 的“Monotonic”注释与实现不一致。

统一 RTSP 的 `canonical_raw` 可变帧率输出与同组准入条件见 [当前拓扑](pipeline_topology.md)。PPE 1123 与 Light 1121 使用 `qtimlpostprocess_roi` 下的 `yolox-ppe-gesture1121` 和 `yolox-light1121` 模块，两目标由该插件工程构建。PPE 的 human/gesture 在后处理共用抑制组，白帽和工服与人员跨类别的归属由 Rust 规则在整帧处理。[模块构建](../../plugins/mlpostprocess_roi/CMakeLists.txt) · [PPE 后处理](../../plugins/mlpostprocess_roi/modules/object-detection/ml-postprocess-yolox-ppe.cc) · [PPE 匹配](../../../IoT_edge/aibox/crates/aibox-rule_engine/src/rule/ppe_compliance/association.rs)

## 3. aiboxstreamdemux（verified）

来源：[头文件](../../plugins/aiboxstreamdemux/gstaiboxstreamdemux.hpp)、[实现](../../plugins/aiboxstreamdemux/gstaiboxstreamdemux.cpp)。sink/src template 均为 NV12 raw。

| 属性 | 当前声明 / 初始化 |
|---|---|
| max-srcs | 范围 1..16，声明默认 16，init 为 4；需审核统一 |
| silent | false |
| output-fps | 0..120，默认 10；0 传递原 PTS；非零更新声明帧率与 PTS 步长，不是实际推理吞吐承诺 |
| queue-size | 1..32，默认 2，满时丢最旧 buffer |

每 request 输出有 camera-id 关联、sticky events、GstTask、lock/cond、buffer/event queue、drop/stall 计数；serialized events 与 buffers 共享每 src FIFO。停止/释放路径调用 task stop/join。异步 bounded fan-out 的设计目的不能表述为所有 flush/销毁路径绝对不会阻塞上游；板端并发证据尚缺。

## 4. FrameMeta 与构建依赖（verified）

唯一现役结构定义及 ownership 见 [数据契约](data_contracts.md)。timemux/camsplit/streamdemux/qtifaceinfer 的独立 CMake 通过 FindAiboxFrameMetaLib.cmake 解析 common 库。板端脚本先构建 common，再构建依赖插件。

脚本把 common/timemux/camsplit/streamdemux 的产物放到 staging 并生成摘要，再以四次 cp 切入 runtime_plugins；虽然输出文案包含 atomic switch，实际不是多文件事务式原子替换。部署一致性与默认值问题见 [审核报告](REVIEW.md)，本次未修改构建脚本。

后处理模块另以 `libml-postprocess-*.so` 加载，模块清单、标签、shape、dtype 与 QTI runtime ABI 必须共同匹配。运行设备 `gst-inspect` 输出及插件实际加载身份属于下一次板端验收证据。
