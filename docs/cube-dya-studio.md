# Cube 双模版的 DYA 配置

2026-10-14：用户确认当前双模固件硬件验证成功。后续下载包名称为 `Cube-Dual-Dongle`，不再带未验证后缀。此确认针对当前已实现功能，不代表下述仍未实现的高级功能已完成；`.bin` 文件名保留以兼容现有刷机工具。

仅 `xingzhi_cube_dual_candidate.bin` 启用。USB-only 版不新增此配置功能；本版本保留 USB 和电脑蓝牙键盘输出、小智和全部现有主题。

1. 退出 macOS 同步程序、串口监视器和刷机工具，避免占用 CDC。
2. Cube 正常开机连接 USB；在 Chrome/Edge 打开 https://studio.dya.cormoran.works/ ，选择 USB 和运行态 `Cube ZMK Dongle Candidate`。不要进入 ROM 下载模式。
3. 双击 Cube 的 BOOT，解锁写入五分钟。短按仍为小智对话；长按是下载模式，请勿混淆。
4. 可读取原始 61 键物理布局和 BASE/NVA/SYS 三层、修改已有绑定和层名、保存、撤销未保存修改。保存返回成功前会检查 NVS 提交结果，重启后恢复已保存配置。

当前支持：普通键、组合修饰键、透明/禁用键、瞬时层、音量加减/静音、BT_SEL 0..4、BT_CLR、BT_CLR_ALL。修改只影响后续按下的键，已经按住的键保留原绑定直到释放。恢复 Studio 设置仅恢复此 keymap，不清除 Wi-Fi、小智激活或蓝牙配对。

这不是完整 ZMK 引擎：固定三层，不支持新增/删除/重排层、宏、组合键、hold-tap、鼠标行为、DYA 其他自定义子系统。不支持的功能返回错误，不声明虚假能力。

`codex/xingzhi-cube-dongle` 已加入 BLE Studio GATT。先将当前蓝牙卡槽的 `Cube Velvet Pro` 与电脑完成配对，再在支持 Web Bluetooth 的浏览器中选择 DYA 蓝牙连接。配置连接与键盘 HID 共用一个加密连接，只有当前卡槽已授权的电脑可写入；双击 BOOT 解锁已连接的配置会话五分钟。USB 与 BLE 解锁状态分别维护，断线或切换连接重新锁定。此路径已编译，用户已确认当前版本硬件验证成功；浏览器与操作系统是否提供 Web Bluetooth 需在实际环境确认。

二进制 Studio framing 与原 Codex 文本协议共用 CDC，由帧边界隔离。单帧上限 4096 字节，超过限制的帧丢弃；半帧两秒超时；断开及五分钟超时重新锁定。BLE 根据 MTU 分片，只有收到 indication 确认才推进，队列有上限，旧连接响应不会发送给新连接。NVS 只使用 `cube_studio/keymap_v1`，不触碰 split/host 绑定。

协议依据 cormoran/zmk-studio-messages `89b81d2e587fce807b668dff2a6967a40beef421`，独立实现。主机测试包含分帧/转义、协议响应、持久化损坏、保存失败、非法参数、锁定及输入释放。编译成功不等于网页或实机验证，首次安装后需确认网页载入、修改保存、重启保留及持续打字。
