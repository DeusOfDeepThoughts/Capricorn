# Capricorn v1.0.0 变更说明

v1.0.0 基于 V128，并将当前产品版本和发布命名统一为语义化版本格式；不改变 Capricorn 的 UI、人格、聊天、语音或桌宠行为。

## 一次编译同时生成两种发行物

执行 `BUILD_v1.0.0.cmd` 后，同一份经过校验的 staging 目录会同时生成：

- `Capricorn-v1.0.0-Windows-x64.zip`
- `Capricorn-v1.0.0-Setup-x64.exe`

便携 ZIP 与安装版 Setup.exe 因此共享完全一致的运行时文件。

## 安装器

`installer/Capricorn.iss` 采用 Inno Setup：

- 可选择安装目录
- 当前用户安装，默认无需管理员权限
- 开始菜单快捷方式
- 可选桌面快捷方式
- 标准卸载
- 固定 AppId，支持未来版本覆盖升级
- 升级仅清理 Capricorn 的旧版本主 EXE 和 Go Core EXE
- 不碰用户 AppData 中的模型配置、语音配置、人格、聊天记录和长期记忆

## 构建依赖

构建脚本优先使用 Inno Setup 7 x64，若本机未安装，默认通过 winget 启动官方 Inno Setup 7 x64 安装流程。

Inno Setup 仅是开发者构建工具，不进入 Capricorn 的最终运行目录，因此不会增加终端用户的运行依赖。
