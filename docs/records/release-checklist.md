# 发布检查表

每次发布复制一个版本小节并保留结果，不要只反复勾选同一组复选框。

## 版本：Vx.y.z

- 发布负责人：待填写
- commit/tag：待填写
- 日期：YYYY-MM-DD

### 范围与质量

- [ ] 需求追踪矩阵中本版本项目均为 Verified 或有批准的 Deferred
- [ ] 所有自动检查通过
- [ ] 正常、边界、错误和取消路径通过
- [ ] 固定性能设备和样本达到门槛
- [ ] 没有未接受的高影响风险
- [ ] 技术债务已登记并有偿还版本

### 数据与安全

- [ ] 提交和制品不含令牌、密钥、隐私数据或未授权 RAW
- [ ] 样本清单的 SHA-256、参数和授权完整
- [ ] 依赖漏洞和许可证扫描已复核
- [ ] 日志和崩溃信息经过脱敏检查

### 构建与制品

- [ ] 从干净 clone 可重复配置、构建和测试
- [ ] 编译器、Qt、CMake 和依赖版本已记录
- [ ] 安装、升级、卸载通过
- [ ] CPU 回退路径通过
- [ ] 安装包已签名（正式发布需要）
- [ ] 安装包和符号文件 SHA-256 已记录

### 文档与发布

- [ ] README、用户说明、已知问题和 Changelog 已更新
- [ ] About、版本说明和二进制版本一致
- [ ] ADR、风险、债务和项目状态已更新
- [ ] Git tag 与 GitHub Release 已创建
- [ ] 发布制品已上传到受控制品库

### 恢复

- [ ] 上一稳定版本仍可获取
- [ ] 回滚步骤已验证
- [ ] 源码、制品和测试数据备份可恢复

### 发布结论

- 结论：通过 / 有条件通过 / 拒绝
- 未完成项及批准人：待填写
- 证据链接：待填写

---

## 版本：v0.3.0-preview.1

- 发布负责人：Power-Z / Codex
- commit/tag：`v0.3.0-preview.1` 所指向的发布提交
- 日期：2026-08-16

### 范围与质量

- [x] 本次 RAW 显示修复与 Pixel Statistics 需求已追踪并验证
- [x] Debug/Release CTest 4/4 通过
- [x] 正常、边界、错误、Bayer 分通道和取消路径通过
- [x] 11776×8842 实际 RAW 全图统计黄金值通过
- [x] 未新增未接受的高影响风险
- [x] 现有技术债务已登记

### 数据与安全

- [x] Git 变更和 ZIP 制品不包含本地 RAW、令牌、密钥或日志
- [x] 实际样本仅以参数、摘要和 SHA-256 清单引用
- [x] Qt、LibRaw 与 MSVC Runtime 依赖清单和许可证材料进入制品
- [x] GPL demosaic packs 未链接
- [ ] 未执行独立依赖漏洞扫描或正式法律审查

### 构建与制品

- [x] MSVC Release 构建和测试通过
- [x] 编译器、Qt 6.8.3、LibRaw 0.22.2 和依赖版本已记录
- [x] Windows x64 便携 ZIP 启动并保持响应
- [x] Qt、LibRaw、MSVC CRT 与 qwindows 插件均从包内加载
- [x] ZIP SHA-256 已生成并作为独立 Release asset 上传
- [ ] Preview 便携包未签名，未提供安装/升级/卸载程序
- [ ] 未在独立干净 Windows 虚拟机上复测

### 文档与发布

- [x] README、可行性方案、需求追踪、项目状态和 Changelog 已更新
- [x] About 与 `v0.3.0-preview.1` 一致
- [x] Git tag 与 GitHub prerelease 已创建
- [x] Windows x64 ZIP 与 SHA-256 已上传至 GitHub Release

### 发布结论

- 结论：有条件通过，仅作为 Preview 预发布
- 未完成项：代码签名、正式许可证法律复核、独立干净 Windows 验收、安装器；正式稳定版前完成
- 证据：Debug/Release CTest、实际 RAW 黄金值测试、便携包模块路径与启动冒烟测试、GitHub Release assets

