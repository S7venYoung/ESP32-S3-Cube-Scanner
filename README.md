# Cube Codex · 小智语音桌面终端

基于小智 ESP32 v2.5.0，为 **征辰 `zhengchen-1.54tft-wifi`** 定制的桌面固件。

平时显示 Codex2 风格的额度看板；唤醒时，小智以悬浮面板进行语音对话，结束后回到看板。它不是仅用于蓝牙扫描的固件，也不是 USB 键盘 dongle。

## 当前功能

- 保留小智语音唤醒、对话、播放及原生配网功能，默认中文、“你好小智”唤醒词。
- 默认 Codex2 看板：5 小时剩余额度、7 天剩余额度、当天 Token 统计。
- 看板文字采用覆盖率掩模与 RGB565 Canvas 绘制，保留粗体样式，不依赖 LVGL 字体渲染；原生中文提示仍使用字体。
- USB 转串口与 Wi-Fi 双通道同步，USB 数据新鲜时优先使用 USB，失效后切换到可用的 Wi-Fi 数据。
- 右上角显示本机电池电量及充电状态。
- Codex 定制版保持设定亮度，不因长时间没有对话而自动变暗；电池供电时也一样，耗电量会增加。
- 连接语音、聆听、说话时显示悬浮面板，空闲时隐藏，不保留常驻小智状态圆点。
- 保留音量操作、低电量提示、温度警告和充电感知省电逻辑。

**尚未实现：** ZMK 当前层、WPM、左右键盘电池遥测，以及键盘 dongle 的输入转发。目前蓝牙仅发现广播设备名称和 RSSI；看板未接入的键盘数据显示 `--`，不显示演示值。

## 适用硬件

仅针对当前确认的板型，不保证兼容其他 Cube 或 AMOLED 开发板。

| 项目 | 配置 |
| --- | --- |
| 官方板型 | `zhengchen-1.54tft-wifi` |
| 定制固件名称 | `zhengchen-1.54tft-wifi-codex` |
| 主控 / Flash | ESP32-S3 / 16 MB |
| 屏幕 | 240 × 240，SPI ST7789 |
| USB 同步 | 板载 USB 转串口，UART0 TX43 / RX44，115200 |
| 本机电池 | ADC1 通道 7（GPIO8），GPIO9 检测充电 |
| 背光 / 电源保持 | GPIO20 / GPIO2 |

沿用官方板型的屏幕、音频、按键和电池引脚。**不要启用 ESP32 原生 USB：GPIO20 是背光脚。**

## 下载固件

