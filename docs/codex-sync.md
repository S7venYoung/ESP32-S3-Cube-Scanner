# Cube Codex 双通道仪表盘

基于附件 1.6.2 / zhengchen-1.54tft-wifi：240×240 ST7789，原音频、BOOT/音量键、电池与高温报警不变，小智后台对话、AI 状态圆点保留。Codex 仪表盘替代原蓝牙列表；BLE discovery 保留但不冒充真实键盘遥测。

## 使用

1. 将本次 Actions 的 `build/merged-binary.bin` 烧录到 **0x0**。单独的 `zhengchen_154_wifi_codex.bin` 是应用，不能烧录到 0x0。原板刷机需要接电池、打开电源并进入下载模式。默认不要求整片擦除。
2. 完成原小智 Wi-Fi 配网/激活。Mac 安装配套仓库新版 `Prospector.dmg`，设备类型选择 Cube Codex 双通道。
3. USB 同步成功后自动保存 IP/配对码；勾选 Wi-Fi 同步即可同时发送。只有 Wi-Fi 时可以手动填之前保存的 IP/配对码。

USB 为 UART0 **115200 8N1 TX43/RX44**，保持控制台日志。板上 GPIO20 用作背光，绝不启用原生 USB Serial/JTAG 或 TinyUSB。若板子没有 USB-to-UART 线路，则需外接 3.3V USB-UART (TX->44、RX->43、共地)，不能接 5V 串口电平。板上桥接接线需实测确认。

## 协议

每条 USB 命令以换行结尾，回应可能夹有日志，主机应忽略无关行：

- `CAPS` -> `CUBE-CODEX/2`
- `PING` -> `PROSPECTOR-SCANNER/1`（兼容旧客户端）
- `PAIR` -> `CUBE-PAIR <IPv4> <token>`；随机 32 位十六进制 token 存在板上 NVS，只通过本地串口发出。
- `CODEX2 <5h-left> <week-left> <today-tokens> <quota-age-seconds> <ttl-seconds>` -> `OK` / `ERR`。
- `CODEX <5h-left> <today-tokens> [week-left]` -> `OK`，兼容旧协议。新版客户端使用 CODEX2。

百分比范围 -1..100，Token -1..4294967295，age 0..86400，TTL 30..3600；**-1 为未知**。超长/不完整/越界消息整体拒绝，不改变屏幕数据。

Wi-Fi：`GET http://<IP>:8765/v1/info` 返回版本（无 token）；`POST /v1/codex` 发送同一 CODEX2 文本，需 `X-Cube-Token` header。无认证拒绝 403，非法 frame 拒绝 400。服务仅在 station 获得地址后启动，不抢占配网服务端口。**HTTP 未加密，仅限可信 LAN，不要映射到公网。**

两个通道分别保存最近数据。USB 成功数据包具有 75 秒优先租约；停止发送后切换新鲜 Wi-Fi。两者无新数据 120 秒后显示 OFFLINE / `--`。租约检测不是物理拔线检测。额度样本超过 15 分钟显示未知；今日 Token 仍是最后本机日志统计，直到传输数据过期。键盘 L/R 电量、层和 WPM 目前显示 `--`，不会使用主题图中的示例数字。

本机 JSONL 并非官方账户实时接口：额度取最近记录，不包含未写入这些日志的其他终端活动。仅发送百分比和统计数字，不发送账户凭据或对话内容。
