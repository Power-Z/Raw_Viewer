# ADR-0003：相机 RAW 容器与平面 RAW 使用独立解码路径

- 状态：已接受
- 日期：2026-07-31
- 决策者：Power-Z
- 关联需求：DATA-003、DATA-004、DATA-006

## 背景

用户提供的 `B0012535.raw` 扩展名看似平面 RAW，但文件以 little-endian TIFF 签名开头。文件大小也不能由用户提供的 11776×8842×16-bit 加一个合理固定头精确解释。

LibRaw 0.22.0 探测结果为 Hasselblad X2D 100C，相机 RAW 数组 11904×8842 UInt16，活动区 11664×8750，并包含缩略图、边距、Bayer、黑白电平和方向等容器元数据。

## 候选方案

### 方案 A：所有 `.raw/.bin` 都按 header + stride 解释

- 优点：实现简单。
- 缺点：依赖扩展名，无法正确解析 TIFF/DNG/厂商 RAW。
- 风险：错误尺寸会产生错图、越界或大额错误分配。

### 方案 B：仅使用 LibRaw

- 优点：覆盖大量相机 RAW。
- 缺点：不能表达无容器的工程平面 RAW、自定义 head/stride/UInt32/Float32。
- 风险：设备导出的简单二进制文件无法打开。

### 方案 C：内容探测后分流

- 优点：相机容器和工程平面 RAW 各自使用正确模型；扩展名错误也能处理。
- 缺点：需要两个解码器、统一元数据模型和更多集成测试。

## 决策

采用方案 C：

- `ImageFormatProbe` 先检查文件内容签名，再参考扩展名；
- TIFF/DNG/厂商相机 RAW 使用主线 LibRaw；
- 无容器的 `.raw/.bin` 使用 `FlatBinaryRawDecoder`；
- 相机容器的 strip/tile/压缩布局不映射为 `headerBytes/rowStride`；
- 两条路径都输出不可变原始源和统一的 Bayer/像素访问端口；
- 生产构建不启用 LibRaw GPL demosaic packs；
- LibRaw 使用动态库并纳入第三方许可清单。

## 后果

### 正面影响

- 当前 Hasselblad 样本可以按真实元数据打开；
- 自定义平面 UInt8/16/32 RAW 能继续使用显式参数；
- 文件扩展名不再成为数据安全边界。

### 负面影响与债务

- 需要维护 LibRaw 版本和相机回归样本；
- 相机 RAW 的随机瓦片访问能力取决于具体格式和 LibRaw；
- 当前样本只能本地测试，尚未获得再分发授权，CI 仍需合成/可分发样本。

## 验证与复审

第一阶段至少验证：

- `B0012535.raw` 被识别为 Hasselblad X2D 100C；
- RAW 数组为 11904×8842 UInt16，Bayer RGGB；
- 平面 `.raw` fixture 不被错误送入 LibRaw；
- 扩展名与内容冲突时以可信内容探测结果为准；
- LibRaw 升级后元数据和黄金像素回归一致。
