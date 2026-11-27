# 测试索引

这里维护可重复执行的核心回归和 SigLIP2 告警图片 Worker 的板端验证工具。已退役测试的用途与判据见 [实验逻辑归档](EXPERIMENTS_ARCHIVE.md)。

## CTest 回归

| 源文件 | CTest | 覆盖 |
| --- | --- | --- |
| `unit/frame_cache_policy_test.cpp` | `frame_cache_policy_test` | 帧采样、容量、精确与最近帧匹配 |
| `unit/snapshot_service_test.cpp` | `snapshot_service_test` | 告警抓拍复用、回调和停止竞态 |
| `unit/pipeline_control_test.cpp` | `pipeline_control_test` | 模拟 GStreamer 管线与 NATS 控制 RPC。通过 `AIBOX_TEST_NATS_URL` 指定隔离的 NATS。未指定地址或连接失败时，CTest 标记跳过 |
| `postprocess/ppe_gesture_postprocess_test.cpp` | `ppe_gesture_postprocess_test` | 使用统一 PPE 标签和合成张量，检查逐人互斥及帽子、工服保留 |
| `postprocess/light_postprocess_test.cpp` | `light_postprocess_test` | 使用当前四分类 Light 模块和颜色标签，覆盖类别顺序、颜色、阈值、同类 NMS、FLOAT32 和 UINT16 |
| `siglip_worker/embedding_outbox_test.cpp` | `siglip_worker_core_test` | 告警图片 Worker 的向量 JSON、PTS、畸形 JPEG 与 outbox 幂等 |

在具备项目依赖的 Linux 开发树根目录执行。下例中的 NATS 地址应指向专用于测试的实例。

```bash
cmake -S . -B build-tests -DBUILD_TESTING=ON
cmake --build build-tests -j3
AIBOX_TEST_NATS_URL=nats://127.0.0.1:45223 ctest --test-dir build-tests --output-on-failure
ctest --test-dir build-tests --output-on-failure -R '^(ppe_gesture|light)_postprocess_test$'
```

这些测试覆盖代码契约。统一 12 路的 RTSP、4+4 分组和实时场景评分仍通过统一启停入口做板端验收。

## SigLIP2 告警图片 Worker 板端工具

| 源文件 | 入口或用途 |
| --- | --- |
| `siglip_worker/prepare_image_probe.cpp` | 构建为 `siglip_preprocess_probe`，导出实际预处理结果 |
| `siglip_worker/verify_image_preprocess.py` | 对比五张参考图的预处理像素、原生 UINT16 张量和 QNN 向量 |
| `siglip_worker/nats_worker_test_harness.cpp` | 构建为 `siglip_isolated_harness`，为隔离测试提供 JetStream、模拟 RPC 和状态检查 |
| `siglip_worker/test_jetstream_redelivery.sh` | 使用指定 JPEG 和临时 JetStream，覆盖重投、幂等、durable 和写盘失败恢复 |
| `siglip_worker/benchmark_image_encoder.cpp` | 构建为 `siglip_hardware_benchmark`，传入 Worker 配置和 JPEG 测图像编码延迟 |

```bash
bash scripts/siglip/run_board_tests.sh /path/to/reference_dataset /path/to/sample.jpg
```

数值对照需要包含 `data/manifest.jsonl`、源图、预处理原始 RGB 数据和 `results/board_001/image/Result_*/image_embeds.raw` 的参考目录。硬件测试需要 QTI/GStreamer 插件、QNN runtime 和图像编码模型。板端入口构建 `build-review-siglip`，日志写入 `output/siglip_worker`。隔离 JetStream 默认使用本地端口 45223，可通过 `SIGLIP_TEST_NATS_PORT` 修改。

## 已归档的测试

已移除旧 Color8、旧 PPE/Stacklight 和独立 Gesture 的 CTest 注册与源码。Light 回归保留了合成检测、置信度和 NMS 等可复用逻辑，现役 PPE／Gesture 继续使用独立回归。历史用例与实验判据见 [实验逻辑归档](EXPERIMENTS_ARCHIVE.md)，历史源码可从 Git 查阅。

## 后续需求与 Bug 修复的任务目录规范

