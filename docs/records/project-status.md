# 项目状态

- 当前版本阶段：V0.3 非破坏处理与基础分析（实施中）
- 状态日期：2026-08-01
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
| 2026-08-16 | `v0.3.0-preview.1` 发布 | Windows x64 便携包包含 Qt 6.8.3、LibRaw 0.22.2、MSVC CRT、许可证与 SHA-256；包内依赖加载和启动冒烟测试通过，以 GitHub prerelease 发布 |
