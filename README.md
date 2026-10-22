# Cube Velvet Pro · 星智 Cube USB/BLE Dongle + 小智

本分支 `codex/xingzhi-cube-dongle` 专用于 Nologo 星智 Cube 1.54 TFT Wi-Fi（ESP32-S3），沿用官方硬件管脚。Cube 作为 ZMK 分体中央端处理左右手矩阵、动态 keymap、宏和组合，通过 USB 或电脑蓝牙输出；保留小智对话和悬浮唤醒、Codex/麦金塔/Fight 主题及同步功能。

电脑蓝牙名称固定 `Cube Velvet Pro`，支持五卡槽。长按音量加切换下一个蓝牙卡槽，长按音量减切换主题，既有键盘快捷键及语音控制保留。BOOT 短按小智、双击 DYA 解锁五分钟、长按进入下载模式。不会默认永久解锁。

## DYA Studio

USB CDC 和加密 BLE Studio 共用真实运行配置，独立会话和断线隔离。接入动态层、运行时宏/组合、hold-tap、BLE 管理、设备信息、外围设置/日志/扫描诊断转发、输入流、快速 keymap、默认层/OS 映射、通用设置及可选指点/编码器软件通路。

[完整支持范围、限制与连接方法](docs/cube-dya-studio.md)。当前键盘没有轨迹球或编码器，硬件数量为零，不伪造模块或输入。ESP-IDF 不伪装成 Zephyr；未安装的 PMW 芯片专属功能不注册。用户已验证旧版基础硬件，新功能仍需新固件实机检查。

## 构建与刷写

ESP-IDF v6.1，`bash scripts/build-cube-dual-candidate.sh`。Actions 使用双模工作流；普通开发提交不触发，仅手动或最终构建标记启动。USB-only/bench 不随此次开发自动运行。

- 完整安装：`merged-binary.bin`，包含启动、分区、应用与资源，使用匹配星智硬件的刷写预设。
- 同分区应用更新：`xingzhi_cube_dual_candidate.bin` 或同包 `xiaozhi.bin`。保留文件名以兼容刷机工具；并不是独立的“dongle 插件”，应用本身包含小智与 dongle。
- 左右手固件：`S7venYoung/zmk-elashvelvet:zmk-esp32-cube` 的 `velvet_left_cube.uf2` 和 `velvet_right_cube.uf2`。
- 不要刷正辰版本，不要把应用镜像当完整镜像写到 0x0；已有运行态 HID/CDC 端口不等于 ROM 下载端口。BOOT 恢复及刷机工具说明见 [双模说明](docs/cube-dual-candidate.md)。

配置保存不整体擦除小智激活、Wi-Fi 与蓝牙绑定。首次换中央端可能需要清除旧左右手配对，普通更新不应每次都刷 settings_reset。编译成功不等于新功能全部实机验证。

## Cube 键盘主题数据转发

支持左手猫咪层号与右手 CPU/RAM/温度主题，复用既有电脑采集结果和 split relay。USB 与已配对 Wi-Fi 都可更新系统信息；详见 [数据协议与升级说明](docs/cube-keyboard-forwarding.md)。

### DYA 键盘功能

支持范围、新增 Runtime Tap Dance API v2 及使用方法见 [DYA 功能说明](experimental/cube_dongle/DYA-FEATURES.md)。
