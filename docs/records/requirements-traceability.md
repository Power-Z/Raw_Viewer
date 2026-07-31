# 需求追踪矩阵

来源：`require/Raw_Viewer.md`

状态值：`Defined`、`Planned`、`Implemented`、`Verified`、`Deferred`。

| ID | 需求摘要 | 设计位置 | 计划 | 状态 |
|---|---|---|---|---|
| UI-001 | 顶部主菜单 | 架构 6；`src/presentation/main_window.cpp` | P1-02 | Implemented |
| UI-002 | 底部状态栏显示坐标、信号和图片信息 | 架构 6、10；`MainWindow::updateCoordinate` | P1-02/P1-05 | Implemented |
| UI-003 | 左 20% / 中 60% / 右 20% 三栏与内部区域 | 架构 6；`MainWindow` splitter | P1-02 | Implemented |
| UI-004 | Dark/Light/Gray/Yellow/Red 主题 | 架构 6；`ThemeManager` | P1-02 | Implemented |
| UI-005 | 新版本说明与 About | 架构 6；帮助菜单 | P1-02 | Implemented |
| DATA-001 | 拖拽或文件树打开 | 架构 6、10；`ImageViewport` / `MainWindow` | P1-03 | Implemented |
| DATA-002 | JPG/BMP/PNG 直接打开 | 架构 5.3、7；`QtImageDecoder` | P1-03 | Implemented |
| DATA-003 | 平面 RAW/BIN 按参数打开；相机 RAW 按内容探测 | 架构 7.2～7.4、ADR-0003；decoder tests | P1-04/P1-04B | Verified |
| DATA-004 | 原始/中间/显示三层且原始不变 | 架构 7.1、ADR-0002；`DocumentSession` + `PreviewRenderer`；`DocumentSessionTest::rendersWithoutChangingOriginal` | P2-01 | Verified |
| DATA-005 | 基础 ISP 与五步撤销 | 架构 9、12；`DisplayMapping` + `DocumentSession`；application/domain tests | P2-01/P2-02 | Implemented |
| DATA-006 | 200 MP / 400 MB 大图 | 架构 8、ADR-0002/0003 | P3-01～P3-03 | Planned |
| VIEW-001 | 无工具时左键拖动、滚轮鼠标锚点缩放 | 架构 10.1；`ImageViewport` | P1-05 | Implemented |
| VIEW-002 | 多图同步，Alt+左键单独拖动 | 架构 10.2 | P2-06 | Planned |
| VIEW-003 | 放大后标注 RAW 信号或 RGB | 架构 8.4、10.1 | P2-03 | Planned |
| TOOL-001 | Pixel Info 信号、mesh、pattern 标注 | 架构 10.3、12 | P2-03 | Planned |
| TOOL-002 | Pixel Statistics 四种模式 | 架构 11 | P2-05/P3-04 | Planned |
| TOOL-003 | Bayer pattern 特定像素提取 | 架构 12 | P2-04 | Planned |
| TOOL-004 | 预留复杂 ISP 工具 | 架构 12 | P2-01，持续 | Planned |

## 追踪规则

- 实现 PR 必须引用一个或多个需求 ID；
- 状态改为 `Implemented` 时必须链接实现路径；
- 状态改为 `Verified` 时必须给出测试名、样本 ID 和结果；
- 需求变更不得直接覆盖旧含义，应记录变更日期并评估已有测试；
- `Deferred` 必须写明推迟到哪个里程碑及原因。
