# DYA 键盘功能支持

Cube 分支的 DYA RPC 在 ESP32 接收器中执行；键盘固件负责分体输入和显示同步。不要仅在键盘端启用模块就认为 Cube 已支持对应功能。

| 功能 | 状态 |
| --- | --- |
| 普通改键、层、物理布局、Fast Keymap | 已接入 |
| Runtime Macro / Combo | 已接入，保留已有配置和存储格式 |
| Runtime Tap Dance | 新增 API v2、命名配置、持久化和执行引擎 |
| Custom Settings、Device Info | 已接入 |
| 默认层、系统识别、输出和蓝牙管理 | 已接入 |
| 输入状态、Kscan Diagnostics、Watchdog | 已接入，分体诊断依赖配套键盘固件 |
| HOST1 系统信息及键盘屏幕转发 | 保留已有实现 |

## Runtime Tap Dance

在支持 API v2 的 DYA 中创建命名 Tap Dance，设置单击、双击、三击动作，再将 Runtime Tap Dance 行为及其 ID 分配给按键。原有按键不会自动改成 Tap Dance。

Cube 支持 16 个命名条目，名称最多 24 个 UTF-8 字节，逻辑配置池 1024 字节，最多 8 个同时活动序列。共享点击间隔默认 200ms，可设 1–2000ms。ID 单调递增且不复用；受 Cube 原有按键存储结构限制，范围为 0–65535，耗尽时拒绝创建而不会截断。保存/放弃修改、重命名、删除、分页列表均按 API v2 处理；RAM 创建也会持久化 ID 高水位。

支持按下/释放、超时、其他按键打断和序列开始时的配置快照。短按输出按“按下、释放、后续普通按键”的顺序发送，避免修饰键污染后续字符；断连取消待处理动作。支持已有键盘动作及嵌套 Mod Tap/Layer Tap，禁止递归 Runtime Tap Dance。Combo 和传感器的直接动作不接受 Runtime Tap Dance；可将普通物理按键分配为 Tap Dance。

## 范围和验证

开发测试专用的虚拟输入注入、事件捕获和日志捕获不在日常固件自动启用。编码器、轨迹球、指针硬件不新增；DYA 的云端分享、历史记录等客户端功能不属于固件模块。

本次通过 21 项 C++ 主机测试（AddressSanitizer/UndefinedBehaviorSanitizer）和 70 项 Python 测试，包括上游 protobuf 解码、持久化、按键释放与真实生产过滤函数的输出顺序。尚未进行本次变更的 ESP-IDF 固件构建和实机测试；按用户要求，Actions 仅在明确请求构建时启动。此前已验证功能的记录不受此说明影响。
