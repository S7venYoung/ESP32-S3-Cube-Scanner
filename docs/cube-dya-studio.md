# 星智 Cube 双模版：DYA Studio

开发分支：`codex/xingzhi-cube-dongle`。保留小智语音、Fight/Codex 等主题和 USB/BLE 电脑输出，不继续开发 USB-only 版。用户已验证上一版基础连接与输出；2026-10-15 新功能仍须统一 SDK 编译和后续实机验证。

## 连接与解锁

1. 退出占用串口的同步程序、刷机工具及串口监视器。
2. 正常开机，在支持相应浏览器接口的 Chrome/Edge 打开 https://studio.dya.cormoran.works/ 。USB 选择运行态 `Cube ZMK Dongle Candidate`，不要进入 ROM 下载模式。
3. BLE 先在当前电脑卡槽配对 `Cube Velvet Pro`，再选择 DYA 蓝牙连接。Studio 与键盘 HID 共用加密连接，只接受当前卡槽授权的电脑。
4. 双击 BOOT 解锁五分钟。短按仍为小智对话，长按进入下载模式。USB/BLE 独立会话；关闭串口、断线及超时重新锁定，关闭 DYA 不会暂停 HID 打字。

## 当前接入范围

| 子系统 | 实际功能及限制 |
| --- | --- |
| 标准 keymap / behaviors | 61 键、最多四层稳定 ID 0..3；增删/恢复/重排/命名/保存/撤销；按下绑定锁存；V1 配置迁移到 V2，不清除其他 NVS |
| `cormoran__fast_keymap` | 十项快速读取、完整布局几何、行为详情、默认/当前差异、未保存位置和缓存指纹 |
| `cormoran__runtime_macro` | 8 槽、3072 字节池、每宏最多 64 步；创建/编辑/命名/删除/保存/撤销；按下/抬起/点击/延迟/紧凑键序列；一次播放一个宏 |
| `cormoran__runtime_combo` | 8 组、每组 2..4 个位置，跨左右手；超时/前置空闲/慢释放/层限制/重叠优先；未匹配时补回真实单键和快速点按 |
| `cormoran_ble` | 五电脑卡槽真实绑定/连接信息、命名/选择/解绑/输出优先级；左右手状态和解绑；只删除明确目标 |
| `zmk__settings` | Cube 和已接通原生 relay 的左右手活动设置读写；外围运行值读回确认；Cube 默认 idle/sleep 均 0，保持常亮 |
| `zmk__device_info` | 实际 MAC ID、构建、ESP-IDF 版本、板型、Flash、复位原因和运行时间，不伪装成 Zephyr |
| `cormoran__watchdog` | Cube 任务看门狗和 16 条持久化复位记录；分页/删选；左右手原生请求及日志转发；危险崩溃注入关闭 |
| `cormoran__kscan_diagnostics` | 61 远程位置真实统计；左右手原生扫描诊断转发；Cube 没有 GPIO 矩阵，不虚构本地设备 |
| `zmk__input_stream` | 解锁会话的按键位置、锁存绑定及层变化流；断线代次隔离；有限队列，溢出停流 |
| `cormoran__default_layer` | USB + 五 BLE 卡槽默认层、OS 映射；删除所选层后安全回退，已按住键不重解释 |
| `cormoran__os_detection` | USB 枚举观测分类、五卡槽手动 OS 覆盖；无可靠观测时明确返回未知 |
| `cormoran_rip` | 实际 input-split 指点通道；倍率/旋转/轴变换/活动层/临时层/轴吸附/XY 滚动/惯性及通知；三种写入模式、保存全部/撤销/复位；USB/BLE 五键鼠标与双向滚轮 |
| `cormoran_rsr` | 数量及每圈触发数来自加密能力特征；正反转分层绑定、真实点击时长、键/媒体/层/主机/宏输出；最多每半两个 |
| `cormoran__physical_layouts` | 实际发现的指点设备和编码器清单；无坐标元数据时不捏造外设安装位置 |
| `cormoran_custom_settings` | 十二项通用设置操作、元数据/权限/范围、保存/撤销/重置/数组/分块及 `cube.user.` 用户空间；`hold_tap.term_ms` 真实控制 1..5000ms 阈值 |

行为 ID 1..8：Key Press、Momentary Layer、Transparent、None、Bluetooth、Runtime Macro、Mod Tap、Layer Tap。Hold-tap 为 hold-preferred：短按 tap、超时或其他键打断后 hold；阈值按每次按下锁存。不宣称支持其他风格或任意 ZMK behavior。组合与编码器是合成输入，不接受物理 hold-tap 行为。宏支持按键与媒体键，不执行任意层/主机动作。

## 外设与安全边界

当前 Velvet **没有轨迹球或编码器**，实际设备数为零，编辑器不显示硬件项目是正常的。接入真实模块需要相应驱动、管脚和 native input-split/sensor 配置；不启用假 PMW3610 或伪造位移。PMW 芯片 DPI/寄存器/图像采集页面不适用，未注册。ESP-IDF 不提供 Zephyr 专属 GPIO、冻结线程栈及配置枚举，不伪装这些能力。

USB OS 分类是指纹推断，不能可靠区分 macOS/iOS 或 Linux/Android；BLE 自动识别无可靠观测时为未知，可逐卡槽手动设置。默认层映射可正常使用。

左右手新版启用 Settings RPC、扫描诊断及看门狗原生 split relay。缺少外围固件、未连接、队列满或超时会返回错误。外围设置读回确认运行值，持久化沿用 Zephyr 延迟保存，不承诺即时断电安全。解绑左右手后本次启动暂停自动重绑定；重新配对可能需要清除外围旧绑定，不能要求每次都刷 settings_reset。

切换输出、目标丢失、左右手断线取消宏、组合、hold-tap、鼠标惯性及编码器延迟输入，等待物理中立。相对位移仅发送一次，不能由定时器或 GET_REPORT 重放。按键、媒体和鼠标按钮释放屏障覆盖 USB/BLE。

## 存储及测试

配置独立位于 `cube_studio`：`keymap_v1`、`macros_v1`、`combos_v1`、`activity_v1`、`watchdog_v1`、`defaults_v1`、`pointers_v1`、`sensors_v1`、`custom_v1`。记录带版本和校验；保存失败保留旧状态。恢复配置不整体擦除小智激活、网络和配对。

测试包含 15 组 C++ ASAN/UBSAN 及 Python 回归。新增协议使用固定上游 protobuf 独立解码，检查 61 键几何、负默认层哨兵、行为元数据、OS 覆盖、范围约束、指点写入模式与编码器绑定。来源和 MIT 许可见 `experimental/cube_dongle/*PROTOCOL-NOTICE.md`。

开发提交不触发 Actions，全部特性提交后统一构建双模 Cube 与左右手。除手动构建外，仅修改 `.github/cube-final-build-request` 会启动原分支的最终构建，普通源码提交不触发。SDK 编译不代替实机检查：需验证 USB/BLE 配置、保存重启、持续打字、组合/宏/hold-tap、端点切换及断线释放，安装真实模块后还要验证鼠标/编码器。
