# 第三方组件说明

Capricorn V129 当前源码与发布流程直接依赖：

- Qt 6：桌面前端、网络、SVG 与 SQLite 支持。最终分发时应依据所选 Qt 许可方式履行对应许可义务。
- Go：Go Core 的构建工具与标准库。
- Windows 系统 API：WinHTTP、WinMM、Ole32、UUID 等系统组件。
- Inno Setup 7：仅作为开发者本地生成 `Setup-x64.exe` 的安装器构建工具；其编译器本体不会被打包进 Capricorn 的运行目录。

V129 已移除 Python Worker 与 PyInstaller，因此它们不再属于 Capricorn 当前运行或构建依赖。

默认桌宠 SVG 由项目方提供并随软件发布；正式分发前应由项目方确认所有形象素材的使用与再分发权利。

本文件不是法律意见；正式发布前仍应依据实际分发方式核对完整许可证要求。
