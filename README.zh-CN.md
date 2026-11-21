# Stick Arcade

**Stick Arcade** 是面向 M5Stack StickS3 的像素风小游戏合集。所有游戏编译进同一份固件，共用启动器、设置系统，并可通过 ESP-NOW 在两台设备间联机。游玩时不需要 Wi-Fi 路由器或互联网。

[English](README.md) · [项目架构](docs/architecture.zh-CN.md) · [添加游戏](docs/adding-a-game.zh-CN.md)

## 游戏

- **Throwhalla**：倾斜设备瞄准，在熔岩竞技场中蓄力投掷武器并使用道具。
- **Big Bump**：竖屏双人相扑游戏，控制充气斗士移动、冲撞并收集道具。

两款游戏都支持单人和两台 StickS3 联机。启动器中按 B 切换游戏卡片，按 A 进入；等待界面按 B 开始单人游戏，按 A 返回启动器。游戏内按键提示会显示对应操作。快速连续摇晃设备两次可打开退出确认。当前屏幕文字为英文。

## 硬件

- M5Stack StickS3 和可传输数据的 USB-C 线。
- 双人联机需要两台安装了兼容固件和游戏协议版本的 StickS3。
- 当前提供 Windows 开发流程；固件使用 PlatformIO、ESP32 Arduino 平台和 M5Unified 构建。

## 构建和烧录

安装 [PlatformIO Core](https://platformio.org/install/cli) 和 Python。在项目目录的 PowerShell 中执行：

```powershell
python -m venv .venv
.\.venv\Scripts\python.exe -m pip install platformio
.\.venv\Scripts\pio.exe run -e m5stack-sticks3
```

连接 StickS3 后烧录（将 `COM9` 替换为设备当前串口）：

```powershell
.\.venv\Scripts\pio.exe device list
.\.venv\Scripts\pio.exe run -e m5stack-sticks3 -t upload --upload-port COM9
```

首次构建会下载 ESP32 工具链、Arduino 框架和 M5Unified。若无法连接设备，请用侧边复位键让 StickS3 进入 USB Bootloader 后重试。也可以用 `dev-throwhalla` 或 `dev-bumperduel` 单独构建某一款游戏。

## 项目结构

`src/core` 包含硬件无关的游戏接口、输入类型、网络编码、设置以及显示和输入支持；`src/net` 负责设备配对并通过 ESP-NOW 传输由主机权威模拟产生的状态；`src/ui` 实现启动器、设置和游戏流程；`src/games` 下每个游戏目录包含规则和画面实现。详细组件职责和数据流见[架构说明](docs/architecture.zh-CN.md)。

## 扩展游戏

实现 `arcade::IGame` 接口，将模拟规则和状态编解码保持为可脱离硬件测试的代码，再添加画面、注册游戏并编写主机端规则和网络测试。[添加游戏指南](docs/adding-a-game.zh-CN.md)包含具体步骤、协议编号、按键约定和验证流程。

## 测试

主机端测试需要 C++17 编译器（`g++` 或 Windows 上的 Visual Studio C++）：

```powershell
python test_host/run_tests.py
```

使用 `pio run -e m5stack-sticks3` 构建包含所有游戏的固件。`test_host/` 中的串口脚本属于开发工具；部分脚本需要连接两台设备。

## 版本与兼容性

固件当前标记为 1.0。联机设备需要使用兼容的固件、游戏 ID 和游戏协议版本；如不确定，请同时更新两台设备。

## 许可证

本项目采用 [MIT 许可证](LICENSE)。
