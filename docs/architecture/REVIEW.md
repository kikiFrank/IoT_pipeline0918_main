# 架构文档迁入历史记录与当期待审核事项

> 以下保留 2026-11-08 迁入时的核对结果和问题清单。当前统一 RTSP 进程所有权、启停和板端运行证据见 [运行生命周期](runtime_lifecycle.md) 与 [验收记录](../validation/README.md)。当前跨服务契约已对照同一工作区的 Rust 实现。[数据契约](data_contracts.md)

- 日期：2026-11-08
- 代码基线：`b878219`（分支 master）
- 输入：用户指定的 `PIPELINE_DOCS_BUILD_GUIDE.md` 及同目录已解压的 `docs/architecture/` 七篇草稿；本地未见配套 ZIP，直接读取已解压内容，不依赖 ZIP 名称。
- 范围：只调整文档信息架构。新建项目/文档索引、七篇架构文档、本报告及 decisions/validation 索引；没有移动源码或修改业务逻辑、模型、配置、构建/运行脚本和测试。

## 1. 已存在的修改与合并处理

执行前有八个未提交的 Markdown：工程知识库六篇、`tests/README.md`、`tests/EXPERIMENTS_ARCHIVE.md`，都是此前历史来源修正。这八个文件保持原内容。原工程知识库未被替换或迁移，tests 的任务目录规范保持有效。

目标 architecture、decisions、validation 与项目/文档总索引原先不存在，因此本次创建，没有同名已有文档需要覆盖合并。七篇草稿经源码核对后重写为中文文档；不原样迁入尚未核实的推断。

## 2. 七篇草稿与最终文档差异

| 文件 | 草稿中需要修正/补齐的内容 | 最终处理 |
|---|---|---|
| [README](README.md) | 基线仅写 master/日期，last_verified_commit 未填；微服务范围与证据状态偏笼统 | 写入代码 SHA、证据状态与本仓库边界，链接本报告 |
| [system_overview](system_overview.md) | Worker 图中包含 downstream transport，易读作当前已有下游上传；外部仓库名未核实 | 改为 local outbox；明确两个进程及外部服务端未核对 |
| [module_map](module_map.md) | Worker、插件、构建与测试职责偏名称推断；测试清单遗漏 raisehands | 按真实 CMake/源码/脚本整理职责，补齐手势与现存测试，区分注册插件和视频元素 |
| [pipeline_topology](pipeline_topology.md) | slot 被描述为单调分配；标注输出被描述为 H.264；配置到 Builder option 的映射待确认 | 改为环形空闲槽搜索、按相机 codec；补齐 main 映射、batch 限制、serial metadata/bypass 配对与诊断分支 |
| [plugin_catalog](plugin_catalog.md) | 主要列属性名，默认值、request pads、线程/ABI 未深入核对 | 补充 mux/demux 属性与默认值冲突、实际 task/thread 路径；其它插件完整 ABI 仍标 needs-confirmation |
| [runtime_lifecycle](runtime_lifecycle.md) | 对注释中的 exact-PID/no-orphan 描述范围过大，未说明宽匹配和部分启动失败路径 | 按实际命令记录 PID、超时、恢复策略，区分 CNN 与 Worker 的身份检查 |
| [data_contracts](data_contracts.md) | 多为“需检查哪些字段”的占位清单，尚无实际字段/主题/ACK 契约 | 写入 FrameMeta 字段/ownership、主题及载荷、Core NATS/JetStream 差异、outbox link+fsync 与提交后 ACK；模型实测矛盾保留待确认 |

## 3. 代码证据支持的修正（verified）

