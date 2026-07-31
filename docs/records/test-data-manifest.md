# 测试数据清单

## 规则

- 只有“小型、脱敏、可再分发”的 fixture 可以提交到 `tests/fixtures/`；
- 大型或受限样本只登记，不提交 Git；
- 每个外部文件使用 SHA-256 唯一标识，不依赖文件名；
- 参数或期望结果不完整的样本不能作为验收基准；
- 删除或替换外部样本时保留历史登记和原因。

## 合成 fixture

| ID | 路径 | 生成方式 | RAW 参数 | SHA-256 | 预期结果 | 授权 |
|---|---|---|---|---|---|---|
| SYNTH-U16-LE-001 | 测试运行时临时生成 | C++ 测试生成 | 2×2 UInt16 LE；2-byte header；6-byte stride | 每次运行生成 | 1、2、4660、255 | 项目自有 |
| SYNTH-U32-BE-001 | 测试运行时临时生成 | C++ 测试生成 | 2×1 UInt32 BE | 每次运行生成 | 16909060、2147483647 | 项目自有 |
| SYNTH-F32-LE-001 | 测试运行时临时生成 | C++ 测试生成 | 2×1 Float32 LE | 每次运行生成 | 1.0、2.5 | 项目自有 |
| SYNTH-TRUNC-001 | 测试运行时临时生成 | C++ 测试生成 | 2×2 UInt16，实际 3 bytes | 每次运行生成 | `raw.file_truncated` | 项目自有 |

当前测试还应继续补充：

- 4×4 UInt8 RGGB 的独立黄金文件；
- 普通 JPG、PNG、BMP 黄金文件；
- 大端 UInt16；
- NaN/Inf Float32；
- UI 拖放与坐标锚点自动化。

## 外部/大型样本

| ID | 受控位置标识 | 文件大小 | RAW 参数 | SHA-256 | 来源与授权 | 用途 | 负责人 |
|---|---|---:|---|---|---|---|---|
| CAMERA-HB-X2D-001 | `Data/Test_data/B0012535.raw`（本地，不入 Git） | 211,714,048 bytes | TIFF 相机 RAW；11904×8842 UInt16；RGGB | `3708AF7CBA70EC5A3523DBF969943987A2FBCE2C5B17AE80CFF0414D81F16CFF` | 用户允许本项目测试；未确认再分发 | LibRaw 集成、约 100 MP 基准 | Power-Z |
| PERF-200MP-001 | 延期准备 | 至少约 400 MB | 至少 200 MP | 待填写 | 待填写 | V0.4 容量与性能验收；不阻塞 V0.2 | 待填写 |

> “受控位置标识”只写存储系统中的非敏感 ID，不记录密码、令牌或可公开访问链接。

## CAMERA-HB-X2D-001 核验记录

- 核验日期：2026-07-31；
- 文件签名：`49 49 2A 00`，little-endian TIFF；
- 相机：Hasselblad X2D 100C；
- 文件大小：211,714,048 bytes（201.906 MiB）；
- RAW 数组：11904×8842，105,255,168 pixels（105.255 MP）；
- 解码类型：UInt16；
- 解码数组 stride：23,808 bytes；
- 解码数组大小：210,510,336 bytes；
- 活动区：11664×8750，left margin 124，top margin 92；
- Bayer：RGGB；
- 黑电平：4094 / 4093 / 4093 / 4094；
- 白电平：65535；
- 缩略图：3888×2918；
- 数组抽查：min=0，max=65535，mean=13032.235；
- 核验工具：rawpy 0.26.1 / LibRaw 0.22.0；dcraw 9.27 交叉检查。
- 产品集成复核：Raw Viewer Debug + 官方 LibRaw 0.22.2 Win64，`DecoderTest::verifiesApprovedCameraSampleWhenConfigured` 通过；V0.3 额外断言平均 Sensor BLV 4093.5 与 WLV 65535，并完成显示默认值窗口验收。

若按用户提供的 11776×8842×16-bit 计算，文件大小差为 3,467,264 bytes；若按 LibRaw 报告的 11904×8842×16-bit 计算，差为 1,203,712 bytes。这些差值包含 TIFF IFD、缩略图、元数据及可能的容器布局开销，均不能当作平面 RAW 的 `headerBytes`。

用户提供的 width=11776 与容器元数据不一致。该文件不存在可供平面解码器使用的单一 `headerBytes` 或物理 `rowStride`；必须走相机 RAW 容器解码路径。解码后的逻辑类型为 UInt16，逻辑数组 stride 为 23,808 bytes。

当前 rawpy 检查环境包含额外 GPL demosaic packs，仅用于本地核验。产品依赖必须使用不含这些扩展包的主线 LibRaw 构建。
