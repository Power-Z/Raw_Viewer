# Changelog

本项目的重要变更记录在此文件中，格式参考 Keep a Changelog，版本号计划遵循语义化版本。

## [Unreleased]

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
- 增加不复制完整通道数据的只读 2×2 像素源视图、通道/源坐标互转和约 3 MP 有界预览。
- 增加原子 CSV 导出接口，记录通道坐标、源坐标和原始信号值。
- 增加奇数尺寸、非对齐 ROI、空通道、取消、坐标溢出和 CSV 黄金测试。
