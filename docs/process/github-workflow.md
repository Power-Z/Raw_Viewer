# GitHub 长期维护流程

## 1. 推荐模型

使用轻量 GitHub Flow：

```text
同步 main → 创建 Issue → 创建短分支 → 提交 → 推送 → Pull Request → 检查/审查 → 合并 → 删除分支
```

`main` 始终保持可构建、可测试。不要直接在 `main` 上开发。

## 2. 仓库首次配置

### 2.1 本地身份

由仓库所有者配置真实提交身份：

```powershell
git config user.name "你的姓名或 GitHub 用户名"
git config user.email "你的 GitHub noreply 邮箱"
```

推荐使用 GitHub 提供的 `ID+username@users.noreply.github.com` 地址，避免暴露私人邮箱。

### 2.2 连接远端

先在 GitHub 创建空仓库，不要自动生成 README、许可证或 `.gitignore`，然后执行：

```powershell
git remote add origin https://github.com/<owner>/<repository>.git
git push -u origin main
```

如果使用 SSH，应先配置并验证 SSH key，再将远端地址换为 SSH 地址。访问令牌、私钥不得写入仓库。

### 2.3 GitHub 仓库设置

对 `main` 配置分支保护或 Ruleset：

- 禁止强制推送和删除；
- 合并前必须通过 Pull Request；
- 必须通过 `repository-quality` 检查；
- 合并前分支必须与 `main` 保持最新；
- 至少 1 次审查（只有一名维护者时可暂缓，但仍使用 PR 留痕）；
- 推荐 Squash merge，合并后自动删除分支。

为仓库启用：

- Issues；
- Actions；
- Dependabot alerts 与 secret scanning（可用时）；
- 私有项目确认数据访问成员，公开项目前先完成许可证与样本授权审查。

## 3. 每次任务的标准操作

### 3.1 开始任务

```powershell
git switch main
git pull --ff-only origin main
git switch -c feature/<topic>
```

开始前先创建 Issue，写清目的、范围和验收标准。

### 3.2 提交变更

```powershell
git status
git diff
git add <明确的文件路径>
git diff --cached
git commit -m "feat: concise change description"
```

避免使用不经检查的全量暂存。RAW 样本、密钥、日志和构建产物不得进入提交。

### 3.3 推送与创建 PR

```powershell
git push -u origin feature/<topic>
```

在 Pull Request 中完整填写模板。检查通过且审查意见解决后，Squash 合并。

### 3.4 合并后清理

```powershell
git switch main
git pull --ff-only origin main
git branch -d feature/<topic>
```

## 4. Codex 协作约定

后续可以直接要求 Codex：

- “同步远端并基于 Issue #N 开始开发”；
- “检查当前修改，运行测试并提交”；
- “推送当前分支并准备 PR 描述”；
- “审查 PR 的失败检查并修复”。

Codex 在执行 Git 操作前应：

1. 检查当前分支、工作区改动和远端状态；
2. 保留用户已有且与任务无关的修改；
3. 拉取使用 `--ff-only`，不擅自重写历史；
4. 提交前展示或检查 diff，并运行相关测试；
5. 不把令牌、私钥或受限 RAW 数据写入仓库；
6. 对强制推送、删除远端分支、回滚已发布版本等破坏性操作单独确认。

## 5. 版本与发布

- 开发阶段版本从 `0.x.y` 开始；
- `CHANGELOG.md` 的变更先写入 `Unreleased`；
- 发布时确认测试、样本授权、安装包签名和变更说明；
- 创建带注释标签，例如 `v0.1.0`；
- 构建产物由 GitHub Release 或受控制品库保存，不提交到 Git。

首个可用版本前建议的里程碑：

| 里程碑 | 交付内容 |
|---|---|
| V0.0 | 仓库、流程、需求模板 |
| V0.1 | 需求基线、架构、关键技术验证 |
| V0.2 | 单文件 RAW 浏览 MVP |
| V0.3 | 基础处理与统计 |
| V1.0 | 满足正式发布质量门槛 |

## 6. 备份与恢复

- GitHub 不是大型原始数据的备份系统；
- 源码至少保留 GitHub 远端和一个定期验证的本地/组织备份；
- 测试 RAW 数据使用独立受控存储，并维护文件校验和与授权记录；
- 每季度抽查一次克隆、构建和样本恢复流程。
