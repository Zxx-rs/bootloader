#!/usr/bin/env python3
"""
OneNET MQTT Token 计算工具

用于计算 OneNET 多协议接入（MQTT）的设备连接密码（Token）。
生成结果可直接填入 MQTTX 客户端或 STM32 ota_config.h。

用法:
  python onenet_token.py <ProductID> <DeviceName> <AccessKey> [--years 5]
  python onenet_token.py                         # 交互式输入

参考: OneNET 多协议接入 MQTT 鉴权文档
"""

import base64
import hmac
import hashlib
import time
import argparse
import sys


def calc_onenet_token(product_id: str, device_name: str, access_key: str,
                      expire_years: int = 5, method: str = "sha1") -> str:
    """
    计算 OneNET MQTT 连接 Password (Token)

    参数:
        product_id:  产品 ID（纯数字字符串）
        device_name: 设备名称
        access_key:  设备 AuthCode / AccessKey
        expire_years: Token 有效年数（默认 5 年）
        method:      签名算法（sha1 / md5 / sha256）

    返回:
        Base64 编码的 Token 字符串，直接用作 MQTT Password
    """
    version = "2018-10-31"
    res = f"products/{product_id}/devices/{device_name}"

    # 过期时间戳（Unix 秒数）
    et = str(int(time.time()) + 86400 * 365 * expire_years)

    # 拼接待签名字符串: key=value 按首字母排序
    token_raw = f"et={et}&method={method}&res={res}&version={version}"

    # 计算签名
    if method == "sha1":
        h = hmac.new(access_key.encode(), token_raw.encode(), hashlib.sha1)
    elif method == "sha256":
        h = hmac.new(access_key.encode(), token_raw.encode(), hashlib.sha256)
    elif method == "md5":
        # MD5 模式: 直接 MD5(access_key + et)，而非 HMAC
        md5 = hashlib.md5((access_key + et).encode()).digest()
        sign = base64.b64encode(md5).decode()
        full = f"{token_raw}&sign={sign}"
        return base64.b64encode(full.encode()).decode()
    else:
        raise ValueError(f"Unsupported method: {method}")

    sign = base64.b64encode(h.digest()).decode()

    # 最终 Token = Base64(token_raw + &sign=xxx)
    full = f"{token_raw}&sign={sign}"
    return base64.b64encode(full.encode()).decode()


def main():
    parser = argparse.ArgumentParser(
        description="OneNET MQTT Token 计算工具",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
示例:
  python onenet_token.py 12345678 stm32_001 my_access_key
  python onenet_token.py 12345678 stm32_001 my_access_key --years 10
  python onenet_token.py                           # 交互式输入
        """
    )
    parser.add_argument("product_id", nargs="?", help="产品 ID（纯数字）")
    parser.add_argument("device_name", nargs="?", help="设备名称")
    parser.add_argument("access_key", nargs="?", help="设备 AuthCode / AccessKey")
    parser.add_argument("--years", type=int, default=5, help="Token 有效年数（默认 5）")
    parser.add_argument("--method", default="sha1",
                        choices=["sha1", "md5", "sha256"],
                        help="签名算法（默认 sha1）")
    args = parser.parse_args()

    # 交互式输入
    pid = args.product_id or input("ProductID: ").strip()
    dn  = args.device_name or input("DeviceName: ").strip()
    ak  = args.access_key  or input("AccessKey: ").strip()

    if not pid or not dn or not ak:
        print("错误: ProductID / DeviceName / AccessKey 不能为空")
        sys.exit(1)

    token = calc_onenet_token(pid, dn, ak, args.years, args.method)
    et = int(time.time()) + 86400 * 365 * args.years

    # ── 输出 ──────────────────────────────────────────
    print()
    print("=" * 62)
    print("  OneNET MQTT 连接参数")
    print("=" * 62)
    print(f"  Broker:    mqtt.heclouds.com")
    print(f"  Port:      1883")
    print(f"  Client ID: {dn}")
    print(f"  Username:  {pid}")
    print(f"  Password:  {token}")
    print("-" * 62)
    print(f"  Token 过期: {time.ctime(et)} ({args.years} 年后)")
    print(f"  签名算法:   {args.method}")
    print("=" * 62)
    print()

    # ── MQTTX 导入格式 ──
    print("MQTTX 快速配置:")
    print(f"  Name:      STM32_{dn}")
    print(f"  Host:      mqtt.heclouds.com")
    print(f"  Port:      1883")
    print(f"  Client ID: {dn}")
    print(f"  Username:  {pid}")
    print(f"  Password:  {token}")
    print()

    # ── C 代码片段 ──
    print("ota_config.h 代码片段:")
    print(f'  #define ONENET_BROKER    "mqtt.heclouds.com"')
    print(f'  #define ONENET_PORT      1883')
    print(f'  #define ONENET_CLIENT_ID "{dn}"')
    print(f'  #define ONENET_USERNAME  "{pid}"')
    print(f'  #define ONENET_PASSWORD  "{token}"')
    print()

    return token


if __name__ == "__main__":
    main()
