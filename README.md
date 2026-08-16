# Raw Viewer

面向 Bayer RAW 图像的浏览、处理与统计软件。

## 当前阶段

项目处于 **V0.3：非破坏处理与基础分析** 阶段。V0.2 浏览闭环已合并，当前分支开始交付第二阶段能力：

- C++20、Qt 6 Widgets、CMake Presets 和分层 target；
- 20/60/20 三栏主窗口、文件树、拖放、状态栏和五种主题；
- JPG、PNG、BMP 基础解码；
- UInt8、UInt16、UInt32、Float32 平面 RAW，支持端序、skip bytes 和 row stride；
- 主线 LibRaw 0.22.2 相机 RAW 容器解码；
- 左键平移、鼠标锚点缩放、预览直方图和原始像素查询；
- 后台打开、视口加载动画、取消标志和 generation 防止旧结果覆盖。
- UInt16 平面 RAW 使用完整 W×H Grayscale16 视图，按文件顺序逐像素显示，不生成缩小预览、不按 Bayer channel 进行 RGB 着色；
- 原始信号与显示预览分离，调整显示参数不会修改原始图像；
- Sensor BLV 与 Display BLV 分离，并提供 Display WLV、Gamma 和图像默认值复位；
- 每个文档独立的五步参数撤销/重做，显示变化保持当前缩放与平移。
- Pixel Info 同时查询 Raw、Display、RGB 与 Bayer 通道，并在可读缩放级别绘制有界标签和 Bayer mesh。
- Bayer Extract 支持 R/Gr/Gb/B 只读通道视图、源 ROI、通道/源坐标互转和带坐标的 CSV 导出。
- `Pixel Statistics` 支持 Status、Horizontal Box、Vertical Box、Line 四种原始 Bayer 统计模式，WB 入口预留；两次左键完成矩形/线段选择；后台计算 count/min/max/mean/std、直方图或一维 profile；支持 All/R/Gr/Gb/B 通道、进度和取消。
- 像素统计流式读取只读原始像素源，不复制整幅 RAW；图表数据有界降采样。统计口径和性能设计见 [V0.3 Pixel Statistics 方案](docs/plans/v0.3-pixel-statistics.md)。

## Windows 开发环境

已验证的依赖基线：

| 依赖 | 版本/变体 |
|---|---|
| Visual Studio | 2022，Desktop development with C++ |
| CMake | 3.21 或更新 |
| Qt | 6.8.3，`msvc2022_64`，动态链接 |
| LibRaw | 0.22.2 官方 Win64 动态库，不含 GPL demosaic packs |

设置依赖路径；若依赖位于本机已验证的 `E:\Qt` 和 `E:\LibRaw` 位置，可直接省略：

```powershell
$env:RAWVIEWER_QT_ROOT = "C:\Qt\6.8.3\msvc2022_64"
$env:RAWVIEWER_LIBRAW_ROOT = "C:\LibRaw\LibRaw-0.22.2"
```

生成包含 Qt、VC Runtime、LibRaw 和第三方许可证的 Windows x64 便携包：

```powershell
.\scripts\package-windows.ps1 -Version v0.3.0-preview.1
```

输出位于 `artifacts/`，包含 ZIP 和 SHA-256 校验文件。

配置、构建、测试和运行：

```powershell
.\scripts\dev.ps1 configure
.\scripts\dev.ps1 build
.\scripts\dev.ps1 test
.\scripts\dev.ps1 run
```

直接打开文件：

```powershell
.\scripts\dev.ps1 run -- "D:\images\sample.raw"
```

验证本地受控相机样本：

```powershell
$env:RAWVIEWER_CAMERA_SAMPLE = "E:\code\Raw_viewer\Data\Test_data\B0012535.B0011072.3FR"
.\scripts\dev.ps1 test
```

同时用 11776×8842、UInt16、小端、Skip bytes=0 验证本地平面 RAW：

```powershell
$env:RAWVIEWER_FLAT_SAMPLE = "E:\code\Raw_viewer\Data\Test_data\B0012535.B0011072.raw"
.\scripts\dev.ps1 test
```

`Data/` 中的 RAW 不会进入 Git。平面 `.raw/.RAW/.bin/.BIN` 使用左侧参数，从 Skip bytes 后按行连续展开；UInt16 显示采用完整分辨率单通道 Grayscale16 和最近邻缩放，像素标签直接读取原始 UInt16。TIFF/DNG/厂商相机容器按内容签名进入 LibRaw，不会按扩展名盲目平面解码。

## 仓库结构

```text
.
├─ .github/                  GitHub 工作流、Issue 与 PR 模板
├─ apps/raw-viewer/          程序组装入口
├─ docs/
│  ├─ architecture/         架构原则与后续架构设计
│  ├─ decisions/            架构决策记录（ADR）
│  ├─ plans/                分阶段实施方案
│  ├─ process/              开发和维护流程
│  ├─ records/              状态、追踪、风险、样本与发布记录
│  └─ requirements/         业务与数据需求
├─ require/                 原始版本需求
├─ scripts/                 本地开发入口
├─ src/
│  ├─ domain/               RAW 描述与纯规则
│  ├─ application/          打开用例与端口
│  ├─ infrastructure/       Qt/LibRaw 解码和本地日志
│  └─ presentation/         Qt Widgets 界面
├─ tests/                   Qt Test / CTest 测试
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

仓库当前为私有，项目自身许可证尚未确定。开发期按 Qt LGPL 动态链接方案和 LibRaw LGPL 2.1/CDDL 1.0 双许可依赖方案实施；正式分发前必须再次完成许可证与制品清单复审。
