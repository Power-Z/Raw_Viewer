# 项目状态

- 当前版本阶段：V0.3 非破坏处理与基础分析（实施中）
- 状态日期：2026-08-19
- 默认分支：`main`

## 已完成

- V0.0 GitHub 私有仓库与维护流程；
- Issue、PR 和基础仓库质量检查；
- 业务/数据需求模板；
- V0.1 软件需求初稿；
- V0.1 分层架构和三阶段实施方案；
- 代理约束、ADR、风险、追踪、样本、债务和发布记录模板。
- 确认 Windows 10/11 x64、Qt 开发期 LGPL 动态链接、UInt32/Float32 和统计方向；
- 登记并核验 Hasselblad X2D 100C 相机 RAW 样本；
- 接受相机 RAW 容器与平面 RAW 双解码路径。
- 安装并验证 Qt 6.8.3 MSVC2022 x64 与主线 LibRaw 0.22.2 动态库；
- 建立五层 CMake target、CMake Presets、Qt Test/CTest 和 Windows Debug/Release CI；
- 完成三栏主窗口、文件树/拖放、五种主题、状态栏和预览直方图；
- 完成普通图片、四类 byte-aligned 平面 RAW 与相机 RAW 基础解码；
- 完成后台打开、取消标志、generation 保护、平移、缩放和原始像素查询；
- 合成 RAW 测试及 `CAMERA-HB-X2D-001` 产品 LibRaw 集成测试通过。
- V0.2 已通过 Pull Request #2 合并到 `main`；
- 建立不可变原始信号、DisplayMapping 与文档 revision；
- 完成 Sensor BLV / Display BLV 分离、Display WLV、Gamma 和默认值复位；
- 完成每文档独立、最多五个逻辑操作的参数撤销/重做。
- 完成 P2-03 Pixel Info：Raw/Display/RGB 查询、四种 Bayer pattern、mesh、缩放阈值与标签上限；
- 状态栏像素查询和显示预览更新均限制为约 60 Hz。
- 完成 P2-04 Bayer Extract：R/Gr/Gb/B、源 ROI、坐标互转、单通道显示和 CSV 导出接口；
- 完成 V0.4 Bayer Extract 优化：n×m 选择矩阵、行/列优先紧凑映射、部分边缘确认、自定义配置持久化和矩阵式现代 UI；
- 完成 V0.5 Recent Files：最近十个成功打开文档、完整 RAW 参数恢复、MRU 去重、缺失文件禁用和清除记录；
- 完成 V0.6 Bayer Extract：标准 Bayer、Quad Bayer、Hex Bayer 和特殊 pattern 坐标提取；固定原图全图输入并完成紧凑动态 UI；
- 完成 V0.6.1 Bayer Extract：全选恒等零拷贝、单位置快速采样、1024 有界预览、窗口双向缩放和按钮精简；
- 完成 V0.6.2 Bayer Extract：矩阵区占比提升、固定 Pattern 标题、自定义按需展开、英文排列帮助、方形紧密网格和整体窗口缩小；
- 完成 V0.6.3 Bayer Extract：18 px 无边框帮助提示、48/36/24/12 px 自适应矩阵单元并移除顶部 Source 信息；
- 完成 V0.7 RAW 像素标注：纯数值、黑白自适应字体、默认无网格、1/7 字高以及拖拽/滚轮期间暂停标注查询；
- 完成 V0.7.1 像素标注：取消交互暂停与标签上限、RAW/RGB 自动值、全可见像素覆盖、四色 Bayer 遮罩、右下角 Pattern 和精简 Pixel Info；
- 完成 V0.7.2 像素标注：启动即同步默认叠加选项、Pattern 四通道独立字体颜色、可见区域有界缓存和静态文字布局复用；
- 完成 V0.7.3 Pixel Statistics：统计源切换为当前显示 RAW 管线，选择交互改为按下—拖拽—松开，窗口改为紧凑 MODE/ANALYSIS/PLOT 技术面板；
- 完成 V0.7.4 Pixel Statistics：Bayer channels 复选后单遍扫描并显示 R/Gr/Gb/B 四结果；图表参数移入持久化 Preferences，默认线宽改为 1；
- 完成 V0.7.5 提取结果像素标注：排除多次 Extract 串接，紧凑输出按原图坐标恢复 R/Gr/Gb/B 标注，数值保持读取当前显示源；
- 完成 V0.7.6 视口综合导航：中键平移、上下像素标尺、固定全图缩略导航、底部/右侧同步滚动条和默认关闭 Bayer pattern；
- 完成 V0.7.7 Filter：当前显示 RAW 的 Mean/Gaussian/Median、3/5/7 kernel 专业紧凑子窗口，以及 1024 预览、可分离卷积、滑动中位数和有界惰性瓦片优化；
- 完成 V0.7.8 Bayer Demosaic：Bilinear、Malvar-He-Cutler、Hamilton-Adams，当前规则 Bayer 输入、专业紧凑子窗口、输入恢复和有界惰性 RGB 管线；
- 单通道使用只读 2×2 像素源视图，不复制四分之一幅完整通道数据。
- 完成 P2-05 Pixel Statistics：Status、Horizontal Box、Vertical Box、Line、WB 预留、两次点击选择、Bayer 分通道、后台进度/取消和有界图表。

