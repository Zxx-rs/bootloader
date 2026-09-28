#!/bin/bash
#=============================================================================
# ECDSA P-256 密钥对生成工具
#
# 用法:
#   bash generate_keypair.sh
#
# 输出:
#   private_key.pem    私钥 (严格保密，不要分发！)
#   public_key.h       公钥 C 数组，粘贴到 BootLoader 的 firmware_sign.c 中
#
# 运行之后:
#   1. 将 public_key.h 的内容粘贴到 app/firmware_sign.c
#      (替换 ECDSA_PUBLIC_KEY 数组)
#   2. private_key.pem 妥善保管在开发机上，加入 .gitignore
#   3. 每次发布固件时，用 sign_firmware.py + private_key.pem 签名
#=============================================================================

set -e

echo "=== 生成 ECDSA P-256 密钥对 ==="

# ── 第1步: 生成私钥 ──
# ecparam -genkey: 生成 EC 密钥对
# prime256v1:      NIST P-256 曲线 (也叫 secp256r1)
openssl ecparam -genkey -name prime256v1 -out private_key.pem
echo "[OK] private_key.pem (私钥)"

# ── 第2步: 从私钥提取公钥 ──
# -pubout:         输出公钥
# -outform DER:    DER 二进制格式
# tail -c 64:      取最后 64 字节 = X坐标(32B) + Y坐标(32B)，去掉前缀 0x04
openssl ec -in private_key.pem -pubout -outform DER 2>/dev/null \
  | tail -c 64 > public_key.bin
echo "[OK] 公钥 (原始 64 字节)"

# ── 第3步: 生成 C 数组头文件 ──
# xxd -i: 将二进制转为 C 十六进制数组格式
echo "// ECDSA P-256 公钥 — 自动生成，粘贴到 firmware_sign.c" > public_key.h
echo "// 生成时间: $(date)" >> public_key.h
echo "static const uint8_t ECDSA_PUBLIC_KEY[64] = {" >> public_key.h
xxd -i < public_key.bin >> public_key.h
echo "};" >> public_key.h
echo "[OK] public_key.h"

# ── 清理临时文件 ──
rm -f public_key.bin

echo ""
echo "=== 完成 ==="
echo "  私钥: private_key.pem  (务必保密！不要提交到 git！)"
echo "  公钥: public_key.h     (粘贴到 app/firmware_sign.c 中)"
echo ""
echo "对固件签名:"
echo "  python tools/sign_firmware.py firmware.bin private_key.pem"
echo ""
echo "安全提醒:"
echo "  私钥 = 你的数字印章，任何人拿到它就能签发'合法'固件"
echo "  公钥 = 烧在设备里的验假锁，可以公开"
