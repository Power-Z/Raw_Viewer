# Raw Viewer

面向 Bayer RAW 图像的浏览、处理与统计软件。

## 当前阶段

项目处于 **V0.0：工程维护基线** 阶段。此阶段只建立可长期维护的协作流程和需求入口，不提前锁定 GUI 框架、图像算法库或文件格式实现。

## 下一步

请先填写 [`docs/requirements/business-and-data-requirements.md`](docs/requirements/business-and-data-requirements.md)。需求确认后，再基于真实业务场景完成：

1. 技术选型与分层架构设计；
2. 最小可用版本（MVP）范围拆分；
3. RAW 解码、显示、处理和统计的验证原型；
4. 自动化测试、构建与发布流程。

## 仓库结构

```text
.
├─ .github/                  GitHub 工作流、Issue 与 PR 模板
├─ docs/
│  ├─ architecture/         架构原则与后续架构设计
│  ├─ decisions/            架构决策记录（ADR）
│  ├─ process/              开发和维护流程
│  └─ requirements/         业务与数据需求
├─ require/                 原始版本需求
├─ CHANGELOG.md             版本变更记录
└─ CONTRIBUTING.md          贡献指南
```

## 协作方式

- 日常开发遵循 [`docs/process/github-workflow.md`](docs/process/github-workflow.md)。
- 首次上线 GitHub 前完成 [`docs/process/project-preparation-checklist.md`](docs/process/project-preparation-checklist.md)。
- 每个功能或缺陷使用独立分支和 Pull Request。
- 架构决定记录为 ADR，避免长期维护中丢失决策背景。
- 版本变化同步维护 `CHANGELOG.md`。

## 许可证

尚未选择许可证。在确定项目公开/私有策略和第三方依赖前，不应默认将代码作为开源项目发布。
