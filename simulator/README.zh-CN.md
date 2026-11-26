# StickS3 桌面模拟器

Windows 上的项目级 StickS3 硬件接口模拟器。运行原来的 `src/main.cpp`、启动动画、启动器、设置、六款游戏、输入处理及联机会话代码，只有屏幕后端和 ESP-NOW 传输层替换为桌面实现。无需连接实体设备。

## 启动

需要 Python 和 Visual Studio C++ Build Tools（“使用 C++ 的桌面开发”）。先构建一次固件以准备 `.pio/libdeps/m5stack-sticks3/M5GFX` 字体依赖。运行时无需 Python 第三方包。

在项目根目录执行：

```powershell
python simulator/run.py
```

或双击 `simulator/start.cmd`。首次编译后可直接运行 `.pio/sticks3-simulator/sticks3-simulator.exe`。修改源码后重新执行构建命令。

```powershell
python simulator/run.py --build-only
python simulator/run.py --no-build
```

## 操作

| 电脑输入 | 虚拟硬件 |
| --- | --- |
| Z / Enter | A，支持按下、长按、松开 |
| X / Backspace | B，支持按下、长按、松开 |
| 左右方向键 | 临时倾斜 ±30°，松开回到滑块角度 |
| 上下方向键 | 临时俯仰 ±20° |
| Q / E | 临时水平转角 ±15°，按住固定视角，松开回到滑块角度 |
| WASD | Joystick2 摇杆轴 |
| Space | 摇杆按压 |
| 右键拖动 / 滑块 | 设置水平转角和俯仰角度；按住 Shift 右键水平拖动改为调整倾斜 |
| Esc | 一次双摇晃动作，经过原有加速度检测器 |
| R | 恢复设备零角度；以非零姿态校准后，回到校准姿态才是视角正中 |
| P | 暂停 / 继续 |
| F10 | 暂停并执行一帧主循环 |
| F5 | 重启虚拟设备，保留设置 |
| F12 | 将当前游戏画面保存到输出目录的 `screenshot.ppm` |

右侧面板可切换 Joystick2 连接状态，调整设备姿态。Device yaw 与 Device pitch 都是角度：停在某个角度时视角固定，回到校准姿态时视角回中。水平转动 15° 对应视角 35°，30° 对应 70°；超过边界后仍跟踪设备转角，转回范围内再继续跟随。模拟器根据水平角度变化生成陀螺仪采样，保持角度时角速度为零。横屏倾斜和竖屏旋转使用对应持握姿态生成重力向量；模拟的是理想传感器采样，仍经过原来的滤波、校准和摇杆映射。

桌面画面将五档背光设置映射为适合显示器的亮度，默认约 94%，最高档为原始像素颜色；实体设备的 PWM 背光数值和保存格式不变。游戏区域独立缓冲重绘，右侧控件不随游戏帧刷新，状态文字最多每秒刷新十次。

按 A 跳过启动画面；启动器按 B 切换、A 进入；游戏等待界面按 B 单人练习。MidDiff 先在等待界面短按 B 并松开，保持静止完成校准，然后再按 B 进入单人，用 WASD 移动、Space 跳跃、Q/E 转向、A 射击。HwyDrift 开赛时切换竖屏，从全图拉近到跟车视角。按住 Z 加速、松开减速，左右方向键打方向，任何车速都可以按住 X 漂移。Big Bump 会自动切换竖屏。MoonDuel 和 Spring Ring 单人对手保持中性输入。Spring Ring 中按住 Z 伸腿，松开收腿，左右方向键模拟横屏倾斜以调整落脚点。

暂停会冻结虚拟时间。A/B 面板按钮产生约 100 ms 短按；持续按键请使用键盘。失去窗口焦点时释放键盘，双窗口测试时避免出现卡住的按键。

## 双设备联机

启动两个实例，分别指定设备编号：

```powershell
python simulator/run.py --no-build --device 1
# 另一终端：
python simulator/run.py --no-build --device 2
```

在两边进入同一游戏的等待界面即可配对。沿用原本的主机权威同步和游戏协议，以 `127.0.0.1` 的 UDP 端口 39171 / 39172 传输，每个编号同时只运行一个实例。两个设备分别使用 `settings-1.bin` / `settings-2.bin`，不会修改实体设备设置。关闭任一窗口可测试断连。

## 可重复输入测试

```powershell
.\.venv\Scripts\python.exe simulator/check.py
```

验证六款游戏从真实启动器进入单人、按键长按、IMU、摇杆、竖屏、双摇退出、设置持久化、缺少摇杆时的入口限制，以及两个完整应用实例之间的联机。此验证脚本额外需要 Pillow（`python -m pip install pillow`）。产物在 `.pio/sticks3-simulator/checks/`。

也可编写输入脚本，每行格式为 `帧号 输入 值`，状态持续到下一次变更：

```text
4 a 1
6 a 0
100 a 1
102 a 0
115 b 1
117 b 0
170 tilt 25
200 a 1
230 a 0
250 shot playing.ppm
275 shake 1
```

```powershell
python simulator/run.py --no-build --headless --frames 340 --script input.txt --output .pio/sim-replay
```

支持 `a b tilt pitch yaw joyX joyY stick connected shake shot`。脚本在指定帧主循环前生效；`shot` 保存此前最近一次绘制的画面。`joyX/joyY` 范围 -100..100，角度单位为度，`yaw` 也是设备水平角度（度），保持数值不会持续旋转。`--realtime` 可让无窗口运行按实际时间推进，用于两个进程联机测试。输出包括 `input-trace.csv`、`events-编号.csv` 和 `final.ppm`。

## 范围

这是当前项目所用 StickS3 接口的源码兼容后端，运行桌面编译的同一份应用源码，不能直接加载 ESP32 固件。画布覆盖目前使用的 M5GFX 操作，字体复用 M5GFX 内的 Adafruit 字体；声音通过 Windows 音频输出。新游戏使用现有接口时自动纳入构建；新增硬件 API 需要扩展对应适配器。

模拟器适合规则、流程、画面和输入回归测试。真实 IMU 噪声、硬件性能、耗电、DMA/I2C 时序和 ESP-NOW 无线质量仍需真机验证。添加 `--seed 122` 可固定随机开局（使用相同设置和输入脚本）；双设备联机应使用不同的随机种子。
