# AI Box Stream Pipeline

本仓库维护 C++17/GStreamer 视频推理 Pipeline、项目插件、配置快照，以及独立的 SigLIP2 图片向量 Worker。外部微服务、云端和前端不属于本仓库的实现范围。

## 项目结构

| 路径 | 职责 |
|---|---|
| `main.cpp`、`CMakeLists.txt` | 主进程入口、主程序与 Worker 构建 |
| `src/`、`include/aibox/` | 配置、规划、构图、事件、媒体、控制及 Worker 实现 |
| `plugins/` | 独立 CMake 插件项目、共享 FrameMeta ABI 与后处理模块 |
| `config_snapshots/` | 流、模型、算法、输出及 Worker 配置 |
| `contracts/imsdk/1.0/` | Protobuf 消息契约 |
| `scripts/` | 板端构建、场景启动、统一停止与测试入口 |
| `tests/` | 核心回归、现役 Worker 测试依赖、任务与实验记录 |
| `docs/` | 架构、工程经验、架构决策及验收索引 |
| `data/` | 模型等资源；部分大文件按忽略规则不入库 |
| `output/` | 本地生成结果，不是源码或正式知识库 |

## 阅读与修改入口

- [文档总索引](docs/README.md)
- [当前架构与微服务集成边界](docs/architecture/README.md)
- [架构草稿核对报告：差异与待审核事项](docs/architecture/REVIEW.md)
- [测试入口与按功能归集任务资料的规范](tests/README.md)

构建入口为 `scripts/board_build_unified.sh`；场景启动及统一停止见 [运行生命周期](docs/architecture/runtime_lifecycle.md)。运行脚本可启动/终止进程、初始化图形环境，并按策略恢复 VPU，应先阅读当前实现。配置声明的相机上限和历史实验不代表当前板端容量验收。