**后续每新增一个功能或修复一个 Bug，都必须在 `tests/` 下建立对应的独立功能目录；当次任务使用和生成的测试、复现、诊断、验收脚本，以及日志、报告和结果，统一收纳到该目录。** 不再把临时文件散放在 `tests/` 根目录，也不要混进其它功能目录。

目录使用能说明功能或问题的名称，例如 `rtsp_reconnect/`、`alarm_image_binding/`。同一功能的后续任务继续使用该功能目录，以“日期 + 任务简名”区分每次任务；同次任务的多轮实验按运行批次分开，避免覆盖先前结果。

示例结构（仅为命名和组织示例，不代表已经执行这些测试）：

```text
tests/
  rtsp_reconnect/                       # 一个功能 / 问题一个目录
    README.md                          # 功能说明、任务索引、长期回归入口
    regression/                        # 经确认需要长期维护的回归代码
    20261108_fix_reconnect/             # 当次需求或 Bug 修复任务
      README.md                        # 任务目标、复现条件、脚本索引与运行方法
      scripts/                         # 本次所有测试 / 复现 / 诊断 / 验收脚本
      fixtures/                        # 本次必要的输入样本、测试配置
      runs/
        01_before_fix/                 # 修复前的复现轮次
          logs/                        # 命令输出、标准错误、运行日志
          results/                     # 指标、JSON、截图等实验结果
        02_after_fix/                  # 修复后的验证轮次
          logs/
          results/
      REPORT.md                        # 本次结论、修复前后对照、遗留问题
```

按实际需要创建子目录，不预建空目录或无内容的占位文件。已有核心测试仍按本索引维护；新增任务采用上述结构。

### 当次任务必须记录什么

- **任务 README**：需求或 Bug 描述、复现条件、预期行为、环境与版本、模型/配置等必要输入；逐个说明脚本的用途、参数、执行顺序、前置条件、输出位置及通过/失败判据。
- **完整执行记录**：将本次实际运行的脚本和每轮日志、报告、结果放入任务目录；包含失败轮次、修复前后对照和中间排查记录，不只留下最后一次成功输出。调用已有公共脚本时，在 README 中索引其路径和实际命令，不复制一份公共脚本。
- **任务报告**：说明测试逻辑、关键代码或断言、实际实验条件、观察结果、问题原因及修复依据；逐项标明通过、失败、未执行或未闭环，并链接对应运行批次和证据。不能把“脚本执行结束”当成验收通过。
- **产物去向**：新脚本优先支持指定输出目录，将输出写入对应 `runs/<批次>/`。现有程序必须在 runtime/output 等固定位置生成文件时，将与本任务有关的日志和结果收集到任务目录，并记录来源路径和采集时间。

### 任务完成后的归档与清理

执行期间先在任务目录内收齐脚本、日志、报告和结果，便于复盘；任务完成后再整理，遵循本项目“保留知识与必要回归、删除无用实验脚本”的原则：

1. 将有价值的测试思想、关键逻辑、实验条件、结果和踩坑经验整理进本任务 `REPORT.md`；跨任务可复用的结论再提炼到 `docs/engineering-notes/` 对应专题，并互相链接。
2. 确需长期运行的回归代码放入功能目录的 `regression/`，在功能 README 中索引；需要时接入 CMake/CTest，确保输入、调用路径和依赖完整。
3. 一次性部署包装、固定现场路径的探针、重复或无维护价值的脚本，在关键逻辑已写入报告后删除；完成归档后清理无保留价值的重复日志和生成产物。报告必须说明最后保留的证据与已清理内容，不能留下指向已删除文件的有效证据链接。
4. 更新功能 README 的任务索引；新增长期回归入口时同步更新本文件。最终目录保留清楚的任务记录和必要代码，不长期堆积每次实验的全部临时产物。

**本地归集与 Git 跟踪需要分别检查。** 当前项目的 `*.log` 规则会忽略新日志；需要随仓库提交的任务日志，须明确加入 Git（例如 `git add -f -- tests/<功能>/<任务>/runs/<批次>/logs/<文件>.log`），并检查暂存清单。不能因为文件已经放进任务目录，就认为它已同步到 GitHub。
