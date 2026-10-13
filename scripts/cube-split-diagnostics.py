#!/usr/bin/env python3
"""Explicit bench diagnostics/provisioning. Never flashes, resets, or deletes bonds."""
import argparse
import re
import time


def validate_identity(value, address_type):
    if not re.fullmatch(r'(?:[0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}', value):
        raise ValueError('MAC 地址格式应为 AA:BB:CC:DD:EE:FF')
    octets = bytes(int(part, 16) for part in value.split(':'))
    if octets in (bytes(6), b'\xff' * 6):
        raise ValueError('不能使用全零或全 FF 地址')
    if address_type == 1 and octets[0] & 0xc0 != 0xc0:
        raise ValueError('静态随机身份地址的最高两位必须为 1；不要使用临时 RPA 地址')
    return value.upper()


def decode_status(line):
    match = re.fullmatch(r'CUBE-SPLIT-BENCH/1 (\d+) HID-OFF', line)
    if not match:
        raise ValueError('不是输入测试固件的诊断响应；不会继续写入配置')
    word = int(match.group(1))
    if word & ~0xff0f or (word >> 8) > 61:
        raise ValueError('诊断状态不合法')
    return dict(left_configured=bool(word & 1), right_configured=bool(word & 2),
                left_ready=bool(word & 4), right_ready=bool(word & 8), pressed=word >> 8)


def exchange(port, command, prefix):
    port.reset_input_buffer()
    port.write((command + '\n').encode('ascii'))
    port.flush()
    deadline = time.monotonic() + 3
    while time.monotonic() < deadline:
        raw = port.readline(512)
        line = raw.decode('ascii', errors='replace').strip()
        if line.startswith('ERR'):
            raise RuntimeError(line)
        if line.startswith(prefix):
            return line
    raise TimeoutError('Cube 未回复；确认固件版本、串口，以及已退出配套同步程序')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--bind-left')
    parser.add_argument('--bind-right')
    parser.add_argument('--left-type', type=int, choices=(0, 1), default=1)
    parser.add_argument('--right-type', type=int, choices=(0, 1), default=1)
    args = parser.parse_args()
    bindings = []
    for side, value, kind in [('L', args.bind_left, args.left_type), ('R', args.bind_right, args.right_type)]:
        if value:
            bindings.append((side, validate_identity(value, kind), kind))
    if len(bindings) == 2 and bindings[0][1:] == bindings[1][1:]:
        raise ValueError('左右手不能绑定同一个身份地址')
    if not args.port.startswith('/dev/cu.'):
        raise ValueError('请选择 macOS /dev/cu.… 串口')
    import serial  # Install pyserial in an isolated environment; not required for parser tests.
    with serial.Serial(port=None, baudrate=115200, timeout=0.2, write_timeout=2, exclusive=True) as port:
        port.dtr = False
        port.rts = False
        port.port = args.port
        port.open()
        if exchange(port, 'CAPS', 'CUBE-') != 'CUBE-CODEX/2':
            raise ValueError('设备不是 Cube；不会写入配置')
        print('当前状态:', decode_status(exchange(port, 'DONGLE', 'CUBE-SPLIT-BENCH/')))
        for side, address, kind in bindings:
            reply = exchange(port, f'DONGLE BIND {side} {address} {kind}', 'CUBE-SPLIT-BIND ')
            if reply != 'CUBE-SPLIT-BIND SAVED REBOOT-REQUIRED HID-OFF':
                raise ValueError('配置保存响应异常；停止后续配置')
            print(f'{side} 已保存；手动重启后生效，未清除配对、未重启设备。')
        if bindings:
            print('配置尚未应用。两侧配置保存完毕后，请手动重启 Cube 再运行无绑定参数的诊断。')


if __name__ == '__main__':
    try:
        main()
    except Exception as exc:
        raise SystemExit(f'错误: {exc}')
