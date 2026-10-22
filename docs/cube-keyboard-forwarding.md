# 左猫咪、右系统信息：Cube 转发

配套软件复用既有 SystemMetricsReader 一次采集的 CPU%、RAM%、CPU 温度，追加发送 HOST1 帧；Cube 缓存在 RAM，每秒通过既有原生 ZMK split relay 队列转发。不写 NVS，不增加 BLE host，不改变 HID、配对、键盘发现或 Studio RPC。

USB：CAPS/CAPS3 保持原样，新增 CAPSHOST 返回 CUBE-HOST-METRICS/1（仅 split 版本）。配套软件探测到能力后发送 `HOST1 <36个十六进制字符>\n`，Cube 校验成功回复 OK，失败回复 ERR。旧 USB 固件不发送新帧。Wi-Fi：同一 HOST1 帧 POST 到已有 `/v1/codex`，仍要求 X-Cube-Token，校验失败 HTTP 400。旧固件语法拒绝时，配套软件回退已有额度同步；认证和网络错误不会被回退掩盖。

36 个十六进制字符编码 Nano 相同的 18 字节 host_metrics：版本、有效标记、序号、CPU%、RAM%、额度、预留、CPU 温度（有符号 0.1°C LE16）、RAM 用量/总量（64MiB 单位）、reset 秒数。长度/版本/标记/百分比/温度/RAM 约束全部校验，坏数据不更新时效。零值有效，缺失值保留未知。

每秒 `vlcat` 向已就绪的左右手发送版本 1 和零起始实际层号；右手还收到 `vldash` 22 字节版本 2 数据：固定系统页、真实已连接输出 USB/BLE、BLE 卡槽 1–5、18 字节指标。电脑数据 90 秒未更新后清除有效位。断开或禁用电脑 HID 时输出为未知，不能把 split 蓝牙连接冒充电脑 BLE。

发送运行在已有 NimBLE 事件线程。队列忙时让路给待处理 Studio/RPC，请求仍受原有队列长度、连接 generation、加密及超时约束。只连右手也能刷新。主控层号/输出即使电脑指标过期也继续每秒刷新；键盘侧断开/3 秒无系统包时隐藏旧指标。

需要更新：本分支 Cube 主控固件、`S7venYoung/zmk-elashvelvet` 的 `zmk-esp32-cube` 键盘固件、配套软件 main。无需 reset、改 BL 或清除配对。电脑采集间隔维持软件当前设置。

验证：`python3 scripts/tests/test_cube_keyboard_forwarding.py` 覆盖协议、BLE MTU 23/64/247 分包、过期/坏帧、USB/BLE 卡槽、队列让路和仅右手场景。实际 ESP-IDF 构建只在用户请求后由 Actions 执行。