---

## 版本：v0.3.0-preview.2

- 发布负责人：Power-Z / Codex
- commit/tag：`v0.3.0-preview.2` 所指向的发布提交
- 日期：2026-08-18

### 范围与质量

- [x] V0.4～V0.7.1、Recent Files 和发布需求已进入追踪矩阵
- [x] Debug/Release CTest 4/4 通过
- [x] Bayer Extract 正常、部分边缘、全选恒等、取消和坐标映射测试通过
- [x] Pixel Info 连续跟随、500/500 可见像素、RGB 自动值和 RGGB 遮罩测试通过
- [x] 11776×8842 实际 RAW 全图统计和 Bayer Extract 性能回归通过
- [x] 未新增未接受的高影响风险，现有风险与技术债务已保留

### 数据与安全

- [x] Git diff 与暂存清单不包含本地 RAW、构建目录、令牌、密钥或日志
- [x] `.gitignore` 继续排除 `Data/`、`artifacts/`、`*.raw`、`*.bin` 和 `*.dng`
- [x] 实际样本只在清单中记录摘要、参数和授权，不进入 GitHub
- [x] Qt、LibRaw、MSVC Runtime 的 notices 与许可证进入便携包
- [x] GPL demosaic packs 未链接
- [ ] 未执行独立依赖漏洞扫描或正式法律审查

### 构建与制品

- [x] MSVC Debug/Release 构建与测试通过
- [x] 记录 Visual Studio 2022、Qt 6.8.3、LibRaw 0.22.2 和 CMake 基线
- [x] Windows x64 便携包通过启动并保持响应测试
- [x] 包含 Qt DLL、LibRaw、MSVC CRT、`platforms/qwindows.dll` 和许可证
- [x] 生成 ZIP 及独立 SHA-256 校验文件
- [ ] Preview 包未签名，未提供安装/升级/卸载程序
- [ ] 未在独立干净 Windows 虚拟机上复测

### 文档与发布

- [x] README、用户指南、Release Notes、Changelog、需求追踪和项目状态已更新
- [x] 应用版本、About、打包脚本和 `v0.3.0-preview.2` 一致
- [x] 源码提交与 tag 已推送到 GitHub
- [x] GitHub prerelease 包含 Windows x64 ZIP 与 SHA-256 assets

### 恢复

- [x] `v0.3.0-preview.1` 源码标签和 Release 仍可获取
- [x] 回滚方式为在独立目录解压 Preview 1，不覆盖当前便携包
- [x] 源码位于 GitHub 与本地仓库；测试 RAW 仍在独立受控存储

### 发布结论

- 结论：有条件通过，仅作为 Preview 预发布
- 未完成项：代码签名、正式许可证法律复核、独立干净 Windows 验收、安装器；正式稳定版前完成
- 证据：Debug/Release CTest、真实 RAW 黄金值、Bayer Extract 性能记录、离屏 UI 测试、便携包启动测试、GitHub Release assets

---

## 版本：v0.3.0-preview.3

- 发布负责人：Power-Z / Codex
- commit/tag：`v0.3.0-preview.3` 所指向的发布提交
- 日期：2026-08-19

### 范围与质量

- [x] V0.7.2～V0.7.8 已进入需求追踪矩阵并标记 Verified
- [x] Debug/Release CTest 4/4 通过
- [x] Pixel Info 缓存、当前显示 Pixel Statistics、Extract 标注和视口综合导航回归通过
- [x] Mean/Gaussian/Median 黄金值、预览边界和 Filter 瓦片复用通过
- [x] 四种 Bayer 排列、三种 Demosaic 算法、惰性 RGB、Filter 链和输入恢复通过
- [x] 未新增未接受的高影响风险，现有技术债务保持登记

### 数据与安全

- [x] Git 变更不包含本地 RAW、构建目录、令牌、密钥或日志
- [x] `.gitignore` 继续排除 `Data/`、`artifacts/` 和 RAW 样本
- [x] Qt、LibRaw、MSVC Runtime notices 与许可证进入便携包
- [x] 未链接 LibRaw GPL demosaic packs
- [ ] 未执行独立依赖漏洞扫描或正式法律审查

