# BLPFormat2

Windows x64 Photoshop BLP 文件格式插件。打开 Warcraft BLP1 贴图，在 Photoshop 的保存格式列表中以 **BLP1 Warcraft Texture** 导出。

2.0.3 将保存时的逐行、逐通道像素请求改为分块传输，普通 RGB(A) 一次接收多个通道，独立 Alpha 按需单独传输。保留中文修复、持久化设置和分块读取；导出不弹窗。

## 安装与使用

1. 退出 Photoshop。将发布包里的 `BLPFormat2.8bi` 复制到 Photoshop 安装目录的 `Plug-ins` 文件夹；建议建立 `File Formats` 子文件夹。
2. 如果已经装过旧版 BLP 插件，先将旧 `.8bi` 移到插件目录之外备份，避免同一扩展名被多个插件接管。
3. 重启 Photoshop，用“打开”载入 `.blp`。导出前将文档设为 **RGB 颜色 / 8 位通道**。
4. 在“存储为”或“存储副本”的格式列表中选择 **BLP1 Warcraft Texture**。Photoshop 的版本及文档图层状态会影响格式所在菜单。
5. 正常保存直接使用已配置的 JPEG 质量、Mipmap Count 和 Alpha 来源，不再弹出选项窗口。

修改设置：在 Photoshop 的“帮助 → 关于增效工具 → BLP1 Warcraft Texture”中打开设置窗口；也可双击发布包内的 `BLPFormat2Settings.exe`。两者共享同一套设置，点击“保存设置”后用于后续导出。独立设置程序可放在 Photoshop 安装目录根目录。

仅依赖 Windows 系统 DLL，JPEG 库和 C++ 运行库静态链接。**当前已通过 Windows x64 Release 构建、实际 DLL 加载与模拟 Photoshop 接口测试；尚未在真实 Photoshop 或 Warcraft 游戏中完成验收。** 不声称支持某个未经测试的 Photoshop 版本；ARM64 和 macOS 不在此版本范围内。

## 导出选项

| 选项 | 含义 |
| --- | --- |
| JPEG 质量 | 1–100，越高细节越多、通常文件越大。默认 85。质量数值不是文件压缩百分比，100 也不是无损。 |
| Mipmap Count | 1–16，默认 16，包含原图。导出时自动限制到图像实际可用的最高层数，例如 512×512 的 16 会生成 10 层，1024×1024 会生成 11 层。1 表示只有原图。 |
| 自动 Alpha | 优先首个独立 Alpha 通道，其次文档透明度；没有则完全不透明。 |
| 文档透明度 | 使用图层合成后的透明度，忽略独立 Alpha 通道。没有透明度则完全不透明。 |
| 不保存 Alpha | 强制完全不透明。 |

设置保存在 `HKCU\Software\BLPFormat2`，关闭或取消设置窗口不会写入。导出及动作回放不会修改偏好；“恢复默认”恢复质量 85、Mipmap Count 16、自动 Alpha，再点击“保存设置”即可生效。旧版的 0（完整链）会自动迁移为 16，已保存的质量和 Alpha 选项保留。

## 格式范围

- 读入：BLP1 JPEG、调色板，Alpha 0 / 1 / 4 / 8 位，复用用户指定的 `war3_preview_core` 解码器快照。
- 写出：BLP1 JPEG，四分量原始 BGRA，无 Adobe/JFIF 标记，共享 JPEG 头长度为 0，每层独立 JPEG。Alpha 与颜色一起参与 JPEG 有损压缩。
- mipmap 的剩余表项为 0；只有原图时 `hasMipmaps=0`。最多 16 个表项，实际层数由尺寸决定。
- 尺寸 1–16384；支持矩形和奇数尺寸。引擎对贴图尺寸的要求仍需在目标游戏中验证。
- 使用面积缩小及 Alpha 加权处理透明边缘；全透明区域仍保留 RGB。
- 全零或全不透明的已声明 Alpha 在打开时保留为独立通道，便于编辑遮罩/队伍色。
- 不保存 Photoshop 图层、ICC 和其他文档资源。保留 PSD 作为编辑源文件。
- 不支持 BLP0、BLP2、DXT 写出、调色板写出、16/32 位通道或 CMYK 文档导出。

4 位 Alpha 的读取严格跟随指定核心的高半字节优先规则，并有回归测试。它是本项目选择的兼容基准，未将其他解码器的规则混入此实现。

## 构建

在 Visual Studio 的 **x64 Native Tools / Developer PowerShell** 中运行，需要 C++ 桌面工具、Windows SDK、CMake 3.24+ 和 Ninja：

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

输出：`build/BLPFormat2.8bi` 和 `build/BLPFormat2Settings.exe`。独立设置程序使用同一套 UI 和持久化逻辑，保存后无需重启 Photoshop 即可用于下一次导出；替换插件本身仍需重启 Photoshop。

打包：`powershell -ExecutionPolicy Bypass -File tools/Package.ps1`。脚本仅在当前项目生成 `dist/BLPFormat2-2.0.3-win-x64.zip`，不安装插件。

## 代码结构

- `src/BlpCodec.*`：BLP1 编码、JPEG 错误边界、mipmap 缩小。
- `src/Plugin.cpp`：Photoshop selector、分块像素交换、Alpha 选择、动作参数、文件 I/O。
- `src/ExportDialog.*` 与 `resources/Dialog.rc`：原生中文设置窗口与偏好。
- `third_party/reference`：未经修改的指定解码核心与 stb_image。
- `third_party/jpeg`：IJG JPEG 9f。
- `third_party/sdk`、`tools/Cnvtpipl.exe`：老插件提供的 Adobe SDK 头文件与资源编译工具。

第三方组件各自保留原始许可，详见 [THIRD_PARTY.md](THIRD_PARTY.md)。本项目不需要旧仓库在原路径继续存在。

## 动作参数

类 ID `Blp2`；可记录并回放整数参数：`JpQl`（质量 1–100）、`MpLv`（1–16 总层数，兼容旧动作的 0）、`AlSr`（0 自动，1 透明度，2 不透明）。动作中明确记录的参数优先于全局设置；未记录的参数使用全局设置。正常保存与动作回放均不弹出设置窗口，非法值返回错误。模拟宿主已验证参数传入；实际 Photoshop 动作录制/回放仍需人工验收。

测试细节见 [docs/VALIDATION.md](docs/VALIDATION.md)。Adobe 对新增文件格式建议使用 C++ SDK，参考 [官方插件说明](https://developer.adobe.com/photoshop/uxp/guides/hybrid-plugins)。
