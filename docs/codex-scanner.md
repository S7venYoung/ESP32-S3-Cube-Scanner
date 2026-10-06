# Zhengchen 1.54 Wi-Fi / 小智 + Codex 扫描页

本版本基于用户提供的 `1、代码.zip` 中 `4、1.6.2版本/源程序代码1.6.2.zip`。
目标是 `zhengchen-1.54tft-wifi`，ESP32-S3、16MB Flash、240×240 ST7789。
请勿将之前 Cube 2.0 的 292×240 固件用于此板。

默认页面已改为 Codex 额度仪表盘，显示五小时剩余、每周剩余和今日 Token。
USB / Wi-Fi 双通道配置见 [同步说明](codex-sync.md)。蓝牙发现仍在后台运行，
不连接键盘，也不提供键盘电量、层号或按键遥测；这些字段明确显示 `--`。

小智语音对话、语音唤醒、服务器 AEC、电池与温度监测保留。AI 对话文字和表情
不覆盖扫描页；标题栏小点显示 AI 状态：黄色聆听、绿色说话、蓝色连接、橙色
配网/激活、灰色待机、红色错误。必要的配网、激活及音量提示短暂显示在底部。
原生低电量与高温告警保留在前台。

三个按键保留原行为：BOOT(GPIO0) 单击控制对话，长按重新配网；音量加(GPIO10)
与音量减(GPIO39) 调音量，长按分别设为最大音量与静音。电源管理和省电计时沿用
附件源码。禁用原厂自动 OTA 固件安装，保留服务器配置查询和激活，以免覆盖定制页。

构建：

```sh
idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.esp32s3;sdkconfig.scanner" \
  -D BOARD_NAME=zhengchen-1.54tft-wifi set-target esp32s3
idf.py build
idf.py merge-bin
```

GitHub Actions 产物：`Zhengchen-154-WiFi-Codex-firmware`。
首次安装使用 `merged-binary.bin`，地址 **0x0**。独立应用
`zhengchen_154_wifi_codex.bin` 仅用于相同分区布局升级，应用偏移 **0x100000**。
完整安装会覆盖对应区域的原配置，之后可能需要重新配网与绑定。

根据附件版本说明，刷写时应接电池、打开电源，使用 USB 下载模式。BOOT(GPIO0)
需要在复位时保持按下以进入下载模式。刷写完毕松开 BOOT 再重启。

验证范围：Actions 编译、分区容量、镜像合并和 SHA256 校验；运行表现仍需实机验证。
