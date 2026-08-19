# 需求追踪矩阵

来源：`require/Raw_Viewer.md`、`require/require_V0.0.md` V0.2/V0.2.1

状态值：`Defined`、`Planned`、`Implemented`、`Verified`、`Deferred`。

| ID | 需求摘要 | 设计位置 | 计划 | 状态 |
|---|---|---|---|---|
| UI-001 | 顶部主菜单 | 架构 6；`src/presentation/main_window.cpp` | P1-02 | Implemented |
| UI-002 | 底部状态栏显示坐标、信号和图片信息 | 架构 6、10；`MainWindow::updateCoordinate` | P1-02/P1-05 | Implemented |
| UI-003 | 左 20% / 中 60% / 右 20% 三栏与内部区域 | 架构 6；`MainWindow` splitter | P1-02 | Implemented |
| UI-004 | Dark/Light/Gray/Yellow/Red 主题 | 架构 6；`ThemeManager` | P1-02 | Implemented |
| UI-005 | 新版本说明与 About | 架构 6；帮助菜单 | P1-02 | Implemented |
| V02-UI-001 | 左侧文件树双击加载 RAW 时显示加载动画 | `MainWindow::beginOpen`、`ImageViewport::setLoading` | V0.2 补充需求 | Implemented |
| DATA-001 | 拖拽或文件树打开 | 架构 6、10；`ImageViewport` / `MainWindow` | P1-03 | Implemented |
| DATA-002 | JPG/BMP/PNG 直接打开 | 架构 5.3、7；`QtImageDecoder` | P1-03 | Implemented |
| DATA-003 | 平面 RAW/BIN 按参数打开；相机 RAW 按内容探测 | 架构 7.2～7.4、ADR-0003；decoder tests | P1-04/P1-04B | Verified |
| V02-DATA-001 | `.raw/.RAW/.bin/.BIN` 从 skip 后按顺序展开，按 11776×8842 UInt16 小端生成完整 W×H Grayscale16；不抽样、不进行 Bayer RGB 着色，最近邻缩放并标注原始值 | `FlatRawDecoder`、`ImageViewport`、`PixelInfo`；`unfoldsSequentialBayerSamplesAfterSkip`、`preservesDirectGray16WithoutRgbRendering`、`verifiesApprovedFlatSampleWhenConfigured` | V0.2/V0.2.1 补充需求 | Verified |
| DATA-004 | 原始/中间/显示三层且原始不变 | 架构 7.1、ADR-0002；`DocumentSession` + `PreviewRenderer`；`DocumentSessionTest::rendersWithoutChangingOriginal` | P2-01 | Verified |
| DATA-005 | 基础 ISP 与五步撤销 | 架构 9、12；`DisplayMapping` + `DocumentSession`；application/domain tests | P2-01/P2-02 | Implemented |
| DATA-006 | 200 MP / 400 MB 大图 | 架构 8、ADR-0002/0003 | P3-01～P3-03 | Planned |
| VIEW-001 | 无工具时左键拖动、滚轮鼠标锚点缩放 | 架构 10.1；`ImageViewport` | P1-05 | Implemented |
| VIEW-002 | 多图同步，Alt+左键单独拖动 | 架构 10.2 | P2-06 | Planned |
| VIEW-003 | 放大后标注 RAW 信号或 RGB | 架构 8.4、10.1；`ImageViewport::drawPixelOverlay`；Pixel Info/decoder tests | P2-03 | Verified |
| TOOL-001 | Pixel Info 信号、mesh、pattern 标注 | 架构 10.3、12；`queryPixelInfo` + `PixelInfoDialog`；四 pattern 测试与 `CAMERA-HB-X2D-001` UI 验收 | P2-03 | Verified |
| TOOL-002 | Pixel Statistics：Status、Horizontal Box、Vertical Box、Line，WB 预留；两次点击选择、原始 Bayer 分通道、后台进度/取消、统计卡片和图表 | 架构 11；`PixelStatisticsService`、`PixelStatisticsDialog`、`ImageViewport`；application/UI 黄金测试及 `FLAT-HB-X2D-001` 全图统计 | P2-05 | Verified |
| TOOL-003 | Bayer pattern 特定像素提取 | 架构 12；`BayerExtractService` + `BayerExtractDialog` + `BayerCsvExporter`；odd-size/ROI/CSV tests 与 `CAMERA-HB-X2D-001` UI 验收 | P2-04 | Verified |
| TOOL-004 | V0.4 n×m Bayer Extract：矩阵选择、行/列优先、部分边缘、配置持久化与现代化 UI | `docs/plans/v0.4-bayer-mask-extract.md`；通用 `BayerMaskPattern`/只读 `BayerPlaneGeometry`；应用层行列优先与边缘黄金测试、CSV 和 UI 持久化测试 | V0.4-01～04 | Verified |
| FILE-002 | V0.5 Recent Files：最近十个成功文档、加载配置恢复、缺失文件处理 | `docs/plans/v0.5-recent-files.md`；`IRecentDocumentStore` + `QtRecentDocumentStore` + `MainWindow::refreshRecentFilesMenu`；MRU/去重/64 位配置/缺失文件 UI/清除测试 | V0.5-01～02 | Verified |
| TOOL-005 | V0.6 Bayer Position Extract：标准/Quad/Hex/特殊 pattern、固定原图全图采样、精简动态 UI | `docs/plans/v0.6-bayer-position-extract.md`；`BayerExtractService` + `BayerExtractDialog` + `MainWindow::beginBayerExtraction`；2×2 四角、4×4、8×8、None pattern 与 UI 结构测试 | V0.6-01～09 | Verified |
| TOOL-006 | V0.6.1 Bayer Extract：窗口双向缩放、全选恒等显示和提取性能 | `docs/plans/v0.6.1-bayer-extract-performance.md`；identity zero-copy、single-position axis cache、1024 signal preview；全选/非整倍数/预览/UI 往返尺寸及 11776×8842 实图测试 | V0.6.1-01～04 | Verified |
| TOOL-007 | V0.6.2 Bayer Extract：矩阵区约 2/5、Pattern 固定行高、自定义按需展开、方形紧密网格、英文排列帮助和整体缩小 | `docs/plans/v0.6.2-bayer-extract-ui.md`；`BayerExtractDialog` 双卡片布局；标题/Custom/网格/尺寸/持久化 UI 自动化测试 | V0.6.2-01～06 | Verified |
| TOOL-008 | V0.6.3 Bayer Extract：常规小型帮助提示、自适应方形单元并删除顶部 Source 信息 | `docs/plans/v0.6.3-bayer-extract-ui.md`；`BayerExtractDialog::rebuildMatrix/adjustDialogSize/setSource`；2×2/4×4/8×8/Custom 尺寸及标签 UI 测试 | V0.6.3-01～03 | Verified |
| VIEW-004 | V0.7 RAW 像素标注：纯数值、黑白自适应、默认无网格、1/7 字高及交互性能优化 | `docs/plans/v0.7-pixel-value-overlay.md`；`ImageViewport::drawPixelOverlay/mousePressEvent/wheelEvent`、`PixelInfoDialog`；黑白 RAW 离屏渲染与交互期间零像素查询测试 | V0.7-01～05 | Verified |
| VIEW-005 | V0.7.1 连续像素标注：无闪烁跟随、全可见像素、自动 RAW/RGB 值、RGGB 四色遮罩和右下角 Pattern | `docs/plans/v0.7.1-continuous-pixel-overlay.md`；`ImageViewport::drawPixelOverlay`、`PixelInfoDialog`；连续拖拽/滚轮、500 像素、四色遮罩、RGB 自动格式 UI 测试 | V0.7.1-01～06 | Verified |
| VIEW-006 | V0.7.2 像素标注：初始自动启用、Pattern 四通道独立字体颜色、可见区域标签缓存与静态文本复用 | `docs/plans/v0.7.2-pixel-overlay-optimization.md`；`MainWindow` 初始选项同步、`ImageViewport` 有界缓存；初始显示、缓存复用和四色字体离屏 UI 测试 | V0.7.2-01～03 | Verified |
| TOOL-009 | V0.7.3 Pixel Statistics：统计当前显示 RAW 管线、拖拽选择、紧凑专业分格 UI | `docs/plans/v0.7.3-pixel-statistics-optimization.md`；`MainWindow::openPixelStatistics/beginPixelStatistics`、`ImageViewport`、`PixelStatisticsDialog`；提取结果黄金测试及端到端 UI 测试 | V0.7.3-01～03 | Verified |
| TOOL-010 | V0.7.4 Pixel Statistics：Channel 复选后显示 R/Gr/Gb/B 四结果；图表参数移至 Preferences，线宽默认 1 | `docs/plans/v0.7.4-pixel-statistics-channels-preferences.md`；`PixelStatisticsService::executeChannels`、`PixelStatisticsDialog::showPreferences/setResults`；单遍扫描、四面板、端到端和持久化测试 | V0.7.4-01～02 | Verified |
| VIEW-007 | V0.7.5 提取结果像素标注：连续 Extract 均从 original 开始；数值读取当前显示提取源；紧凑输出保留源 Bayer 通道标注 | `docs/plans/v0.7.5-extracted-pixel-annotation.md`；`IPixelSource::bayerChannel`、`BayerPlanePixelSource`、`queryPixelInfo`、`ImageViewport`；连续提取精确值与提取后离屏标注测试 | V0.7.5-01～03 | Verified |
| VIEW-008 | V0.7.6 RAW 视口综合导航：中键平移、像素标尺、全图缩略视野框、底部/右侧滚动条、默认关闭 Bayer pattern | `docs/plans/v0.7.6-viewport-navigation.md`；`ImageViewport::canvasRect/drawRulers/drawOverview/updateScrollBars`、`PixelInfoDialog`；中键工具模式、刻度对齐、缩略图和滚动条离屏/UI 测试 | V0.7.6-01～05 | Verified |
| TOOL-011 | V0.7.7 Filter：当前显示 RAW 的 Mean/Gaussian/Median、kernel 子窗口和大图性能优化 | `docs/plans/v0.7.7-filter-tool.md`；`FilterService`、`FilteredPixelSource`、`FilterDialog`、`MainWindow::beginFilter`；算法黄金值、惰性链、瓦片读取次数、预览上限和提取后端到端 UI 测试 | V0.7.7-01～04 | Verified |
| TOOL-012 | V0.7.8 Bayer Demosaic：三种常用算法、当前规则 Bayer RAW、专业子窗口和惰性 RGB 性能设计 | `docs/plans/v0.7.8-bayer-demosaic.md`；`DemosaicService`、`DemosaicedPixelSource`、`DemosaicDialog`、`MainWindow::beginDemosaic`；四 pattern×三算法、脉冲系数、瓦片读取、不可用状态及 Filter→Demosaic UI 测试 | V0.7.8-01～05 | Verified |
| VIEW-009 | V0.7.9 综合优化：顶部/左侧主题标尺；Extract 紧凑输出坐标、精确显示与标注一致 | `docs/plans/v0.7.9-extract-coordinate-accuracy.md`；`BayerExtractService`、`MainWindow::flushCoordinateUpdate`、`ImageViewport::drawExactPixelLayer/drawRulers`；200×200→100×100 黄金测试、状态栏与精确像素缓存 UI 测试 | V0.7.9-01～02 | Verified |
| VIEW-010 | V0.7.10 像素标注：RGB 三行左下角、连续换图坐标同步、Pixel Info 4×4 RGGB 实时示例和直角 UI | `docs/plans/v0.7.10-pixel-overlay-rgb-preview.md`；`ImageViewport::setImage/drawPixelOverlay/makeOverlayCell`、`PixelInfoDialog`；RGB 离屏布局、未完成拖拽后换图坐标、三选项预览差异测试 | V0.7.10-01～03 | Verified |
| TOOL-013 | V0.7.11 Pixel Statistics/RGB 标注：紧凑直角统计窗口、放大指标标题、主题无关黑色细图线、固定灰色绘图区、悬停轴向引导线及 60% RGB 通道色标注 | `docs/plans/v0.7.11-pixel-statistics-visual-optimization.md`；`PixelStatisticsDialog`、`StatisticsChartWidget`、`ImageViewport::drawPixelOverlay`；紧凑布局、深色主题图表/悬停和 RGB 三色离屏测试 | V0.7.11-01～05 | Verified |
| EDIT-001 | V0.7.12 编辑与全局撤销：Flip/Mirror/三种 Rotate 及快捷键；显示参数、Extract、Filter、Demosaic 等统一进入每文档五步历史；撤回同步刷新显示和统计 | `docs/plans/v0.7.12-edit-global-undo.md`；`ImageTransformService`、`DocumentSession::commitPipelineEdit/undo/redo`、`MainWindow::refreshFromDocumentState/applyHistoryStep`；五变换黄金矩阵、混合历史、快捷键、精确像素和统计自动刷新测试 | V0.7.12-01～02 | Verified |
| VIEW-011 | V0.7.13 全图统计与显示窗口：Bayer R/Gr/Gb/B、RGB R/G/B/Y、单通道三种自动模式；BLV/WLV 一位小数、共轴双手柄和区间放大精调 | `docs/plans/v0.7.13-global-histogram-display-window.md`；`GlobalHistogramService`、`HistogramWidget`、`MainWindow::beginGlobalHistogram/previewHistogramWindow`；三模式黄金值、取消、四色离屏曲线、手柄与主窗口同步测试 | V0.7.13-01～02 | Verified |
| VIEW-012 | V0.7.14 全图统计性能与 Display Window：双手柄松手应用、输入防抖、连续内存统计快速路径、曲线投影缓存、RAW Gamma=1 及正确黑白阈值 | `docs/plans/v0.7.14-global-histogram-performance.md`；`GlobalHistogramService`、`HistogramWidget::ensureProjection`、`ImageViewport::ensureGrayscaleDisplayLut`、`MainWindow::commitHistogramWindow`；零虚函数读取、投影复用、延迟历史和黑/白/中灰 UI 测试 | V0.7.14-01～04 | Verified |
| TOOL-004 | 预留复杂 ISP 工具 | 架构 12 | P2-01，持续 | Planned |

## 追踪规则

- 实现 PR 必须引用一个或多个需求 ID；
- 状态改为 `Implemented` 时必须链接实现路径；
- 状态改为 `Verified` 时必须给出测试名、样本 ID 和结果；
- 需求变更不得直接覆盖旧含义，应记录变更日期并评估已有测试；
- `Deferred` 必须写明推迟到哪个里程碑及原因。