| 编号 | 原描述或易误解点 | 当前源码证据与文档结论 |
|---|---|---|
| D1 | timemux slot 单调增长且不复用 | [实现](../../plugins/aiboxtimemux/gstaiboxtimemux.cpp) `request_new_pad` 从约第 350 行按 max_sinks 取模寻找未占槽；已释放 ID 可以复用，存活 slot 不重复 |
| D2 | 标注输出固定 H.264 | [CodecProfile](../../include/aibox/common/CodecProfile.hpp) 第 17–24 行定义 H.264/H.265；[Builder](../../src/pipeline/GstPipelineBuilder.cpp) 约第 3091 行按 input.encoding 选 encoder/parser；[RTSP server](../../src/media/AnnotatedRtspServer.cpp) 选择对应 payloader |
| D3 | Builder 默认 false，因此是否启用标注仍未知 | [main.cpp](../../main.cpp) 约第 599–615 行显式打开 YOLO stages，按 annotated 配置开启标注，并拒绝 batch+annotated；接口默认值不等于 main 的实际选项 |
| D4 | FrameMeta common 与其它目录一样是视频处理元素 | [common 实现](../../plugins/aiboxframemeta_common/gstaiboxframemeta.cpp) 注册 GstMeta/plugin，没有视频 factory/pads；它提供共享 ABI 库 |
| D5 | Worker embedding 已有下游 transport | [worker.txt](../../config_snapshots/siglip_worker/worker.txt) output.mode=local_outbox；[SiglipWorker](../../src/siglip_worker/SiglipWorker.cpp) 提交本地结果后 ACK，没有据此证明云端消费 |
| D6 | FrameMeta 字段尚未读取；outbox 可能是 rename | [canonical header](../../plugins/aiboxframemeta_common/gstaiboxframemeta.h) 已给出字段；[EmbeddingOutbox](../../src/siglip_worker/EmbeddingOutbox.cpp) 第 15–45 行实际用 link 创建最终文件并 fsync，拒绝覆盖 |
| D7 | 存在 enum 就表示功能可用 | [控制实现](../../src/control/PipelineControlServer.cpp) 明确拒绝 RELOAD_MODEL；timemux fifo 只是已注册/保留枚举，chain 未实现独立 FIFO 队列 |

## 4. 需要用户审核的实现不一致或行为边界

下列事项仅记录事实和待决策问题，本次未修改对应代码。不能将它们写成已修复。