## 当前进行

- 实施 P2-06 双图对比；
- 为后续瓦片缓存接入完整 revision 键；
- 补齐普通 JPG/PNG/BMP 黄金样本和更多 UI 自动化测试；
- V0.4 前再准备至少 200 MP / 400 MB 的合法性能样本。

## 下一里程碑

V0.3：完成像素信息、Bayer 提取、ROI 基础统计与双图对比闭环。

出口条件见 `docs/plans/v0.1-three-stage-implementation.md` 第 4.3 节。

## 已确认决策

| ID | 项目 | 结论 |
|---|---|---|
| D-001 | 首发平台 | Windows 10/11 x64 |
| D-002 | Qt 许可 | 开发期 LGPL 动态链接；正式发布前复审 |
| D-003 | 32-bit RAW | 同时提供 UInt32 / Float32 |
| D-004 | Horizontal/Vertical | 采用架构第 11 节定义 |
| D-005 | 性能目标 | 使用架构初始目标；200 MP/400 MB 样本延期到 V0.4 |
| D-006 | 相机 RAW | 内容探测 + 主线 LibRaw，与平面解码器分离 |

## 参考测试设备

用户指定的基准设备：

| 项目 | 配置 |
|---|---|
| 操作系统 | Windows 11 x64 |
| CPU | Intel Core Ultra 5（具体型号待补） |
| 系统内存 | 待补 |
| GPU | 型号待补，显存 16 GiB |
| 测试数据磁盘 | 待补 |

## 剩余门槛

- `CAMERA-HB-X2D-001` 只有 105.255 MP / 201.906 MiB；200 MP / 400 MB 验收样本已批准延期到 V0.4；
- 正式分发前完成 Qt 和 LibRaw 许可复审；
- 补齐 GPU 型号、系统内存和磁盘规格。

## 最近验证

