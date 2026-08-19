# Raw Viewer 用户指南

适用版本：`v0.3.0-preview.4`，Windows 10/11 x64。

## 1. 获取与运行

1. 从 [GitHub Releases](https://github.com/Power-Z/Raw_Viewer/releases) 下载 `RawViewer-v0.3.0-preview.4-windows-x64.zip` 及同名 `.sha256` 文件。
2. 在 PowerShell 中校验：

   ```powershell
   Get-FileHash .\RawViewer-v0.3.0-preview.4-windows-x64.zip -Algorithm SHA256
   ```

   输出应与 `.sha256` 文件一致。
3. 将 ZIP 完整解压到可写目录，不要只从压缩包内直接运行 exe。
4. 运行 `RawViewer.exe`。这是未签名 Preview，Windows SmartScreen 可能显示提示。

便携包已经包含 Qt 6.8.3、LibRaw 0.22.2、MSVC x64 Runtime、平台插件和第三方许可证，不需要单独安装这些运行库。

## 2. 打开图像

支持三种入口：

- `File` 菜单选择文件；
- 左侧文件树双击；
- 将单个文件拖入中央视口。

### 2.1 普通图片

JPG、PNG、BMP 使用文件自身的尺寸与 RGB 数据，无需填写 RAW 参数。

### 2.2 平面 RAW/BIN

`.raw`、`.bin` 文件没有统一的自描述头，必须在左侧设置与数据一致的参数：

- `Width` / `Height`：图像宽高；
- `Skip bytes`：首个像素之前跳过的字节数；
- `Scalar type`：UInt8、UInt16、UInt32 或 Float32；
- `Byte order`：Little endian 或 Big endian；
- `Row stride`：每行实际字节跨度；为 0 时按紧密行计算；
- `Bayer pattern`：None、RGGB、BGGR、GRBG 或 GBRG。

读取顺序固定为：跳过 `Skip bytes`，从第一个样本开始按行从左到右、从上到下组成 `Width × Height` 图像。平面 UInt16 RAW 使用单通道 Grayscale16 显示，不执行 RGB 着色、旋转或 Bayer demosaic。

默认平面 RAW 参数为 11776×8842、UInt16、Little endian、Skip=0。参数不匹配时程序会拒绝截断数据；文件尾部超过所需图像范围的数据不会显示。

### 2.3 相机 RAW 容器

TIFF/DNG/厂商 RAW 先按内容签名识别，再由 LibRaw 读取尺寸、数据偏移与 Bayer 元数据；不会仅因扩展名为 `.raw` 就猜测平面布局。

## 3. 最近文件

`File > Recent Files` 保存最近 10 个成功打开的文档，并保存当时使用的完整平面 RAW 参数：

- 再次选择时按原配置加载；
- 重复文件移动到列表顶部；
- 已删除文件会标记并禁用；
- 菜单底部可清除记录。

最近文件配置保存在当前 Windows 用户设置中，不写回原图。

## 4. 浏览与显示

- 左键拖拽：未启用区域选择工具时平移图像；
- 中键拖拽：始终平移图像，Pixel Statistics 等工具占用左键时也可使用；
- 鼠标滚轮：以鼠标位置为锚点缩放；
- 顶部和左侧标尺：分别显示与当前图像像素边界严格对齐的 X/Y 坐标，颜色跟随当前主题；
- 右上角缩略图：固定显示完整图像，橙色矩形表示当前可见区域；
- 底部和右侧滚动条：图像超出显示区域时启用，可拖动到其他位置；
- 打开新图时：自动适合窗口；
- 状态栏：显示坐标、Raw/Display/RGB 信息、Bayer 通道和缩放比例。

右上角直方图读取当前显示源的全分辨率像素：规则 Bayer RAW 自动叠加 R/Gr/Gb/B 四条曲线，RGB 自动显示 R/G/B/Y，非规则 Bayer 提取或单通道 RAW 显示一条灰度曲线。统计在后台执行，切换图像会取消旧任务；修改显示参数不会重复扫描全图。

Display BLV 和 Display WLV 保留一位小数，右侧输入框与直方图横轴上的蓝色/白色双手柄双向同步。拖动期间只预览手柄和数字，松开后才一次刷新图像并形成一个撤销步骤；输入框在停止输入约 120 ms 或失焦后应用。点击 `🔍 BLV–WLV` 会以当前两点为横轴范围进入 `WINDOW DETAIL` 精调，再次点击恢复完整范围。RAW 默认 Gamma 为 1.0：小于等于 BLV 为全黑，大于等于 WLV 为全白，中间值线性显示；修改 Gamma 后按对应幂函数显示。这些操作只影响显示映射，原始文件与原始像素源保持只读。

### 编辑和全局撤销

`编辑` 菜单提供以下几何操作：

- `Flip`（上下翻转）：`Ctrl+Alt+V`；
- `Mirror`（左右翻转）：`Ctrl+Alt+H`；
- `Rotate Left`：`Ctrl+Alt+Left`；
- `Rotate Right`：`Ctrl+Alt+Right`；
- `Rotate 180`：`Ctrl+Alt+Down`。

Undo/Redo 使用标准 `Ctrl+Z` / `Ctrl+Y`，每个文档保存最近五个逻辑编辑步骤。历史覆盖 Display BLV/WLV/Gamma、恢复显示默认值、上述几何操作、Bayer Extract/Show Original、Filter 和 Demosaic/Restore Source。撤销后会同步刷新视口、直方图、像素坐标、工具状态；统计窗口可见且 ROI 仍有效时会自动基于恢复后的图像重新计算。

平移、缩放、统计选区、主题、工具偏好和 CSV 导出不修改图像处理管线，因此不占用五步历史。打开新文件会创建该文档自己的空历史。

## 5. Pixel Info

像素值标注在应用启动后默认启用，不需要先打开 Pixel Info 窗口；Bayer mesh 和 Bayer pattern 默认关闭。Pixel Info 仅包含三个选项：

1. `像素值标注`：RAW 自动显示原始值，普通图片和 Demosaic/ISP RGB 输出显示三行 `R n`、`G n`、`B n`；
2. `Bayer mesh`：根据 Bayer pattern 添加半透明 R、Gr、Gb、B 遮罩，Gr/Gb 使用不同绿色；
3. `Bayer pattern`：在每个像素右下角显示 `R/Gr/Gb/B`；四种通道使用不同字体颜色，Gr/Gb 分别使用亮绿和青绿色系。

单个源像素显示高度达到 40 px 后自动出现叠加层。RAW 数值位于像素中央；JPG/PNG/BMP 和 Demosaic 等 RGB 图像的三行数值位于像素左下角。数值字高约为像素高度的 1/6；Pattern 位于右下角，字高约为 1/7。数值根据当前显示灰度自动选择黑字或白字，Pattern 颜色也会按背景明暗选择深色或亮色变体。达到阈值后所有可见像素都会标注，并随拖拽和缩放逐帧移动。连续打开另一张图时会同步清除旧图拖拽锚点、选择、缩放、偏移和坐标状态。程序只缓存当前可见区域附近的标签数据，不会为整幅图建立副本。

Pixel Info 子窗口右侧提供 4×4 RGGB 示例。切换像素值、Bayer mesh 或 Bayer pattern 时示例会立即更新，可在应用到大图前确认组合效果；窗口使用普通直角区域，不使用圆角卡片。

## 6. Bayer Extract

Bayer Extract 从当前文档的原始全图中周期性提取指定 pattern 位置，并重新组合为新的只读灰度视图。每次 Extract 都以 original source 为输入，不会基于上一次提取结果继续处理。

### 6.1 Pattern

- 预设：2×2 标准 Bayer、4×4 Quad Bayer、8×8 Hex Bayer；
- `Custom`：支持 1×1～16×16，自定义名称、宽高和位置；
- 点击方块切换保留/忽略；`Select all`、`Clear`、`Invert` 批量操作；
- 自定义配置可以保存、重新加载和删除。

矩阵顶部为 X 坐标，左侧为 Y 坐标。方块大小随 pattern 自动缩放：2×2 为 48 px、4×4 为 36 px、8×8 为 24 px、大尺寸自定义 pattern 为 12 px。

### 6.2 Packing

- `Row-major`：选中位置按从左到右、再从上到下排列；
- `Column-major`：选中位置按从上到下、再从左到右排列；
- `?` 按钮显示说明。

源宽高不是 pattern 整数倍时，Extract 前会确认是否处理右侧/底部的部分单元。全选所有位置属于恒等操作，结果严格等于原图并复用原始只读数据；非全选使用最长边 1024 的有界信号预览。提取结果是独立的紧凑坐标空间：例如 200×200 图像按 2×2 只保留左上位置时，结果及状态栏坐标均为 100×100，不显示原图坐标。放大到 16 px/像素后，视口会对可见区域使用最多 65,536 样本的精确缓存，使背景像素与数值标注严格一致，同时限制绘制线程负载。

## 7. Pixel Statistics

支持：

- `Status`：矩形区域 count/min/max/mean/population std 和直方图；
- `Horizontal Box`：矩形区域按水平方向生成平均 profile；
- `Vertical Box`：矩形区域按垂直方向生成平均 profile；
- `Line`：拖拽确定线段并生成沿线 profile；
- `WB`：当前版本仅预留入口。

矩形和线段均使用左键按下、拖拽、松开完成。统计对象是当前显示的 RAW 管线结果：例如先执行 Bayer Extract，再打开 Pixel Statistics，会统计提取后的坐标和值，不会自动恢复原图。后台流式读取只读像素源，支持进度、取消和过期任务丢弃，不复制完整 RAW。

`Bayer channels` 默认关闭，此时显示一个 All 结果。规则 Bayer 排列下可勾选该项，底部会切换为 R/Gr/Gb/B 四个独立结果面板；四通道计算只扫描 ROI 一次。非恒等 Bayer Extract 会改变排列，此时该选项不可用，避免按重新排列后的坐标错误推断 Bayer 通道。

图表参数位于主菜单 `偏好 → Pixel Statistics…`，包括 Line width、Histogram bins、Data points、Grid 和 Histogram fill。默认线宽为 1 px，设置会在下次启动时保留。

统计窗口采用直角紧凑布局，模式、指标和图表区不再重复显示英文区段标题；计算状态只显示在进度条内部。图表的数据线在任意主题下均为纯黑色，X/Y 轴围成的绘图区固定为 RGB(207,207,207)。鼠标移动到距曲线或直方图顶部约 14 px 内时，会自动吸附最近数据点并显示连接 X/Y 轴的横纵引导线。

## 8. Filter

从 Tool 菜单或右侧工具区打开 Filter。子窗口提供：

- `Gaussian`：高斯平滑，可设置 0.10～10.0 的 Sigma；
- `Median`：精确中值滤波，适合脉冲噪声；
- `Mean`：均值平滑；
- Kernel：3×3、5×5 或 7×7。

Apply 的输入是中央 RAW 区域当时正在显示的图像，而不是固定原图。例如 Bayer Extract 后再 Apply，会滤波提取结果；再次 Apply 会继续处理上一次 Filter 输出。切换回 Bayer Extract 的 Original 可重新从原图开始。所有输入保持只读。

Filter 只立即生成最长边 1024 的显示预览；准确像素在 Pixel Info、放大标注或 Pixel Statistics 读取时按 64×64 瓦片计算并有界缓存，不会创建完整 W×H 滤波副本。Mean/Gaussian 使用可分离卷积，Median 使用滑动窗口。边缘像素按 clamp-to-edge 处理。

## 9. Demosaic

Demosaic 只在当前显示源具有规则 RGGB、BGGR、GRBG 或 GBRG 排列时可用。Filter 会保留 Bayer 排列，因此可以先降噪再 Demosaic；重新排列像素的非恒等 Bayer Extract 不再是规则 CFA，Demosaic 会禁用。

三种方法：

- `Malvar-He-Cutler`：默认推荐，固定 5×5 线性颜色修正，质量和 CPU 成本平衡；
- `Hamilton-Adams`：根据水平/垂直梯度选择插值方向，适合明显边缘；
- `Bilinear`：最快的基础插值，适合快速预览和对照。

Apply 使用当时的 Display BLV、Display WLV 和 Gamma 将插值值映射为 RGB。输出只生成最长边 1024 的立即预览，准确 RGB 像素按 64×64 瓦片惰性生成。Demosaic 后 Filter、Pixel Statistics 和再次 Demosaic 会禁用；点击 `Show Bayer Source` 可恢复本次输入并选择另一算法。

当前 Demosaic 只完成 CFA 颜色插值，不包含白平衡、相机颜色矩阵、色域转换、降噪、锐化或完整 tone mapping，因此不等同于相机 JPEG 成片。

## 10. 已知限制

- Preview 便携包未签名，也未提供安装器；
- 尚未在独立干净 Windows 虚拟机完成正式验收；
- 10/12/14-bit packed RAW 尚不支持；
- 200 MP / 400 MB 正式性能目标、GPU 瓦片渲染和双图对比仍在后续计划；
- WB 统计尚未实施；
- Pixel Info 在 40 px/像素以上标注全部可见像素，极大窗口下的性能仍取决于 CPU 与显卡驱动。
- 当前 Filter 只支持标量 RAW 管线，不处理 JPG/PNG/BMP RGB 图像；kernel 最大为 7×7，尚未使用 GPU。
- Demosaic 仅支持四种规则 2×2 Bayer CFA；Quad Bayer、Hex、自定义重排和 RGB 输入不适用，当前输出为显示映射后的 8-bit RGB。

## 11. 故障排查

- RAW 画面错行：检查 Width、Height、Skip、Scalar type、Byte order 和 Row stride；
- RAW 无法打开：确认 `skip + stride × height` 不超过文件大小；
- 相机 RAW 被当作平面数据：保留原始容器内容，不要仅修改扩展名；
- 程序提示缺少 DLL：确认完整解压 ZIP，且 `platforms/qwindows.dll`、Qt DLL、`libraw.dll` 与 MSVC Runtime 未被删除；
- 最近文件不可用：原文件可能被移动或删除，请从新位置重新打开。

报告问题时请提供应用版本、Windows 版本、RAW 参数、可复现步骤和错误信息。不要公开上传客户 RAW、个人图像、访问令牌或其他未授权数据。
