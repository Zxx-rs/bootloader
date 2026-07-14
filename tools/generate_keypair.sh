#!/bin/bash
#=============================================================================
# ECDSA P-256 key pair generator for firmware signing
#
# Usage:
#   bash generate_keypair.sh
#
# Output:
#   private_key.pem    — PRIVATE KEY (keep secret, DO NOT distribute!)
#   public_key.h       — C header with public key for BootLoader compilation
#
# After running:
#   1. Copy the content of public_key.h into app/firmware_sign.c
#      (replace the ECDSA_PUBLIC_KEY array)
#   2. Keep private_key.pem in a secure location (developer workstation, CI vault)
#   3. Use private_key.pem with the host tool to sign firmware before OTA
#=============================================================================

set -e

echo "=== Generating ECDSA P-256 key pair ==="

# Generate private key
openssl ecparam -genkey -name prime256v1 -out private_key.pem
echo "[OK] private_key.pem"

# Extract raw public key (64 bytes: X||Y)
openssl ec -in private_key.pem -pubout -outform DER 2>/dev/null \
  | tail -c 64 > public_key.bin
echo "[OK] public key (raw 64 bytes)"

# Generate C header
echo "// Auto-generated ECDSA P-256 public key — paste into firmware_sign.c" > public_key.h
echo "// Generated: $(date)" >> public_key.h
echo "static const uint8_t ECDSA_PUBLIC_KEY[64] = {" >> public_key.h
xxd -i < public_key.bin >> public_key.h
echo "};" >> public_key.h
echo "[OK] public_key.h"

# Clean up
rm -f public_key.bin

echo ""
echo "=== Done ==="
echo "  Private key: private_key.pem  (KEEP SECRET!)"
echo "  Public key:  public_key.h     (paste into app/firmware_sign.c)"
echo ""
echo "To sign firmware:"
echo "  openssl dgst -sha256 -sign private_key.pem firmware.bin | tail -c 64 > signature.bin"
