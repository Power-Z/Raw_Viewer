# 贡献指南

## 开始工作

1. 确保本地 `main` 与远端同步。
2. 从 `main` 创建短生命周期分支。
3. 修改代码、测试和文档。
4. 提交前运行项目检查。
5. 推送分支并创建 Pull Request。

详细命令和约定见 [`docs/process/github-workflow.md`](docs/process/github-workflow.md)。

## 分支命名

- `feature/<主题>`：新功能
- `fix/<主题>`：缺陷修复
- `docs/<主题>`：仅文档变更
- `chore/<主题>`：工程维护
- `spike/<主题>`：需要验证、可能被丢弃的技术实验

主题使用小写英文和连字符，例如 `feature/raw-file-open`。

## 提交信息

采用 Conventional Commits 风格：

```text
feat: support opening raw files
fix: correct 12-bit pixel unpacking
docs: clarify supported bayer patterns
test: add histogram boundary cases
chore: configure repository checks
```

一次提交只表达一个逻辑变更。不要提交样例 RAW 大文件、构建产物、访问令牌或用户隐私数据。

## Pull Request 要求

- 说明改动目的、验证方式和风险。
- 关联对应 Issue。
- 对 UI 变化附截图或录屏。
- 对 RAW 解析/算法变化说明样本来源、输入参数和期望结果。
- 更新相关需求、设计、测试和 `CHANGELOG.md`。
- 自动检查通过后再合并。

## 完成定义

一项工作只有满足以下条件才算完成：

- 验收标准全部满足；
- 新增或修改行为有相应测试；
- 没有引入已知的高优先级缺陷；
- 文档与实际行为一致；
- Pull Request 已审查并合并。
