#!/bin/bash
# 分段烧录脚本：规避 ESP32-S3 原生 USB CDC 写 Flash 中途掉线
# 原理：把主固件切成 64KB 小段逐段写入，每段写操作短，USB 不会超时断开
set -u

PORT=/dev/cu.usbmodem141201
ESPTOOL=/Users/Terry/Library/Arduino15/packages/esp32/tools/esptool_py/4.5.1/esptool
BUILD=/Users/Terry/Downloads/class-pet-main/build
BOOT_APP0=/Users/Terry/Library/Arduino15/packages/esp32/hardware/esp32/2.0.17/tools/partitions/boot_app0.bin

echo "=== [1/2] 烧录 bootloader + partitions + boot_app0 (保持 download 模式) ==="
"$ESPTOOL" --chip esp32s3 --port "$PORT" --baud 460800 --before default_reset --after no_reset write_flash -z \
  0x0000 "$BUILD/ClassPetDevice.ino.bootloader.bin" \
  0x8000 "$BUILD/ClassPetDevice.ino.partitions.bin" \
  0xe000 "$BOOT_APP0"
if [ $? -ne 0 ]; then echo "❌ 小段烧录失败"; exit 1; fi

echo "=== [2/2] 主固件分段烧录 (每块 64KB) ==="
BIN="$BUILD/ClassPetDevice.ino.bin"
SIZE=$(stat -f%z "$BIN")
CHUNK=65536
BASE=65536   # 0x10000
i=0
pos=0
while [ "$pos" -lt "$SIZE" ]; do
  dd if="$BIN" of="/tmp/cpk_$i.bin" bs=65536 skip="$i" count=1 2>/dev/null
  addr=$(( BASE + pos ))
  if [ $(( pos + CHUNK )) -ge "$SIZE" ]; then
    after="hard_reset"
  else
    after="no_reset"
  fi
  printf "  → 段 %d: 偏移 0x%X  大小 %d 字节 (after=%s)\n" "$i" "$addr" "$CHUNK" "$after"
  "$ESPTOOL" --chip esp32s3 --port "$PORT" --baud 460800 --before no_reset --after "$after" write_flash -z "$addr" "/tmp/cpk_$i.bin"
  if [ $? -ne 0 ]; then
    echo "❌ 段 $i 烧录失败 (偏移 0x$(printf %X $addr))"
    exit 2
  fi
  pos=$(( pos + CHUNK ))
  i=$(( i + 1 ))
done
echo "✅ 分段烧录完成，共 $i 段，主固件大小 $SIZE 字节"
