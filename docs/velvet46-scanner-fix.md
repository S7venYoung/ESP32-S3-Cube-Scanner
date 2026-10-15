# 星智 Cube 扫描仪识别修复 · Velvet46

基于用户实际刷入的扫描仪版本 461e934cc3a9bc1ef09211ac2fbf25fc880e8219，继续使用 `codex/nologo-xingzhi-cube-154`。不是 dongle 版本，不改动配对、供电或充电设置。键盘配套使用 `codex/zmk04-dya-Prospector`，右手格斗广播改为真实键盘名称；不改变矩阵、键位或设备角色。

- 星智 Codex 扫描仪构建固定名称过滤器为 `Velvet46`，精确匹配，大小写敏感，不接受 `Velvet46-other`。
- 收到名称后按同一 BLE 地址缓存，后续没有名称的有效 Prospector/Fight 状态包也可更新。显式改名会撤销缓存；缓存有期限且最多八条。非空过滤器下，首次从未收到名称的设备不能仅凭匿名包被认成 Velvet46。名称是广播标签，不是密码或身份认证。
- 兼容旧格斗广播名称 `CUBE-FIGHT`，但仅限同地址已经确认为 Velvet46 的来源。左右手 WPM 使用原生 17 字节 `FF FF AB CE 01` 报文的独立字段（第 10/11 字节，下标从零开始），不把总 WPM 拆半或复制。旧 26 字节状态报文继续负责层、连接及总 WPM，不会覆盖两个独立数值。
- 若状态和格斗广播使用不同地址，旧 `CUBE-FIGHT` 无法可靠证明是 Velvet46，因此推荐配套更新右手中央端，使格斗广播自身携带真实名称。绝不为了显示数值，把所有匿名格斗包都当成 Velvet46。
- 格斗帧检查来源与 16 位序号，重复/乱序帧不续期；允许计数器回绕，超时后回到待机，新的会话可以重新开始计数。人物、招式、待机动画和已有视觉布局不改。
- 无名称包不再依赖仅两个条目的显示列表去寻找名称；合法包会刷新已确认地址的名称关联。没有配置名称过滤器的通用构建，可以用 `ZMK + 地址` 显示合法匿名状态包。
- 键盘状态和设备列表有效期改为 90 秒，兼容约 30 秒的空闲状态广播；格斗动作原有短时衰减不变。
- 保留 64KB 内部空闲内存、16KB 最大连续块保护；不足时在独立启动任务内最多八次、间隔五秒检查，不阻塞主线程。不移除保护强行挤占小智或 Wi-Fi 内存。仍不足会明确报错停止。
- 一旦 NimBLE 已成功初始化，只创建一个 host；底层初始化错误明确报错，不盲目反复初始化。GAP 扫描启动错误使用 1/2/4/8/16/30 秒退避，最多六次重试；正常扫描完成恢复预算，host reset 等待下一次 sync。不会用无延迟死循环重扫。

此版本被动观察键盘广播，不连接/解绑键盘。需要键盘实际启用 Prospector 状态广播；不能从一个独立 Prospector 扫描仪 UF2 的构建链接推断左右手固件。若从未发出 Velvet46 名称或状态广播，Cube 无法凭空发现它。

纯 C++ ASAN/UBSAN 测试覆盖精确名称、空名称、同地址续期、不同地址隔离、改名撤销、超时、非法状态数据和有界退避；SDK 构建验证编译，不代替实机收包验证。异常发热时保持断电，不为复现持续通电。
# Codex and Macintosh keyboard telemetry

## Short radio-gap handling

Fight telemetry now has separate 3-second input and 90-second presence leases.
After 3 seconds without a new accepted sequence, the snapshot reports zero hand
WPM and released modifiers while retaining last-known keyboard batteries. This
returns fighters to idle without flashing the battery gauges to unknown. After
90 seconds without a new frame, the snapshot becomes offline/unknown. Repeated
or rejected sequence numbers still do not renew either lease. Canonical status
modifiers also release after 3 seconds, instead of remaining pressed for the
90-second status lease. No scanning duty-cycle increase or keyboard change is
needed. Host tests cover expiry boundaries, recovery and zero-percent battery;
radio and display behaviour still require hardware validation.

Both dashboards now refresh their layer, WPM and left/right battery widgets from
real scanner telemetry, including immediately after a theme switch. Macintosh
also refreshes the keyboard host transport, Bluetooth profile (1-based) and BLE
connection indicator. Codex shows keyboard transport next to the layer; its
upper-right transport still describes Mac quota synchronization, not keyboard BLE.

Canonical keyboard status supplies layer/profile/total WPM. Fight packets supply
independent left/right WPM and modifiers. Missing fight battery values fall back
to valid canonical battery values, without turning unknown values into zero.
Expired telemetry clears values and pressed modifier backgrounds. This does not
change keyboard firmware, pairing, the scanning policy, or fight animation assets.

Validation: host-side state tests include offline, BLE/USB, per-side battery
fallback, true 0%, invalid values, fight-only and expiry cases. UI binding checks
run alongside scanner regression tests. ESP-IDF compilation and physical display
validation are pending; no Actions build is started by this change.

