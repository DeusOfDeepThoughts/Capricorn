# Capricorn-v1.0.0 一键编译与双发布

## 环境要求

- Windows 10/11 x64
- Qt 6.8+，包含 Qt Widgets、Network、Svg、Sql 及对应 MinGW/MSVC 工具链
- CMake 3.24+
- Go 1.23+
- Inno Setup 7 x64（用于生成安装版；脚本会自动检测）

v1.0.0 仍保持精简运行架构：

`Qt/C++ 前端 → Go Core`

运行 Capricorn 本身不需要 Inno Setup。Inno Setup **只在开发者本地构建 Setup.exe 时使用**，不会被打包进用户运行目录。

## 一键编译

解压源码后双击根目录：

`BUILD_v1.0.0.cmd`

也可以手工运行：

```powershell
.\scripts\build-v1.0.0.ps1 -QtRoot "C:\Qt\6.11.1\mingw_64"
```

脚本只编译一次 Qt 与 Go，然后从同一个经过校验的 staging 目录同时生成：

- `Capricorn-v1.0.0-Windows-x64.zip` —— 便携版，用户解压后直接运行
- `Capricorn-v1.0.0-Setup-x64.exe` —— 安装版，用户双击后选择安装目录
- `Capricorn-v1.0.0-Windows-x64\` —— 当源码路径不太深时额外保留的展开运行目录

这样 ZIP 与 Setup.exe 使用完全相同的 Qt DLL、Go Core、插件和 50 张桌宠 SVG，不会出现两个发行物内容不一致的问题。

## Inno Setup 自动处理

构建脚本会依次检测：

1. 手工传入的 `-InnoSetupCompiler`
2. PATH 中的 `ISCC.exe`
3. 常见的 Inno Setup 7 安装目录

如果都没有找到，默认会调用 Windows `winget` 打开官方 **Inno Setup 7 x64** 安装流程。安装只需第一次进行；之后再次编译会直接复用 `ISCC.exe`。

如果不允许脚本自动调用 winget，可使用：

```powershell
.\scripts\build-v1.0.0.ps1 -NoAutoInstallInno
```

也可以直接指定：

```powershell
.\scripts\build-v1.0.0.ps1 -InnoSetupCompiler "C:\Program Files\Inno Setup 7\ISCC.exe"
```

## 安装版行为

`Capricorn-v1.0.0-Setup-x64.exe`：

- 默认采用 Inno Setup 的 `{autopf}\Capricorn`：当前用户安装时映射到用户程序目录，选择管理员安装时映射到系统 Program Files
- 安装向导允许用户修改安装目录
- 自动创建开始菜单快捷方式
- 可选创建桌面快捷方式
- 安装完成后可直接启动 Capricorn
- 在 Windows“已安装的应用”中提供正常卸载
- 使用固定 AppId，为以后版本覆盖升级保留稳定的安装身份
- 升级时只清理 Capricorn 自己的旧版本主 EXE / Core EXE，不使用危险的 `{app}\*` 全目录删除
- **不会删除或打包用户的模型 API、语音配置、人格、聊天记录或长期记忆**

用户运行数据仍由 Capricorn 保存到用户自己的 AppData 数据目录，安装/覆盖升级与卸载程序默认都不会删除这些个人数据。

## 发布目录说明

默认桌宠 50 张 SVG 继续作为外部资源发布在：

`assets\avatars`

它们不会嵌入 QRC，以避免生成巨大的 `qrc_resources.cpp` 导致 MinGW 编译器内存溢出。

## 代码签名

v1.0.0 的构建流程已经为 Setup.exe 留好标准安装器结构，但源码包**不包含任何开发者证书或私钥**。如果正式公开发布，后续可在构建链中另外接入 Authenticode 代码签名；没有签名时 Windows SmartScreen 可能显示“未知发布者”，这与安装器能否正常安装无关。
