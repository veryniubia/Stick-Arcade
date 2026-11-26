# 一台 StickS3 和一部 Android 手机联机

手机是可操作的第二名玩家。两端选同一款游戏后，沿用原版配对、随机主机、权威模拟、快照和再战流程。可以先在手机离线练习，再给 StickS3 接入热点模式。包名为 `dev.stickarcade.player`，本地安装包面向 arm64-v8a，最低 Android 8.0（API 26），目标 API 35。本次接入的 vivo X Fold6 型号为 V2545A，运行 Android 16（API 36）、OriginOS 6。

## 安装和刷写

构建输出位于仓库的 `out/phone-link/`。

1. 将 `stick-arcade-player.apk` 复制到手机，在文件管理器中打开并允许本次本地安装。使用 USB 调试时也可执行 `adb -s 手机序列号 install -r out/phone-link/stick-arcade-player.apk`。覆盖更新请使用相同包名和签名。
2. 使用 USB 数据线接 StickS3，在仓库根目录用 `.\.venv\Scripts\pio.exe device list` 核对串口。构建环境已就绪时，执行 `.\.venv\Scripts\pio.exe run -e m5stack-sticks3 -t upload --upload-port COM9`，将 COM9 换成实际端口。
3. 直接使用预编译固件时，解压 `stick-arcade-sticks3.zip`，在解压目录用 esptool 4.9 执行 `python -m esptool --chip esp32s3 --port COM9 write_flash @flash_args.txt`。固件包内有四个镜像、地址表及 SHA-256。bootloader、分区表、OTA 启动数据和应用分别写入 `0x0000`、`0x8000`、`0xe000` 和 `0x10000`。NVS 位于 `0x9000–0xdfff`，保留旧设置时请使用分段写入并跳过整片擦除。

## 连接和操作

1. StickS3 每次启动时手机模式默认关闭。按任意键进入启动器，B 切到 SETTINGS，A 进入。B 选 **PHONE LINK**，A 打开详情，再按 A 选 **TURN ON**。页面显示 `StickArcade-XXXX` 和 **KEY**。密码在同一次启动期间可重复使用。重启后请用新显示的密码，手机缓存旧密码时可先忘记该网络。
2. 手机打开 Stick Arcade，点顶部 **Wi-Fi**，在系统设置加入该热点，遇到无互联网提示时选择继续使用。回到应用后，StickS3 显示 **APP CONNECTED**，手机显示 **UDP OK**。**WI-FI JOINED** 表示手机已加入 Wi-Fi，应用还没有完成 UDP 探测。可以保持移动数据开启。
3. StickS3 在详情页按 B 返回设置，用 B 选 BACK，A 返回启动器。两端用 B 选同一张游戏卡，按 A 进入等待室，在两端等到 READY 和倒计时。两端的随机数决定主机。手机状态栏会显示游戏、Host 或 Client、RTT 和实际循环帧率。RTT 有往返样本后才显示毫秒值。
4. 单人练习时可直接在手机进入游戏等待室，按 B 选 SOLO。MoonDuel 的练习对手持续接收中性输入，照常参与物理、受击及回合重置。
5. MidDiff 在 StickS3 一侧需要接好 Unit Joystick2。手机使用屏幕下方摇杆和独立 JUMP，A 射击，短按 B 切镜，长按 B 重新校准。进等待室后两端分别面向正前，按 B、松开、握稳约一秒完成校准。HwyDrift 进入比赛后竖握手机，右侧压低向车身右侧打方向，左侧压低向左打。按住 A 加速、松开减速，B 在任何车速下都能按住漂移。先展示全图，再拉近到发车位，随后倒数 3、2、1。松开 B、回正方向可收住侧滑，右下角仪表显示相对车速。Throwhalla、Big Bump 和 MoonDuel 使用倾斜和 A/B。画面以原比例居中，操作键支持多指同时按住。
6. 双摇设备打开退出提示，A 退出、B 取消。退出游戏后热点继续工作，双方可以换游戏。要关热点，回 **SETTINGS → PHONE LINK → TURN OFF**。之后进游戏恢复原有 ESP-NOW 搜索。
7. 切换内外屏前请先退出对局。应用切后台会结束当前对局、释放虚拟按键和摇杆、停止传感器、音频及 UDP。回前台会重新检测热点。**Wi-Fi** 和 **日志** 会打开系统界面，适合在开局前或赛后使用。

## 记录和排查

MidDiff 校准完成后，手机状态栏中的 **HDG** 表示左右视角来自系统融合航向。数字是相对校准中心的游戏视角（度），向右为正。手机用角速度判断是否在转动，并按实测朝向更新视角。校准完成时若缺少有效的航向样本，本局使用 **GYRO** 陀螺仪积分；长按 B 可以重新校准。磁性支架或附近磁场变化可能影响系统航向。