进入本仓库 [GitHub Actions](https://github.com/S7venYoung/ESP32-S3-Cube-Scanner/actions/workflows/build-cube-codex.yml)，选择成功的 **Build Zhengchen Cube Codex v2.5** 运行，在 Artifacts 下载：

`Zhengchen-154-Codex-v2.5.0-firmware`

解压后主要文件：

| 文件 | 用途 |
| --- | --- |
| `merged-binary.bin` | 完整固件，首次升级使用 |
| `zhengchen_154_wifi_codex.bin` | 定制应用固件 |
| `xiaozhi.bin` | 同一个应用的构建原名，分文件刷写配置引用此名称 |
| `generated_assets.bin` | 新版语音模型及显示资源 |
| `flasher_args.json` / `flash_args` | 本次构建的刷写地址和参数 |
| `sdkconfig` | 实际生效的构建配置 |
| `SHA256SUMS` | 固件校验值 |

Actions 附件保留 30 天，建议下载后保存。官方小智原版 ZIP 不包含本项目的 Codex 定制功能。

## 刷机

**从旧版 1.6.2 首次升级，刷 `merged-binary.bin`，地址填 `0x0`。**

芯片选择 ESP32-S3，Flash 16 MB，DIO，80 MHz。使用板载 USB 转串口连接；开始前备份旧固件和需要保留的配置。

v2.5 的分区及语音资源布局已经改变：

- 不要把新应用刷到旧版 `0x100000` 地址。
- 首次升级不要只刷应用，否则旧分区和资源不能一起更新。
- 全量刷写可能清除 Wi-Fi、激活与配对配置，需要重新设置。
- 后续分文件刷写以下载包里的 `flasher_args.json` 为准，不要凭旧教程填写地址。

当前验证的布局为：应用 `0x20000`，资源 `0x800000`。独立应用升级还需确认正在使用的 OTA 分区；首次升级仍推荐整包。

## macOS 配套程序

使用 [prospector-codex-macos](https://github.com/S7venYoung/prospector-codex-macos) 采集并同步数据。额度不是固件自行查询或估算的，必须由配套程序上报。

- USB：在配套程序选择“Cube Codex 双通道”，启用 USB 并同步；选择实际的板载 USB 转串口端口，波特率 115200。
- Wi-Fi：先通过 USB 同步取得设备 IP 和配对令牌，再启用 Wi-Fi；Mac 和 Cube 需处于可信局域网。同步为未加密 HTTP，端口 8765，不要暴露到公网。
- 协议：`CODEX2 <5h-left|-1> <week-left|-1> <tokens|-1> <age-seconds> <ttl-seconds>`。
- 未取得的数据使用 `-1`，页面显示 `--`；额度来源过期超过 15 分钟时，不继续显示旧额度。
- USB 优先租约为 75 秒，由有效数据刷新，而不是由 PING 刷新。
- Wi-Fi 更新需要配对令牌，不要公开该令牌。

刷机前退出配套程序或停用其 USB 同步，释放串口，避免通信冲突。

如果 USB 或 Wi-Fi 已连接但没有额度，先检查配套程序是否取得有效数据、是否为兼容 CODEX2 的版本，以及是否成功配对。

## 按键与蓝牙

三个顶部功能键目前沿用原版行为：

- 对话键单击：切换对话；启动阶段可进入配网。
- 对话键长按：进入 Wi-Fi 配网。
- 音量加 / 减单击：调整音量；长按：最大音量 / 静音。

蓝牙扫描只在 Wi-Fi 连接后尝试启动，内部内存不足时会跳过，以保留小智、配网和音频服务。当前构建使用 NimBLE 观察者模式，不代表已经实现完整的 ZMK 状态协议。

## 自行编译

使用 **ESP-IDF 6.1**；最低支持 6.0.1，不能使用旧版 IDF 5.x 构建此源码。

初始化 ESP-IDF 环境后：

```sh
python scripts/build.py zhengchen/1.54tft-wifi \
  --name zhengchen-1.54tft-wifi-codex \
  --language zh-CN \
  --wake-word nihaoxiaozhi
```

完整的测试、编译及打包流程：

```sh
bash scripts/build-cube-v25.sh
```

定制版默认禁止官方自动 OTA 替换应用，避免看板被原版固件覆盖；服务器激活和配置获取仍保留。除非明确需要恢复原版，不要启用 `CONFIG_CUBE_ALLOW_STOCK_OTA`。

## 验证状态

通过 GitHub Actions 执行 ESP-IDF 6.1 编译、上游及定制版构建脚本测试、同步协议测试和绘制掩模边界测试。固件包需核对 SHA-256；编译成功不等于硬件测试完成。

刷机后请验证：稳定启动与无持续闪屏、USB / Wi-Fi 两种额度同步、充电与电量、按键、语音唤醒 / 聆听 / 播放及重连。

## 来源与许可

基于 [78/xiaozhi-esp32 v2.5.0](https://github.com/78/xiaozhi-esp32/releases/tag/v2.5.0)，固定基线提交 `ac6deed3d8e75348475364bf40ad953c6cd48054`。

主题参考 [S7venYoung/zmk-prospector](https://github.com/S7venYoung/zmk-prospector)。本项目不是官方小智发行版。

遵循仓库 [MIT License](LICENSE)，保留上游版权与许可声明。
