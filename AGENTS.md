# AGENTS.md

本文件约束所有在本仓库工作的自动化代理和开发者。除非用户明确覆盖，仓库内所有目录都遵循这些规则。

## 1. 项目目标与当前阶段

Raw Viewer 是面向 Bayer RAW 图像的桌面浏览、处理和统计工具，要求可长期维护，并能处理超过 2 亿像素、约 400 MB 的输入。

当前阶段为 **V0.1：架构与实施方案**。此阶段的交付物是需求基线、架构设计、三阶段计划和维护记录，不应擅自开始大规模功能编码。

权威输入按优先级排列：

1. 用户当前请求；
2. `require/require_V0.0.md` 中的最新版本要求；
3. `require/Raw_Viewer.md` 中的软件需求；
4. `docs/architecture/v0.1-architecture.md`；
5. 已接受的 `docs/decisions/*.md`；
6. 其他过程和说明文档。

发现冲突时必须记录冲突、给出推荐默认值，并在会导致返工或数据错误时请求确认。

## 2. 推荐技术基线

- 语言：C++20；
- UI：Qt 6 Widgets；
- 构建：CMake + CMake Presets；
- 测试：CTest + Qt Test，领域算法可使用独立轻量测试框架但需 ADR；
- 首发平台：Windows 10/11 x64（已确认）；
- 性能基准设备：Windows 11 x64、Intel Core Ultra 5、16 GiB 显存；GPU 型号、系统内存和磁盘待补；
- 相机 RAW 容器：使用主线 LibRaw 动态库适配，按内容签名识别，不依赖扩展名；
- 平面 RAW：使用项目自有 `FlatBinaryRawDecoder`，参数由用户显式提供；
- 图像显示：抽象渲染接口，第一阶段允许 CPU/QPainter，实现超大图阶段切换为瓦片化 GPU 渲染；
- 并发：应用层可取消任务 + 有界线程池，禁止在 UI 线程做文件解码、全图统计或大块转换。

技术基线发生变化时，必须先新增或更新 ADR。

## 3. 分层与依赖规则

计划中的代码目录：

```text
apps/raw-viewer/          程序入口和依赖组装
src/domain/               领域对象、规则、处理描述；不依赖 Qt UI 和文件系统
src/application/          用例、端口、任务协调；依赖 domain
src/infrastructure/       文件、解码、缓存、统计、设置等端口实现
src/presentation/         Qt Widgets 界面、视图模型、输入适配
tests/unit/
tests/integration/
tests/fixtures/
benchmarks/
```

允许的依赖方向：

```text
presentation -> application -> domain
infrastructure -> application -> domain
apps/raw-viewer -> presentation + infrastructure
```

禁止：

- `domain` 包含 QWidget、QFile、注册表、OpenGL 或日志后端代码；
- UI 直接解析 RAW 字节、计算统计值或管理算法缓存；
- infrastructure 反向调用具体窗口；
- 为方便而跨层暴露可变像素缓冲区；
- 使用全局单例保存当前文档、当前图像或工具状态。

## 4. 数据与算法约束

- 原始数据不可变，处理操作不得覆盖输入文件或原始内存视图；
- 原始、中间、显示三层数据必须有不同类型，禁止仅靠变量名区分；
- 大图默认使用瓦片、惰性计算和有界缓存，禁止无评估地生成多份整图副本；
- 所有尺寸、偏移和字节数计算使用带溢出检查的 64 位整数；
- 平面 RAW 打开前必须验证 `head + rowStride * height <= fileSize`；
- TIFF/DNG/厂商相机 RAW 不得通过“文件大小减像素字节数”猜测 header，必须由容器解码器读取元数据和数据偏移；
- 文件类型先检查内容签名，再参考扩展名；`.raw` 不保证是平面字节数组；
- 字节序、存储位宽、有效位深、标量类型、Bayer pattern 和行步长是独立概念；
- 统计口径必须明确 ROI 边界、Bayer 通道、空区域、NaN/Inf、饱和与整数溢出行为；
- 后台任务必须支持取消或世代号失效，过期结果不得更新新文档；
- 撤销栈初期限制为 5 步，只保存操作参数/管线状态，不保存五份整图。

## 5. 测试与样本

- 不得提交客户、个人、未脱敏或无再分发授权的 RAW 数据；
- `tests/fixtures/` 只允许小型、人工生成或明确授权的样本；
- 外部大样本登记在 `docs/records/test-data-manifest.md`，记录 SHA-256、来源、参数和授权；
- RAW 解码必须覆盖正常、截断、头偏移错误、尺寸溢出、端序和不支持格式；
- 相机 RAW 集成测试使用 LibRaw 主线构建；生产依赖不得无意包含 GPL demosaic packs；
- 统计与处理算法使用可独立计算的黄金值，浮点结果写明绝对/相对误差；
- 性能变更使用固定设备和固定样本记录基准，不能只凭主观流畅度验收。
- 200 MP / 400 MB 正式样本延期到 V0.4，不阻塞 V0.2；不得因此降低最终容量目标。

当前没有应用代码时，至少运行：

```powershell
git diff --check
```

代码骨架建立后，标准命令应统一为：

```powershell
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug
```

如果实际预设名称变化，必须同步更新本文件、README 和 CI。

## 6. Git 与文档维护

- 从最新 `main` 创建短分支，命名遵循 `feature/`、`fix/`、`docs/`、`chore/`、`spike/`；
- 不重写共享分支历史，不使用强制推送，除非用户明确授权；
- 提交前检查工作区，保留用户无关的在途修改；
- 提交信息采用 Conventional Commits；
- 每个行为变更同步更新测试、需求追踪和 `CHANGELOG.md`；
- 架构决定写入 ADR，风险写入风险登记表，已知债务写入技术债务表；
- 版本完成时更新 `docs/records/project-status.md` 和发布检查表；
- 文档使用 UTF-8，术语首次出现时给出中英文或明确定义。

## 7. 完成定义

任务完成至少满足：

- 需求 ID 与验收结果可追踪；
- 分层依赖没有被破坏；
- 正常、边界和失败路径经过验证；
- 没有把密钥、隐私数据、未授权样本或构建产物提交入库；
- 相关检查通过，文档与实际行为一致；
- 仍未解决的问题被记录到风险、待决策或技术债务文件，而不是隐藏在代码注释中。
