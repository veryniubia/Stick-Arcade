# 如何添加一款游戏

每款游戏放在 `src/games/<name>/` 中。规则和网络状态应与 M5 硬件解耦，以便在 PC 上构建和测试。可参考 `src/games/throwhalla/` 或 `src/games/bumperduel/`。

## 1. 实现游戏接口

创建继承 `arcade::IGame`（定义于 `src/core/game_iface.h`）的类，并实现：

- `begin(mode, seed)`：初始化新对局。模式为 `Solo`、`Host`、`Client`。
- `step(dt, in[2])`：推进一个固定的 1/60 秒模拟步。单人和主机执行模拟；客户端状态由网络快照更新。`in[0]` 是主机/蓝方，`in[1]` 是客户端/红方。
- `prime(in)`：如有需要，在倒计时结束时同步按键计数，避免倒计时期间按住的按键被算作游戏操作。
- `phase()`：返回 `Playing`、`RoundEnd` 或 `MatchEnd`，供平台处理倒计时和再战流程。
- `encode(writer)` 和 `decode(reader)`：编解码客户端需要的完整状态。解码必须事务化：遇到截断或无效数据时返回 false，且不改变游戏状态。
- `draw(local, ctx)` 和 `sfx(events)`：实现显示和音效。

边沿触发操作使用 `PadInput` 的按键计数器（`aCount`、`bCount`）；布尔量表示按键持续按下。对局开始时由 `prime()` 同步计数。单人模式下玩家 1 没有真人输入。可选的 `localInput()` 只用于即时本地显示，不得修改权威游戏状态。

## 2. 添加画面与启动器卡片

将硬件绘制和音效放在 `<name>_view.cpp` 与 `view.h` 中。使用平台画布绘制，不要直接访问 M5 硬件。实现游戏画面、事件音效、120×78 启动器卡片和等待界面背景。启动器名称应简短（最多 10 个字符，仅英文和数字），屏幕文字尽量精炼。可使用 `tools/png2c.py` 将 PNG 转为嵌入资源（16 色调色板，可选 RLE）。

## 3. 描述并注册游戏

定义 `const arcade::GameInfo`，填写唯一的 `wireId`、`protoVer`、`instanceSize`、`arenaBytes`、工厂函数、`maxPlayers` 和屏幕方向。当前 `maxPlayers` 仅作为广播元数据；平台会话目前只配对两台设备。网络状态格式不兼容变更时必须增加 `protoVer`。在 `src/games/registry.cpp` 注册游戏并设置启动器顺序。

## 4. 遵循按键习惯

平台约定 A 为主要操作、B 为次要操作。不要将 A+B 作为退出方式；退出手势和确认由平台统一处理：快速摇晃两次打开退出确认。设置页和等待界面也会使用 A、B，因此请在游戏界面和 README 中说明游戏内按键。

## 5. 编写主机端测试

在 `test_host/` 添加 C++ 测试，覆盖游戏规则、重要边界情况和确定性结果。验证状态编解码往返，并确认无效或截断数据会被拒绝且不会修改状态。在 `test_host/run_tests.py` 的构建列表中加入游戏源文件。在 `test_host/net_test.cpp` 的通用模拟网络夹具中加入此游戏，按适用范围验证配对、状态快照、再战和断线行为。

运行：

```powershell
python test_host/run_tests.py
pio run -e m5stack-sticks3
```

## 6. 添加快速构建环境

在 `platformio.ini` 中基于 `m5stack-sticks3` 添加 `dev-<name>` 环境，定义 `ONLY_GAME_<NAME>`，并通过 `build_src_filter` 排除其他游戏目录。在 `src/games/registry.cpp` 为其他游戏添加对应条件编译保护。使用 `pio run -e dev-<name>` 构建。

## 7. 实机验证

发布前请在 StickS3 实机上检查单人和（如支持）双机联机，验证屏幕方向、A/B 按键、退出流程、断线重连和再战、帧率及内存。调试固件仅用于开发，不应作为普通用户的发布固件。平台接口或用户可见行为有变化时，同步更新中英文 README 和架构说明。