| 编号 / 状态 | 证据与影响 | 供审核的问题 |
|---|---|---|
| C1 `verified`：插件默认值内部不一致 | [timemux cpp](../../plugins/aiboxtimemux/gstaiboxtimemux.cpp) 第 127 行的 GParamSpec 默认 16，init 约第 195 行为 4；[demux cpp](../../plugins/aiboxstreamdemux/gstaiboxstreamdemux.cpp) 第 113/149 行同样 16/4。Builder 显式设置容量，不能由该差异直接推出当前样例运行错误 | 后续统一为 4 还是 16？同时明确未显式配置的独立插件用法 |
| C2 `verified`：timemux header 注释滞后 | [header](../../plugins/aiboxtimemux/gstaiboxtimemux.hpp) 约第 98 行仍称 Monotonic allocator，实际实现见 D1 | 是否后续只修正注释？不应为了匹配注释改变当前 slot 算法 |
| C3 `verified`：CNN PID 管理没有全程精确身份校验 | [start common](../../scripts/webrtc_start_common.sh) 第 67–71 行 pgrep/pkill -f，约第 108 行按短进程名取首个；[CNN stop](../../scripts/yolo_x_stacklight_stop.sh) 第 67 行宽匹配 TERM，force 时第 80–81 行 fuser -k。Worker 则有 executable/starttime/config 身份检查 | 是否接受当前单实例假设？若需多副本/并行测试，应另立修复任务设计 CNN 身份与作用域 |
| C4 `verified`：并非所有启动失败都会清理新进程 | start common 的 BOOT_GFX_ERROR/BOOT_TIMEOUT 约第 129/133 行直接退出；AGG_TIMEOUT 约第 148 行才有 TERM+等待处理 | 是否要求所有启动失败路径统一清理已启动进程？板端需验证退出后是否残留，当前只确认代码分支差异 |
| C5 `verified`：READY_FOR_BROWSER 不代表浏览器链验收 | start common 约第 138–142 行按 AGG **总行数** 达阈值后输出标记，并非每路唯一相机覆盖，也没有 HTML/8080 服务 | 后续发布门禁是否应要求每相机进展及客户端实际取流？当前文档不提升该标记含义 |
| C6 `verified`：运行脚本可能恢复图形/VPU | start common 第 60 行附近默认 if-needed、无 Weston 时 auto-start；[vpu_health](../../scripts/vpu_health.sh) 可 sudo 调 reset_vpu；CNN stop 进程退出后亦会检查并恢复。只有 build 脚本声明不重置 | 这是当前操作策略，是否继续保留需由现场维护要求决定；不能把启动/停止解释成无设备副作用 |
| C7 `verified`：ABI quartet 的切换不是多文件原子事务 | [board_build_unified.sh](../../scripts/board_build_unified.sh) 第 125 行打印 atomic switch，第 126–129 行实际逐个 cp 四个 .so；staging/摘要并不提供事务原子性 | 若需允许在线构建/部署，是否另行设计停机或版本目录切换？本次仅修正文档描述 |
| C8 `needs-confirmation`：PPE 输出 dtype 资料不一致 | [模型 descriptor](../../config_snapshots/ppe_4cam/models/yolox_ppe_w8a16.txt) output.dtype=uint16，而[历史工程记录](../engineering-notes/01_models_preprocess_qnn.md) 记录 QNN 下游 FLOAT32 [1,8400,8]。后处理兼容多种 Tensor 不足以判定当前运行边界 | 请用当前 BIN、SDK、插件与配置采集真实 Tensor dtype/shape/量化参数，确认是声明滞后、转换边界不同或版本不同；暂不改配置/历史数值 |

## 5. 证据不足，需后续补充（needs-confirmation）

1. **外部微服务架构来源**：草稿提到 `IoT_edge`，但本次没有提供/读取其源码与架构文件。文档只描述本仓库已实现的 NATS/RPC 边界，不确认外部服务职责划分、Rust 契约版本、消息实际投递拓扑或 UI/云端验收。若要逐服务对齐，需要对应仓库/arch 路径与版本。
2. **当前板端拓扑和并发**：没有新运行的 DOT、caps、模型 Tensor 或重复启停证据。scheduler、GstTask、FIFO/flush、零拷贝等源码事实不替代并发时序与硬件 buffer 行为验收。
3. **模型资产**：配置的模型 hash/HTP ID 与历史 scale/offset 不代表本次核验了当前 BIN、模型精度或 SoC/SDK 兼容。正式记录按 [validation 规则](../validation/README.md) 补齐。
4. **其它插件完整 ABI**：本次核对 factory/pads 与关键模块，未对 batchgate/batchsplit/camsplit/qtifaceinfer/ROI 插件每个属性、caps 和锁释放分支做完整形式化审计；插件目录已限制结论范围。

本次不将任何新描述标为 board-verified。历史 tests 清理、已有 Host 测试和旧板端实验仍沿用原证据边界。

## 6. 交付与验证

新增结构：

```text
README.md
docs/
  README.md
  architecture/
    README.md
    system_overview.md
    module_map.md
    pipeline_topology.md
    plugin_catalog.md
    runtime_lifecycle.md
    data_contracts.md
    REVIEW.md
  engineering-notes/       # 保留现有六篇及未提交修改
  decisions/README.md
  validation/README.md
```

验收范围为文件/目录存在性、当前入口路径、Markdown 相对链接、原有文档内容保持、Git diff 格式与修改范围。未运行构建、单测、部署、服务启停、VPU/Weston 操作；这次没有修改可执行代码，也没有当前硬件验收条件。

建议提交信息：`docs: introduce source-verified pipeline architecture and review findings`。本次未提交或推送，先供用户审核。
