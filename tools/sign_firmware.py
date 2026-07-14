#!/usr/bin/env python3
"""
Firmware signing tool — ECDSA P-256 (secp256r1 / NIST256p).

Uses pure-Python 'ecdsa' library (compatible with STM32 uECC / micro-ecc).
Outputs raw r||s signature (64 bytes) — same format uECC expects.

Usage:
  pip install ecdsa
  python sign_firmware.py firmware.bin private_key.pem

Output:
  firmware_signed.bin   firmware + 64-byte raw signature appended
"""

import sys
import os
import hashlib

try:
    from ecdsa import SigningKey
    from ecdsa.util import sigencode_string
except ImportError:
    print("Error: 'ecdsa' package required.", file=sys.stderr)
    print("Install: pip install ecdsa", file=sys.stderr)
    sys.exit(1)


def sign_firmware(firmware_path, key_path):
    """Sign firmware and write firmware_signed.bin."""

    # Read firmware
    with open(firmware_path, 'rb') as f:
        firmware = f.read()
    print(f"  Firmware: {firmware_path} ({len(firmware):,} bytes)")

    # SHA-256 hash
    fw_hash = hashlib.sha256(firmware).digest()
    print(f"  SHA-256:  {fw_hash.hex()}")

    # Load private key (PEM format, SEC1 or PKCS8)
    with open(key_path, 'rb') as f:
        key_data = f.read()

    # Try SEC1 first, then PKCS8
    try:
        sk = SigningKey.from_pem(key_data)
    except Exception:
        # PKCS8 format — strip headers and decode raw key
        from ecdsa.der import unpem
        key_data = unpem(key_data)
        sk = SigningKey.from_der(key_data)

    print(f"  Key:      {key_path}")

    # Sign hash directly (no double-hash — sigencode_string uses raw r||s)
    signature = sk.sign_digest(fw_hash, sigencode=sigencode_string)
    print(f"  Signature: {signature.hex()} (len={len(signature)})")

    # Write signed firmware
    base = os.path.splitext(firmware_path)[0]
    out_path = base + '_signed.bin'
    with open(out_path, 'wb') as f:
        f.write(firmware)
        f.write(signature)

    print(f"  Output:   {out_path} ({len(firmware) + len(signature):,} bytes)")
    print(f"  -> Host tool: read {out_path}")
    print(f"     - first {len(firmware):,} bytes → PROGRAM command")
    print(f"     - last  {len(signature)} bytes → SET_FLAG signature payload")
    return out_path


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        print("Usage:   python sign_firmware.py <firmware.bin> <private_key.pem>")
        print("Example: python sign_firmware.py firmware.bin private_key.pem")
        sys.exit(1)

    firmware_path = sys.argv[1]
    key_path = sys.argv[2]

    if not os.path.exists(firmware_path):
        print(f"Error: {firmware_path} not found", file=sys.stderr)
        sys.exit(1)
    if not os.path.exists(key_path):
        print(f"Error: {key_path} not found", file=sys.stderr)
        sys.exit(1)

    try:
        sign_firmware(firmware_path, key_path)
    except Exception as e:
        print(f"\nError: {e}", file=sys.stderr)
        sys.exit(1)


if __name__ == '__main__':
    main()
