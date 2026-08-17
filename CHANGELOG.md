# Changelog

本项目的重要变更记录在此文件中，格式参考 Keep a Changelog，版本号计划遵循语义化版本。

## [Unreleased]

## [0.3.0-preview.2] - 2026-08-18

### Added

- V0.4 Bayer Channel Extract 支持 2×2、4×4、8×8 与 1×1～16×16 自定义重复掩码、行优先/列优先紧凑排列，以及配置命名、保存、加载和删除。
- 非整倍数图像或 ROI 在提取前提示确认，并只保留右侧/底部部分单元中真实存在的已选像素。
- V0.5 File 菜单增加 `Recent Files`，保存最近十个成功打开文档及完整 RAW 加载配置，支持 MRU 去重、缺失文件标记和清除记录。

### Changed

- Bayer 提取结果改为通用只读坐标映射；完整 RAW 不复制，CSV 使用输出坐标并跳过矩形补位和边缘无效位置。
- Bayer Extract 对话框更新为卡片式矩阵编辑界面，提供全选、清空、反选和选中数量反馈。
- V0.6 明确 Bayer Extract 为基于原图的周期位置采样工具，支持标准 Bayer 2×2、Quad Bayer 4×4、Hex Bayer 8×8 及无标准 Bayer 元数据的特殊 pattern。
- V0.6 精简 Bayer Extract：固定全图、每次从 original source 提取，删除 ROI、选中数量和 CSV 入口；矩阵改为外部坐标轴并随 block 尺寸动态调整窗口。
- V0.6.1 修复 Bayer Extract 窗口由大 pattern 切回小 pattern 时不缩小，以及全选在非整倍数边缘产生黑色补位、降采样预览看起来不同于原图的问题。
- Bayer Extract 增加全选零拷贝恒等路径、单位置坐标预计算，并将有界 signal preview 从 2048 调整为 1024，移除后台重复 RGBA 生成。
- Bayer Extract 主操作按钮由 `Extract & display` 简化为 `Extract`。
- V0.6.2 将 Bayer Extract 改为更小的双卡片布局：矩阵区提高到约 2/5，Pattern 标题固定为 1.5 倍字体行高，预设默认隐藏自定义编辑栏。
- Bayer Extract 的排列选项简化为 `Row-major` / `Column-major` 并增加 `?` 说明；矩阵单元改为 24×24 无圆角方块和 3 px 间距。
- V0.6.3 将 packing 帮助改为 18×18 无边框提示按钮，并删除 Bayer Extract 顶部 Source 信息行。
- Bayer Extract 矩阵单元按 pattern 最大边自适应为 48/36/24/12 px，使 2×2 更醒目，同时控制 8×8 和自定义大矩阵的窗口尺寸。
- V0.7 RAW 像素标注删除 `Raw` 前缀、彩色文字和半透明底板，改为按显示灰度自动选择黑字或白字，字高约为显示像素高度的 1/7。
- Pixel Info 默认关闭 Bayer mesh；拖拽期间和连续滚轮缩放的中间帧暂停标签查询与绘制，停止交互 80 ms 后恢复，以提升大倍率标注下的流畅度。
- V0.7.1 取消交互期间暂停标注和 200 标签抽样上限；达到 40 px/像素后标注全部可见像素，并在拖拽、缩放的每一帧跟随图像。
- Pixel Info 精简为像素值、Bayer mesh、Bayer pattern 三项；RAW 自动显示原始值，标准图片自动显示 RGB 值，像素值字体调整为 1/6 高。
- Bayer mesh 改为 R/Gr/Gb/B 半透明四色遮罩（两种绿色不同），Pattern 英文字样以 1/7 高字体显示在像素右下角。

## [0.3.0-preview.1] - 2026-08-16

### Added