### 构建与制品

- [x] MSVC Debug/Release 构建与测试通过
- [x] Windows x64 便携包通过隐藏窗口启动响应测试
- [x] ZIP 包含 Qt 6.8.3、LibRaw 0.22.2、MSVC CRT 和 qwindows 插件
- [x] 生成 ZIP、独立 SHA-256，并上传同构建 `RawViewer.exe`
- [ ] Preview 包未签名，未提供安装/升级/卸载程序
- [ ] 未在独立干净 Windows 虚拟机上复测

### 文档与发布

- [x] README、用户指南、Release Notes、Changelog、追踪和项目状态已更新
- [x] 应用版本、About、打包脚本和 `v0.3.0-preview.3` 一致
- [x] 源码提交、tag 和当前功能分支已推送到 GitHub
- [x] GitHub prerelease 包含 Windows x64 ZIP、SHA-256 和 EXE assets

### 恢复

- [x] Preview 1/2 标签与 Release 保持可获取
- [x] 回滚方式为在独立目录解压旧 Preview，不覆盖当前便携包

### 发布结论

- 结论：有条件通过，仅作为 Preview 预发布
- 未完成项：代码签名、正式许可证法律复核、独立干净 Windows 验收、安装器
- 证据：Debug/Release CTest、算法黄金值、离屏 UI、瓦片读取次数、便携包启动测试和 GitHub Release assets

---

## 版本：v0.3.0-preview.4

- 发布负责人：Power-Z / Codex
- commit/tag：`v0.3.0-preview.4` 所指向的发布提交
- 日期：2026-08-20

### 范围与质量

- [x] V0.7.9～V0.7.14 已进入需求追踪矩阵并标记 Verified
- [x] Debug/Release CTest 4/4 通过
- [x] Extract 坐标、RGB 像素标注、Pixel Info 与 Pixel Statistics UI 回归通过
- [x] 五步全局 Undo/Redo、几何编辑、任务失效和统一刷新回归通过
- [x] 全分辨率多通道直方图、BLV/WLV 双手柄与性能快速路径回归通过
- [x] 未新增未接受的高影响风险，现有技术债务保持登记

### 数据与安全

- [x] Git 变更不包含本地 RAW、构建目录、令牌、密钥或日志
- [x] `.gitignore` 继续排除 `Data/`、`artifacts/` 和 RAW 样本
- [x] Qt、LibRaw、MSVC Runtime notices 与许可证进入便携包
- [x] 未链接 LibRaw GPL demosaic packs
- [ ] 未执行独立依赖漏洞扫描或正式法律审查

### 构建与制品

- [x] MSVC Debug/Release 构建与测试通过
- [x] Windows x64 便携包通过隐藏窗口启动响应测试
- [x] ZIP 包含 Qt 6.8.3、LibRaw 0.22.2、MSVC CRT 和 qwindows 插件
- [x] 生成 ZIP、独立 SHA-256，并上传同构建 `RawViewer.exe`
- [ ] Preview 包未签名，未提供安装/升级/卸载程序
- [ ] 未在独立干净 Windows 虚拟机上复测

### 文档与发布

- [x] README、用户指南、Release Notes、Changelog、追踪和项目状态已更新
- [x] 应用版本、About、打包脚本和 `v0.3.0-preview.4` 一致
- [x] 源码提交、tag 和当前功能分支已推送到 GitHub
- [x] GitHub prerelease 包含 Windows x64 ZIP、SHA-256 和 EXE assets

### 恢复

- [x] Preview 1/2/3 标签与 Release 保持可获取
- [x] 回滚方式为在独立目录解压旧 Preview，不覆盖当前便携包

### 发布结论

- 结论：有条件通过，仅作为 Preview 预发布
- 未完成项：代码签名、正式许可证法律复核、独立干净 Windows 验收、安装器
- 证据：Debug/Release CTest、黄金值、离屏 UI、直方图快速路径、投影缓存、便携包启动测试和 GitHub Release assets
