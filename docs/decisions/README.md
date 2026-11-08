# 架构决策记录（ADR）

本目录记录“为什么选择某种架构/技术方案”，当前代码结构见 [architecture](../architecture/README.md)，历史踩坑见 [engineering-notes](../engineering-notes/README.md)。本次只建立规则，未将从代码反推的理由伪造为已接受决策。

命名：`ADR-0001-short-title.md`、`ADR-0002-short-title.md`，编号递增；替代旧决策时保留旧记录并双向链接。

## 最小模板

```markdown
# ADR-0001: 决策标题

- Status: Proposed
- Date: YYYY-MM-DD

## Context
问题、限制、代码基线与已有证据。

## Decision
待审核或已明确接受的方案；说明生效范围。

## Alternatives
实际比较过的替代方案及不采用原因。

## Consequences
收益、代价、迁移、兼容性与维护影响。

## References
代码路径、测试/任务报告、相关 ADR、需要同步的外部契约版本。
```

Status 取 Proposed / Accepted / Superseded / Deprecated。未经明确决策保持 Proposed；发现源码与文档不符先记录事实，不能改业务代码来制造“符合 ADR”的结果。重要架构变更在相关 architecture 文档中链接 ADR，不复制全文。