- 建立 V0.0 仓库维护基线。
- 增加业务与数据需求填写模板。
- 增加 GitHub Issue、Pull Request 和基础质量检查配置。
- 增加分层架构原则与 ADR 模板。
- 增加 V0.1 C++20 + Qt 6 + CMake 分层架构提案。
- 增加不可变瓦片化大图管线、五步撤销和后台任务设计。
- 增加 V0.2～V0.4 三阶段实施方案与验收门槛。
- 增加 `AGENTS.md` 以及状态、需求追踪、风险、样本、债务和发布记录。
- 确认 Windows x64、Qt 开发期 LGPL 动态链接、UInt32/Float32 和统计方向。
- 增加内容探测、LibRaw 相机 RAW 与平面 RAW 双解码架构。
- 登记 Hasselblad X2D 100C 本地样本及参考测试设备。
- 将性能基准更新为 Windows 11 / Intel Core Ultra 5 / 16 GiB 显存，并批准 200 MP / 400 MB 样本延期到 V0.4。
- 建立 C++20、Qt 6.8.3、CMake Presets、CTest 和 Windows CI 工程。
- 增加领域、应用、基础设施、表现和组装入口五层 target。
- 增加 JPG/PNG/BMP、byte-aligned 平面 RAW 和 LibRaw 相机 RAW 解码器。
- 增加三栏主窗口、文件浏览、拖放、五种主题、预览直方图、缩放平移和状态栏像素查询。
- 增加异步打开、取消标志、generation 保护、结构化错误和本地日志。
- 增加 RAW 尺寸安全、端序、header、stride、UInt32/Float32 和真实 Hasselblad 样本测试。
- 增加不可变原始信号、显示映射 revision 和 CPU 参考预览重映射。
- 增加 Sensor BLV / Display BLV 分离、Display WLV、Gamma 和恢复图像默认值控制。
- 增加每文档独立的五步显示参数撤销/重做，并验证历史隔离和预览不修改原始数据。
- 修正 LibRaw 黑电平语义为基础黑电平与四通道偏移之和；Hasselblad 样本默认值为 4093.5。
- 增加 Pixel Info 非模态控制窗口、Raw/Display/RGB 开关、Bayer mesh 和 R/Gr/Gb/B 英文字标注。
- 增加像素标注缩放阈值、可见区域均匀抽样和严格标签上限，并将最大分析缩放提高到 256 倍。
- 增加 16 ms 状态栏查询与显示预览刷新节流。
- 增加 Bayer Extract 非模态工具，支持 R/Gr/Gb/B、完整图像或源 ROI 提取及原图恢复。
- 实现 V0.3 `Pixel Statistics`：Status、Horizontal Box、Vertical Box、Line 与预留 WB 五模式，采用两次左键完成矩形/线段选择。
- 增加原始 Bayer All/R/Gr/Gb/B 分通道统计，使用 Welford 计算 count/min/max/mean/population std，并显示有界直方图或 profile 折线。
- 像素统计在后台流式扫描只读 `IPixelSource`，支持进度、取消和 generation 失效保护，不复制整幅 RAW；11776×8842 实际 RAW 全图黄金值回归通过。
- Pixel Statistics 子窗口采用顶部/控制/图表 `1:2:5` 布局，并提供 bins、grid、points、fill、line width 等显示控制。
- 增加不复制完整通道数据的只读 2×2 像素源视图、通道/源坐标互转和约 3 MP 有界预览。
- 增加原子 CSV 导出接口，记录通道坐标、源坐标和原始信号值。
- 增加奇数尺寸、非对齐 ROI、空通道、取消、坐标溢出和 CSV 黄金测试。
- 增加文件树打开 RAW 时的视口内不确定进度加载动画。

### Fixed

- 按 V0.2.1 将平面 RAW 默认参数设为 11776×8842 UInt16 小端，并增加真实平面样本集成测试。
- 明确 `.raw/.RAW/.bin/.BIN` 在 Skip bytes 后按紧密行顺序读取，文件尾部超出 W×H 的数据不参与显示。
- 修正平面 RAW 被缩小到 2048 宽预览后再拉伸造成的像素错位感；UInt16 现在使用完整 W×H Grayscale16 视图。
- 修正平面 RAW 的 RGB 着色、平滑插值和伪 RGB 像素标签，改为最近邻灰度显示并直接标注原始 UInt16。
