# 第三方来源

本项目自有代码沿用根目录 MIT 许可证；以下组件不因此重新授权。

| 组件 | 来源 | 说明 |
| --- | --- | --- |
| Photoshop SDK 头文件与 Cnvtpipl.exe | BLPFormat 原工程的 `photoshopapi/`、`resources/` | Adobe SDK，保留头文件中的原始声明；受 Adobe SDK 对应许可约束。未修改头文件。资源编译工具沿用原仓库已跟踪的文件。 |
| IJG JPEG 9f | BLPFormat 原工程的 `ThirdParty/jpeg/jpeg-9f/` | 保留所用 C 文件与对应 9f 头文件；构建配置来自旧插件。许可见 `third_party/jpeg/README`。This software is based in part on the work of the Independent JPEG Group. |
| BLP 解码核心 | `war3_preview_core` 工程 | 按用户指定使用，`BlpDecoder.cpp`、`parser/BlpDecoder.h` 原样复制。原仓库未提供根许可证；独立对外再分发源码前应明确其授权。 |
| stb_image | 指定核心的 `include/stb_image.h` | 保留文件尾部 MIT / public domain 许可。 |

解码实现快照日期：2026-09-06。
`BlpDecoder.cpp` SHA-256：`6259FFBC776417574CC1DFD6C191787CDC4BB433490E4C1EF9DA80EC5728574C`。

真实测试样本来自本机既有项目，仅用于验证，未打入发布包。