- 手机每秒采样一次状态，**日志** 可把最近约 30 分钟内的最多 1800 条记录保存为本地文本。包括时间戳、Android/API、传感器名称、前后台事件、游戏和角色、RTT 和 fps。建议每轮测试后保存，并另记内屏或外屏、OriginOS 版本和异常发生时间。
- 手机显示 **Offline** 时先确认系统 Wi-Fi 的热点名和密码。**Wi-Fi / probing** 表示找到目标子网、正在等设备应答。**Socket error** 后的 errno 可随日志一起记录。两端已显示 UDP 连通但一直在等待室时，请核对游戏卡、MidDiff 本机校准进度，以及对方是否已经按 B 进入单人。
- 热点固定为 2.4 GHz 信道 1、最多一部手机。UDP 端口为 39731，StickS3 地址是 `192.168.73.1/24`。应用观察 Wi-Fi 网络及其本地地址，用 Android 网络句柄将套接字绑定到该 Wi-Fi，随后与固定端点互发探测包。游戏数据直接携带原会话负载。断线和超时沿用原会话时序。套接字绑定与传感器坐标依据 Android 的接口定义实现。[NDK 网络接口](https://developer.android.com/ndk/reference/group/networking) · [Android 传感器](https://developer.android.com/develop/sensors-and-location/sensors/sensors_overview)

## 从源码构建

在仓库根目录运行。Android 构建只需要 Python 3、JDK 21、Android SDK Platform 35、Build Tools 35.0.1 和 NDK r27d（27.3.13750724）。设置三个路径即可复用既有安装，无需 Android Studio、Gradle 或下载 Maven 依赖。以下路径对应本次构建主机，换机器时替换为本机路径。

```powershell
$env:JAVA_HOME = 'Q:\software\rea-tools\jdk-21.0.13.0.2+1'
$env:ANDROID_HOME = 'Q:\software\rea-tools\android-sdk'
$env:ANDROID_NDK_HOME = 'Q:\AIProject\raepopcart\tools\android-native\android-ndk-r27d'
python android/build.py
.\.venv\Scripts\pio.exe run -e m5stack-sticks3
python tools/package_phone_firmware.py
```

本次签名使用该 Windows 用户已有的 `%USERPROFILE%\.android\debug.keystore`。请在仓库外备份原文件。换构建机时还原该文件，或用 `STICK_KEYSTORE` 指向它。工具也支持 `STICK_KEY_ALIAS`、`STICK_STORE_PASS` 和 `STICK_KEY_PASS` 环境变量。构建会用 apksigner 验签、比对仓库里的公开证书指纹并检查 16 KB ZIP 对齐，再替换输出 APK。重建另一把同名密钥会改变签名，无法覆盖已安装版本。下一版可递增 manifest 的 versionCode。

Android 原生 Activity 提供触控、系统 Wi-Fi 入口、传感器回调和文件导出。JNI 直接调用项目的 setup/loop，使用模拟器的 RGB565 画布及虚拟 Joystick2 接口。游戏和网络规则继续由 `src/` 编译。Android 传感器按当前显示旋转映射轴，将加速度换为 g、角速度换为度/秒、启动时钟纳秒换为微秒，再交给共享输入滤波器。游戏逻辑保持 60 Hz 固定步长，显示按各界面的目标节奏限帧。

## 回归测试

```powershell
python test_host/run_tests.py
.\.venv\Scripts\python.exe simulator/check.py
$jdk = "$env:JAVA_HOME\bin"
New-Item -ItemType Directory -Force out/phone-input-test | Out-Null
& "$jdk\javac" -d out/phone-input-test android/java/dev/stickarcade/player/MotionAxes.java android/java/dev/stickarcade/player/TouchState.java android/tests/InputCheck.java
& "$jdk\java" -cp out/phone-input-test dev.stickarcade.player.InputCheck
```

以下两条使用明确指定的测试模拟器。先安装 APK。第一条会重启应用、操作菜单并将截图存到 `out/phone-link-check`。第二条用 NDK 编译测试程序，并在模拟器自身的真实 UDP 回环套接字上运行两端会话。

```powershell
$adb = "$env:ANDROID_HOME\platform-tools\adb.exe"
python android/check_device.py --adb $adb --serial 127.0.0.1:5555
python android/check_udp.py --adb $adb --serial 127.0.0.1:5555
```

## 真机验收顺序

| 阶段 | 在 X Fold6 与 StickS3 上观察 |
| --- | --- |
| 连接与设置 | 更新前记录四项已有设置。更新后核对数值、确认开机热点关闭。手机在启动器加入；保留移动数据确认 UDP OK；连续开关、退出游戏换卡、设备重启。 |
| Throwhalla | 两端倾斜方向、静止校准、蓄力释放和命中同步。记录角色、RTT、fps，在不同场次观察两种主机角色。 |
| Big Bump | 确认开局竖屏、两端相同动作的倾斜方向和强度、A/B 同按及比分一致，返回启动器恢复横屏。 |
| MoonDuel | 检查双键边跳边打，双方计分、赛后同时再战。离线练习时观察中性对手的受击与复位。 |
| HwyDrift | 倾斜转向、A 给油、B 漂移、障碍碰撞、双机三圈竞速及再战。 |
| MidDiff | 真摇杆与虚拟摇杆、移动中跳射、校准、左右转向及俯仰、切镜与再校准。 |
| 断线和折叠屏 | 记录短暂掉线、长掉线、对方退出和重连表现。分别从外屏和展开内屏启动，退出对局再切屏。结束后保存日志，关闭手机模式，核对原 ESP-NOW 搜索。实际 ESP-NOW 双机互通另需第二台兼容 StickS3。 |
