#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
生成班级宠物园设备用的 LVGL 二进制中文字库 cjk16.bin
- 从 macOS 系统字体 STHeiti Medium.ttc 抽取“简体中文”字体面 (Heiti SC Medium)
- 覆盖: ASCII 可打印字符 + GB2312 全部汉字(6763) + 常用全角标点
- 输出 LVGL 9 binfont 格式, 由固件 lv_binfont_create_from_buffer() 从内存缓冲区载入
用法:
  python3 gen_cjk_font.py [--size 16] [--bpp 2] [--out cjk16.bin] [--compress]
依赖:
  pip install fonttools
  npm install lv_font_conv   (二进制位于 node_modules/.bin/lv_font_conv)

⚠️ 默认【不压缩】(compression_id=0 / PLAIN): 这样固件无需依赖 lv_conf.h 的
   LV_USE_FONT_COMPRESSED 开关, 即使该开关被重置为 0 也不会出现"全屏文字消失"。
   仅在确实需要更小体积且已确认开启 LV_USE_FONT_COMPRESSED=1 时, 才加 --compress。
   2026-07-11 修复: 之前生成的是压缩字体, 但 lv_conf.h 的压缩开关为 0, 导致设备上
   字体加载成功却每个字形位图 return NULL → 文字全部不显示。
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


def convert(symbols, size, bpp, out_path, compress):
    print(f"[3/4] 调用 lv_font_conv 生成 binfont (size={size}, bpp={bpp}, compress={compress}) ...")
    cmd = [
        NODE_BIN, LV_FONT_CONV,
        "--font", TEMP_TTF,
        "--format", "bin",
        "--size", str(size),
        "--bpp", str(bpp),
        "--symbols", symbols,
        "-o", out_path,
    ]
    # 默认不压缩 (PLAIN): 避免依赖 lv_conf.h 的 LV_USE_FONT_COMPRESSED 开关,
    # 否则该开关为 0 时字体"加载成功但字形空白" → 全屏文字消失。
    if not compress:
        cmd.append("--no-compress")
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
        f.seek(4)  # 偏移0是 length 字段, "head" 标签在偏移4
        head = f.read(4)
        f.seek(0x29)
        compression_id = f.read(1)[0]
    if head == b"head":
        print("      ✅ 文件头 'head' 正确, 可被 lv_binfont_create_from_buffer 加载")
    else:
        print(f"      ⚠️ 文件头异常: {head}")
    if compression_id == 0:
        print("      ✅ compression_id=0 (PLAIN, 不依赖 LV_USE_FONT_COMPRESSED)")
    else:
        print(f"      ⚠️ compression_id={compression_id} (压缩, 需 lv_conf.h 开启 LV_USE_FONT_COMPRESSED=1)")


def bin_to_c(bin_path, c_path):
    """将生成的 .bin 转换为固件内嵌的 C 数组 (cjk16_bin.c), 与现有格式保持一致"""
    print("[5/5] 生成固件内嵌 C 数组 cjk16_bin.c ...")
    with open(bin_path, "rb") as f:
        data = f.read()
    lines = []
    for i in range(0, len(data), 16):
        chunk = data[i:i + 16]
        lines.append("  " + ", ".join(f"0x{b:02X}" for b in chunk) + ",")
    body = "\n".join(lines)
    c_text = (
        "#include \"cjk16_bin.h\"\n\n"
        "/* 中文字库 cjk16.bin (LVGL bin font, GB2312, 不压缩/PLAIN), 编译进固件,\n"
        "   运行时通过 lv_binfont_create_from_buffer 加载。由 tools/gen_cjk_font.py 自动生成, 不要手改。 */\n"
        "const uint8_t cjk16_bin[] = {\n"
        f"{body}\n"
        "};\n"
        "const unsigned int cjk16_bin_len = (unsigned int)sizeof(cjk16_bin);\n"
    )
    with open(c_path, "w") as f:
        f.write(c_text)
    print(f"      ✅ 已写入 -> {c_path} ({len(data)} 字节)")


# 固件内嵌 C 数组输出路径 (tools/../firmware/ClassPetDevice/cjk16_bin.c)
CJK_BIN_C = os.path.normpath(os.path.join(SCRIPT_DIR, "..", "firmware", "ClassPetDevice", "cjk16_bin.c"))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--size", type=int, default=16)
    ap.add_argument("--bpp", type=int, default=2, choices=[1, 2, 3, 4, 8])
    ap.add_argument("--out", default=os.path.join(SCRIPT_DIR, "cjk16.bin"))
    ap.add_argument("--compress", action="store_true",
                    help="生成压缩字体(需 lv_conf.h 开启 LV_USE_FONT_COMPRESSED=1)。默认不压缩。")
    args = ap.parse_args()

    extract_face()
    symbols = build_symbols()
    convert(symbols, args.size, args.bpp, args.out, args.compress)
    verify(args.out)
    bin_to_c(args.out, CJK_BIN_C)
    print("\n完成! cjk16.bin 与固件内嵌 cjk16_bin.c 均已更新, 直接重新编译烧录固件即可。")


if __name__ == "__main__":
    main()