| 日期 | 范围 | 结果 |
|---|---|---|
| 2026-07-30 | V0.0 文档与仓库检查 | GitHub Actions `Repository quality` 通过 |
| 2026-07-30 | V0.1 文档 | 空白字符、UTF-8、相对链接、代码块与 18 项需求追踪检查通过 |
| 2026-07-31 | `CAMERA-HB-X2D-001` | SHA-256、TIFF 签名、LibRaw 元数据、解码数组与当前设备规格已核验 |
| 2026-07-31 | V0.2 Debug | MSVC 19.30 + Qt 6.8.3 + LibRaw 0.22.2 编译成功；CTest 2/2 通过 |
| 2026-07-31 | 产品 LibRaw + `CAMERA-HB-X2D-001` | 11904×8842 UInt16 RGGB、Hasselblad X2D 100C 集成测试通过；8 项测试通过 |
| 2026-07-31 | V0.3 第一批 Debug/Release + UI | 两种配置 CTest 均 3/3 通过；BLV/WLV/Gamma、真实样本默认值、撤销与重做窗口验收通过 |
| 2026-07-31 | V0.3 Pixel Info Debug/Release + UI | 四种 Bayer pattern 与 RGB 黄金测试通过；真实样本 Raw/Display/RGB 状态栏及 5223% mesh/标签窗口验收通过 |
| 2026-08-01 | V0.3 Bayer Extract Debug/Release + UI | 两种配置 CTest 均 3/3；奇数尺寸、ROI、坐标和 CSV 黄金测试通过；11904×8842 RGGB 实图 R/B 通道尺寸、起点、源坐标与原图恢复验收通过 |
| 2026-08-16 | V0.2.1 RAW 显示修正 | Debug/Release CTest 3/3；按 11776×8842 UInt16 小端从 skip 后顺序展开为完整 Grayscale16，禁用 2048 抽样、RGB 着色和平滑插值，像素标签返回原始 UInt16；保留非阻塞加载动画 |
| 2026-08-16 | V0.3 Pixel Statistics | Debug/Release CTest 4/4；Status/Horizontal/Vertical/Line 黄金值、Bayer 分通道、取消、两次点击和 1:2:5 UI 测试通过；11776×8842 实际 RAW 全部 104,123,392 像素统计与 NumPy 黄金值一致，最终 Release decoder test 1.56 s |
| 2026-08-17 | V0.4 Bayer Extract 优化 | Debug/Release CTest 4/4；任意掩码行/列优先、奇数边缘、正反坐标、CSV 和自定义配置重载测试通过；Release 应用构建成功 |
| 2026-08-17 | V0.5 Recent Files | 最近十项 MRU、重复路径配置更新、超 2^53 偏移无损持久化、清除记录及缺失文件菜单禁用测试通过；Debug/Release CTest 4/4 |
| 2026-08-17 | V0.6 Bayer Position Extract | 2×2 四角、4×4 Quad、8×8 Hex、3×2 None pattern 黄金坐标通过；无 ROI/CSV/单元文字及动态尺寸 UI 测试通过；Debug/Release CTest 4/4 |
| 2026-08-18 | V0.6.1 Bayer Extract | 11776×8842 Release 实测：8×8 全选恒等路径 0 ms，2×2 单位置 1024 preview 15 ms；全选无补边、预览上限、2→8→2 窗口尺寸及 `Extract` 按钮测试通过 |
| 2026-08-18 | V0.6.2 Bayer Extract UI | 默认 340×375、矩阵区 150 px；Pattern 固定行高、Custom 按需展开、24 px 方形网格、英文 packing 帮助、自定义持久化 UI 测试通过 |
| 2026-08-18 | V0.6.3 Bayer Extract UI | 2×2/4×4/8×8/大尺寸自定义单元分别验证为 48/36/24/12 px；18 px 无边框帮助及 Source 标签移除测试通过 |
| 2026-08-18 | V0.7 RAW 像素标注 | 2×1 黑白 UInt16 RAW 离屏渲染验证纯数值、黑底白字/白底黑字、约 1/7 字高；拖拽和滚轮暂停期间像素查询为零增长，Debug/Release UI 测试通过 |
| 2026-08-18 | V0.7.1 连续像素标注 | 拖拽/滚轮逐帧标注、25×20 全部 500 像素无抽样、RGB 自动值、RGGB 四色遮罩与右下角 Pattern 离屏测试通过 |
| 2026-08-19 | V0.7.2 像素标注优化 | 未打开 Pixel Info 的初始标注、R/Gr/Gb/B 独立字体颜色、重复绘制与平移缩放缓存复用测试通过；Debug/Release CTest 4/4 |
| 2026-08-19 | V0.7.3 Pixel Statistics 优化 | 提取结果统计黄金值、拖拽矩形/Line、紧凑三段 UI，以及 10×10 原图提取为 5×5 后保持当前显示源的端到端测试通过；Debug/Release CTest 均 4/4 |
| 2026-08-19 | V0.7.4 Pixel Statistics 优化 | 4×4 RGGB 单遍 16 次读取产生四通道黄金值；10×10 端到端四通道各 25 样本；四面板、偏好默认值与持久化 UI 测试通过；Debug/Release CTest 均 4/4 |
| 2026-08-19 | V0.7.5 提取结果像素标注 | 连续 original→R、original→Gr 提取验证第二次值为 1/3/21/23，主窗口端到端确认 `(0,0) → Source (1,0) / Raw 1`；紧凑结果按源坐标恢复 Gr mesh/pattern；Debug/Release CTest 均 4/4 |
| 2026-08-19 | V0.7.6 视口综合导航 | 统计工具激活时中键平移不触发选择；上下标尺坐标 5 与同一像素边界对齐；缩略视野框随缩放变小，底部/右侧滚动条与源坐标同步；Bayer pattern 默认关闭；Debug/Release CTest 均 4/4 |
| 2026-08-19 | V0.7.7 Filter | Mean/Gaussian/Median 黄金值、当前显示链、取消/非法参数、1024 预览和宽图瓦片行复用通过；10×10→5×5 Bayer Extract 后 Mean 输出角点 7.333…；Debug/Release CTest 均 4/4 |
| 2026-08-19 | V0.7.8 Bayer Demosaic | 四种 Bayer 排列×三算法常量黄金值、三种脉冲系数、1024 预览、双瓦片 9,800 次读取和恢复输入通过；Mean Filter→Bilinear 端到端 RGB 37,51,61；Debug/Release CTest 均 4/4 |
| 2026-08-16 | `v0.3.0-preview.1` 发布 | Windows x64 便携包包含 Qt 6.8.3、LibRaw 0.22.2、MSVC CRT、许可证与 SHA-256；包内依赖加载和启动冒烟测试通过，以 GitHub prerelease 发布 |
| 2026-08-18 | `v0.3.0-preview.2` 发布 | 汇总 Recent Files、V0.4～V0.7.1；Debug/Release 4/4，便携包包含完整运行库、用户指南、Release Notes 和 SHA-256，以 GitHub prerelease 发布 |
| 2026-08-19 | `v0.3.0-preview.3` 发布 | 汇总 V0.7.2～V0.7.8；Debug/Release 4/4，上传完整便携 ZIP、SHA-256 和同构建 RawViewer.exe，以 GitHub prerelease 发布 |
