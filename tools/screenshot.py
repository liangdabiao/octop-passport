#!/usr/bin/env python3
"""串口截屏：向设备发 FAP_SCREENSHOT_V1 命令，收 RGB565LE 帧存 PNG。
用法：python tools/screenshot.py [输出.png]"""
import sys, struct, zlib
import serial

PORT = "COM3"
OUT = sys.argv[1] if len(sys.argv) > 1 else "screen.png"

ser = serial.Serial(PORT, 115200, timeout=1)
ser.reset_input_buffer()
ser.write(b"FAP_SCREENSHOT_V1\n")

# 头行：FAP_SCREENSHOT_V1 <宽> <高> RGB565LE <字节数>
header = b""
deadline = __import__("time").time() + 5
while time_left := deadline - __import__("time").time():
    line = ser.readline()
    if line.startswith(b"FAP_SCREENSHOT_V1"):
        header = line.strip()
        break
parts = header.split()
w, h, nbytes = int(parts[1]), int(parts[2]), int(parts[4])
print(f"截屏 {w}x{h}, {nbytes} 字节")

raw = ser.read(nbytes)
while len(raw) < nbytes:
    more = ser.read(nbytes - len(raw))
    if not more:
        break
    raw += more
ser.close()
assert len(raw) == nbytes, f"只收到 {len(raw)}/{nbytes}"

# RGB565LE → PNG（每像素扩展到 RGB888）。
rows = []
for y in range(h):
    row = bytearray()
    for x in range(w):
        px = struct.unpack_from("<H", raw, (y * w + x) * 2)[0]
        r = ((px >> 11) & 0x1F) << 3
        g = ((px >> 5) & 0x3F) << 2
        b = (px & 0x1F) << 3
        row += bytes((r, g, b))
    rows.append(b"\x00" + bytes(row))

def chunk(tag, data):
    c = struct.pack(">I", len(data)) + tag + data
    return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

png = (b"\x89PNG\r\n\x1a\n"
       + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
       + chunk(b"IDAT", zlib.compress(b"".join(rows), 9))
       + chunk(b"IEND", b""))
open(OUT, "wb").write(png)
print(f"已保存 {OUT}")
