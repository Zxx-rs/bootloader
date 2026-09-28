#!/usr/bin/env python3
"""
固件签名工具 — 使用 ECDSA P-256 (secp256r1 / NIST256p) 对固件进行数字签名。

签名端使用纯 Python 的 ecdsa 库，与 STM32 端的 uECC / micro-ecc 算法兼容。
输出裸的 r||s 签名（64 字节），与 uECC 验签的输入格式一致。

使用方法:
  pip install ecdsa
  python sign_firmware.py firmware.bin private_key.pem

输出文件:
  firmware_signed.bin   — 原始固件末尾拼接 64 字节签名

工作流程:
  firmware.bin (Keil 编译产出)
      │
      ├─→ SHA-256 哈希 ──→ 32 字节指纹
      │
      ├─→ 私钥签名 ──→ 64 字节裸签名 (r||s)
      │
      └─→ 拼接输出: firmware.bin + 64B 签名

  STM32 BootLoader 收到后:
      - 前 N-64 字节 → PROGRAM 指令写入 W25Q128 B区
      - 后 64 字节   → SET_FLAG 指令下发给 BootLoader 验签
"""

import sys
import os
import hashlib

try:
    from ecdsa import SigningKey          # ECDSA 签名密钥类
    from ecdsa.util import sigencode_string  # 输出裸 r||s 格式的编码器
except ImportError:
    print("错误: 需要安装 'ecdsa' 库。", file=sys.stderr)
    print("安装命令: pip install ecdsa", file=sys.stderr)
    sys.exit(1)


def sign_firmware(firmware_path, key_path):
    """对固件文件签名，输出 firmware_signed.bin"""

    # ── 第1步: 读取固件文件 ──
    with open(firmware_path, 'rb') as f:
        firmware = f.read()
    print(f"  固件文件: {firmware_path} ({len(firmware):,} 字节)")

    # ── 第2步: 计算 SHA-256 哈希 (得到 32 字节指纹) ──
    fw_hash = hashlib.sha256(firmware).digest()
    print(f"  SHA-256:  {fw_hash.hex()}")

    # ── 第3步: 加载私钥 (PEM 格式) ──
    with open(key_path, 'rb') as f:
        key_data = f.read()

    try:
        # 尝试 SEC1 格式 (OpenSSL ecparam -genkey 默认输出)
        sk = SigningKey.from_pem(key_data)
    except Exception:
        # 如果是 PKCS8 格式，去掉 PEM 头尾后解码
        from ecdsa.der import unpem
        key_data = unpem(key_data)
        sk = SigningKey.from_der(key_data)

    print(f"  私钥文件: {key_path}")

    # ── 第4步: 用私钥对哈希值签名 ──
    # sign_digest 直接对 32 字节裸哈希签名 (不额外做哈希)
    # sigencode_string 输出裸的 r||s (64 字节)，与 uECC 验签格式一致
    signature = sk.sign_digest(fw_hash, sigencode=sigencode_string)
    print(f"  签名:     {signature.hex()} (长度={len(signature)})")

    # ── 第5步: 拼接固件 + 签名，写入输出文件 ──
    base = os.path.splitext(firmware_path)[0]
    out_path = base + '_signed.bin'
    with open(out_path, 'wb') as f:
        f.write(firmware)     # 固件本体 (前 N 字节)
        f.write(signature)    # 签名 (末尾 64 字节)

    print(f"  输出文件: {out_path} ({len(firmware) + len(signature):,} 字节)")
    print(f"  → 上位机读取此文件:")
    print(f"     前 {len(firmware):,} 字节 → PROGRAM 指令发到 W25Q128 B区")
    print(f"     末 {len(signature)} 字节 → SET_FLAG 指令发给 BootLoader 验签")
    return out_path


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        print("用法:   python sign_firmware.py <固件.bin> <私钥.pem>")
        print("示例:   python sign_firmware.py firmware.bin private_key.pem")
        sys.exit(1)

    firmware_path = sys.argv[1]
    key_path = sys.argv[2]

    if not os.path.exists(firmware_path):
        print(f"错误: 找不到固件文件 {firmware_path}", file=sys.stderr)
        sys.exit(1)
    if not os.path.exists(key_path):
        print(f"错误: 找不到私钥文件 {key_path}", file=sys.stderr)
        sys.exit(1)

    try:
        sign_firmware(firmware_path, key_path)
    except Exception as e:
        print(f"\n错误: {e}", file=sys.stderr)
        sys.exit(1)


if __name__ == '__main__':
    main()
