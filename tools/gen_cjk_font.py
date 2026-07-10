#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
生成班级宠物园设备用的 LVGL 二进制中文字库 cjk16.bin
- 从 macOS 系统字体 STHeiti Medium.ttc 抽取“简体中文”字体面 (Heiti SC Medium)
- 覆盖: ASCII 可打印字符 + GB2312 全部汉字(6763) + 常用全角标点
- 输出 LVGL 9 binfont 格式, 由固件 lv_binfont_create_from_buffer() 从 TF 卡载入
用法:
  python3 gen_cjk_font.py [--size 16] [--bpp 2] [--out cjk16.bin]
依赖:
  pip install fonttools
  npm install lv_font_conv   (二进制位于 node_modules/.bin/lv_font_conv)
"""
import argparse
import os
import subprocess
import sys

# ---- 路径配置 ----
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
TTC_PATH = "/System/Library/Fonts/STHeiti Medium.ttc"
FONT_FACE_INDEX = 1  # Heiti SC Medium (简体中文)
TEMP_TTF = "/tmp/heiti_sc_medium.ttf"
LV_FONT_CONV = "/Users/Terry/.workbuddy/binaries/node/workspace/node_modules/.bin/lv_font_conv"
NODE_BIN = "/Users/Terry/.workbuddy/binaries/node/versions/22.22.2/bin/node"
PYTHON_BIN = "/Users/Terry/.workbuddy/binaries/python/envs/default/bin/python"


def extract_face():
    """抽取 TTC 中的简体中文面为单文件 TTF (lv_font_conv 不支持 ttc 集合)"""
    from fontTools.ttLib import TTCollection
    print(f"[1/4] 抽取字体面 {FONT_FACE_INDEX} 从 {TTC_PATH} ...")
    coll = TTCollection(TTC_PATH)
    names = [f["name"].getDebugName(4) for f in coll.fonts]
    print("      可用字体面:", names)
    font = coll.fonts[FONT_FACE_INDEX]
    font.save(TEMP_TTF)
    print(f"      已保存临时 TTF -> {TEMP_TTF}")


def build_symbols():
    """生成字符清单: ASCII + GB2312 全量汉字 + 常用全角标点"""
    print("[2/4] 枚举 GB2312 字符集 ...")
    chars = set()
    # ASCII 可打印 0x20-0x7E
    for c in range(0x20, 0x7F):
        chars.add(chr(c))
    # GB2312-80: 高字节 0xA1-0xF7, 低字节 0xA1-0xFE (跳过 0x7F)
    count = 0
    for hi in range(0xA1, 0xF8):
        for lo in range(0xA1, 0xFF):
            if lo == 0x7F:
                continue
            try:
                ch = bytes([hi, lo]).decode("gb2312")
                if ch and ch != "\ufffd":
                    chars.add(ch)
                    count += 1
            except Exception:
                continue
    # 补充若干常用符号/全角字符 (部分已在 GB2312 内, 这里确保)
    extra = "·…—“”‘’《》【】、，。！？：；“”‘’（）％＆＊＃＠～＋－＝／＼｜＜＞　"
    for c in extra:
        chars.add(c)
    symbols = "".join(sorted(chars))
    print(f"      共 {len(symbols)} 个字符 (其中 GB2312 汉字约 {count} 个)")
    return symbols


def convert(symbols, size, bpp, out_path):
    print(f"[3/4] 调用 lv_font_conv 生成 binfont (size={size}, bpp={bpp}) ...")
    cmd = [
        NODE_BIN, LV_FONT_CONV,
        "--font", TEMP_TTF,
        "--format", "bin",
        "--size", str(size),
        "--bpp", str(bpp),
        "--symbols", symbols,
        "-o", out_path,
    ]
    # 使用 subprocess 直接传参, 避免 shell 对大量 CJK 字符的引号问题
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        print("lv_font_conv 失败:\n", r.stdout, r.stderr, file=sys.stderr)
        sys.exit(1)
    size_kb = os.path.getsize(out_path) / 1024.0
    print(f"      已生成 -> {out_path} ({size_kb:.1f} KB)")


def verify(out_path):
    print("[4/4] 校验 binfont 文件头 ...")
    with open(out_path, "rb") as f:
        head = f.read(4)
    if head == b"head":
        print("      ✅ 文件头 'head' 正确, 可被 lv_binfont_create_from_buffer 加载")
    else:
        print(f"      ⚠️ 文件头异常: {head}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--size", type=int, default=16)
    ap.add_argument("--bpp", type=int, default=2, choices=[1, 2, 3, 4, 8])
    ap.add_argument("--out", default=os.path.join(SCRIPT_DIR, "cjk16.bin"))
    args = ap.parse_args()

    extract_face()
    symbols = build_symbols()
    convert(symbols, args.size, args.bpp, args.out)
    verify(args.out)
    print("\n完成! 将生成的 .bin 拷贝到 TF 卡根目录即可 (固件会从 /cjk16.bin 载入).")


if __name__ == "__main__":
    main()
