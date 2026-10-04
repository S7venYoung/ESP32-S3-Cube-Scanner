# Cube WALLE-CODEX 扫描仪仪表盘

本功能在保留小智 AI 的网络、音频、唤醒词和对话流程的同时，后台主动扫描
ZMK 分体键盘的 BLE 广播。Cube 横屏默认显示 WALLE-CODEX 风格的扫描仪仪表盘；
小智对话仍可语音唤醒或按原 BOOT 键操作，但不再把对话文本/表情显示在前台，
而以标题栏中的小圆点显示状态。

扫描器最多显示两个最近发现的命名设备及 BLE 地址、RSSI/信号条；超过 10 秒未
收到广播的设备会从列表清除。可在 `idf.py menuconfig` 的
`Xiaozhi Assistant -> ZMK split keyboard scanner` 中设置 `Advertised name filter`，
例如键盘广播名称包含 `Sofle` 时填入 `Sofle`，只显示匹配设备。Cube 2.0 TFT
默认启用此仪表盘；其他板型默认关闭。

ZMK 的左右半边与 central 之间使用已配对的加密 GATT 通道。旁观扫描器不能、也不应
尝试解析按键事件；本固件的用途是确认半边是否在附近、广告名是否正确和信号强度。

扫描与 Wi-Fi/小智会话共享 ESP32-S3 射频资源，BLE/Wi-Fi 并发由 ESP-IDF coexistence
机制调度。它不会连接键盘，也不会读取按键内容。

构建示例：

```sh
idf.py set-target esp32s3
idf.py menuconfig
idf.py build flash monitor
```

GitHub Actions 的 `Xiaozhi-Cube-Codex-firmware` 构建产物包含：

- `merged-binary.bin`：完整安装镜像，包含引导程序、分区表、语音模型与应用，刷写地址为 `0x0`。
- `xiaozhi_cube_codex.bin`：单独应用镜像。仅供相同小智合并分区布局的升级使用，应用地址为 `0x100000`。

从此前独立扫描仪固件迁移时，应使用完整安装镜像，不能沿用独立扫描仪的
`0x10000` 应用地址。完整安装可能覆盖原配置，刷写前保留所需设置；之后按
原小智流程配网与激活。本构建只提供小智应用，未附带另一套可切换的原厂应用。

按键保持原有逻辑：BOOT 单击控制对话，音量加减键通过 ADC 调节音量，电源键
保持原有电源管理。屏幕仅展示 AI 状态圆点；扫描数据为命名 BLE 广播和 RSSI，
不包含键盘电量、层号或按键遥测。
