# Raw Viewer

面向 Bayer RAW 图像的浏览、处理与统计软件。

## 当前阶段

项目处于 **V0.1：架构与实施方案** 阶段。已基于当前软件需求形成推荐技术基线、分层架构、超大图方案和三阶段实施计划，尚未开始 GUI 功能编码。

## 下一步

请先审阅 [`docs/architecture/v0.1-architecture.md`](docs/architecture/v0.1-architecture.md) 中的待确认问题，然后按 [`docs/plans/v0.1-three-stage-implementation.md`](docs/plans/v0.1-three-stage-implementation.md) 进入 V0.2：

1. 建立 C++20、Qt 6 Widgets 和 CMake 工程骨架；
2. 完成三栏 UI、文件浏览和普通图片打开；
3. 完成 byte-aligned 8/16/32-bit 平面 RAW 与 LibRaw 相机 RAW 基础解码；
4. 完成缩放、平移、状态栏和后台任务闭环。

## 仓库结构

```text
.
├─ .github/                  GitHub 工作流、Issue 与 PR 模板
├─ docs/
│  ├─ architecture/         架构原则与后续架构设计
│  ├─ decisions/            架构决策记录（ADR）
│  ├─ plans/                分阶段实施方案
│  ├─ process/              开发和维护流程
│  ├─ records/              状态、追踪、风险、样本与发布记录
│  └─ requirements/         业务与数据需求
├─ require/                 原始版本需求
├─ AGENTS.md                自动化代理与开发约束
├─ CHANGELOG.md             版本变更记录
└─ CONTRIBUTING.md          贡献指南
```

## 协作方式

- 日常开发遵循 [`docs/process/github-workflow.md`](docs/process/github-workflow.md)。
- 首次上线 GitHub 前完成 [`docs/process/project-preparation-checklist.md`](docs/process/project-preparation-checklist.md)。
- 当前状态和待确认项见 [`docs/records/project-status.md`](docs/records/project-status.md)。
- 所有实现必须更新 [`docs/records/requirements-traceability.md`](docs/records/requirements-traceability.md)。
- 每个功能或缺陷使用独立分支和 Pull Request。
- 架构决定记录为 ADR，避免长期维护中丢失决策背景。
- 版本变化同步维护 `CHANGELOG.md`。

## 许可证

尚未选择许可证。在确定项目公开/私有策略和第三方依赖前，不应默认将代码作为开源项目发布。
