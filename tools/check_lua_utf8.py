# -*- coding: utf-8 -*-
"""
check_lua_utf8.py - 巡检 D:\\srceng\\hl2sb\\lua 下所有 .lua 的编码。

HL2SB 的 Lua 是 Source Lua 5.1（无 BOM），所有 .lua 必须 UTF-8。

用法：
    python check_lua_utf8.py            # 只读巡检，列出非 UTF-8 文件并给根因
    python check_lua_utf8.py --fix      # 把非 UTF-8 文件就地转成 UTF-8（latin-1 -> utf-8）
    python check_lua_utf8.py --backup   # 转之前先把原始字节备份到 lua/_lua_enc_backup

设计要点：
  - 先按 BOM 判定，再尝试 UTF-8 严格解码；失败者再试 GBK、再 fallback latin-1。
  - Team Sandbox 存量文件是非 ASCII 只有 ©(0xa9) 的 latin-1；若出现中文/GBK 双字节，
    --fix 会拒绝并给出文件列表，避免把 GBK 当 latin-1 转出乱码（需要人工确认）。
  - 根因按目录归类输出，方便一眼看出「这批是 Team Sandbox 遗留 latin-1」。
"""

import sys, os

# lua 内容目录：HL2SB 游戏内容的 lua 根。默认 D:\srceng\hl2sb\lua，
# 可用环境变量 HL2SB_LUA 覆盖。
ROOT = os.path.abspath(os.path.normpath(os.environ.get("HL2SB_LUA", r"D:\srceng\hl2sb\lua")))

EXTS = {".lua"}

COPYRIGHT_BYTE = 0xA9  # ©（latin-1）


def analyse(b: bytes):
    """返回 (kind, reason)。kind: utf8 / utf8bom / latin1 / gbk / other"""
    if b.startswith(b"\xef\xbb\xbf"):
        # UTF-8 BOM：能被 vgui/lua 勉强容忍，但规范要求无 BOM，单独标记
        try:
            b[3:].decode("utf-8")
            return "utf8bom", "UTF-8 带 BOM（应去掉 BOM）"
        except UnicodeDecodeError:
            return "other", "UTF-8 BOM 之后仍是非法字节"
    try:
        b.decode("utf-8")
        return "utf8", "UTF-8（规范）"
    except UnicodeDecodeError:
        pass
    try:
        b.decode("gbk")
        return "gbk", "GBK 编码（含中文，勿按 latin-1 转！需人工确认）"
    except UnicodeDecodeError:
        pass
    # latin-1 总能成功。真正判定：非 ASCII 是否只剩 ©
    try:
        bad = {x for x in b if x > 0x7F}
    except Exception:
        bad = set(b) - {x for x in range(0x7F)}
    if bad <= {COPYRIGHT_BYTE}:
        return "latin1", "latin-1 编码（非 ASCII 仅 ©，安全性未知若仅含 © 可转）"
    return "other", "其它编码/字节（需人工检查）"


def collect():
    files = []
    for dp, _, fns in os.walk(ROOT):
        for fn in fns:
            if os.path.splitext(fn)[1].lower() not in EXTS:
                continue
            files.append(os.path.join(dp, fn))
    return files


def main():
    fix = "--fix" in sys.argv
    backup = "--backup" in sys.argv

    if not os.path.isdir(ROOT):
        print("[ERROR] lua 目录不存在：", ROOT)
        print("       请用环境变量 HL2SB_LUA 指定正确的 lua 路径。")
        sys.exit(2)

    rows = []   # (path, kind, reason)
    for p in collect():
        with open(p, "rb") as f:
            b = f.read()
        kind, reason = analyse(b)
        if kind == "utf8":
            continue
        rows.append((p, kind, reason))

    print("== Lua 编码巡检 ==")
    print("扫描目录：", ROOT)
    print("非 UTF-8 文件：%d 个\n" % len(rows))

    if not rows:
        print("OK - 全部为 UTF-8（无 BOM）。")
        return 0

    # 按根因归类输出
    groups = {}
    for p, kind, reason in rows:
        groups.setdefault((kind, reason), []).append(p)

    for (kind, reason), plist in sorted(groups.items(), key=lambda kv: kv[0][0]):
        print("[%s] %s - %d 个" % (kind, reason, len(plist)))
        for p in sorted(plist):
            print("   ", os.path.relpath(p, ROOT))
        print()

    if fix:
        # 只允许 latin1（且确认非 ASCII 只剩 ©）或 utf8bom 的自动修复；gbk/other 一律拒绝
        blocked = [(p, k, r) for (p, k, r) in rows if k not in ("latin1", "utf8bom")]
        if blocked:
            print("!! --fix 拒绝以下文件（可能是 GBK/中文或未知字节，先人工确认编码）：")
            for p, k, r in blocked:
                print("   ", os.path.relpath(p, ROOT), "->", k, r)
            print("   未转换任何文件。")
            return 1

        if backup:
            bdir = os.path.join(ROOT, "_lua_enc_backup")
            print("[backup] 原始字节备份到：", bdir)

        n = 0
        for p, kind, reason in rows:
            if kind == "utf8bom":
                with open(p, "rb") as f:
                    b = f.read()
                content = b[3:].decode("utf-8")     # 去 BOM
                if backup:
                    _cp(p, bdir)
                with open(p, "wb") as f:
                    f.write(content.encode("utf-8"))
            else:  # latin1 -> utf-8
                with open(p, "rb") as f:
                    b = f.read()
                text = b.decode("latin-1")
                if backup:
                    _cp(p, bdir)
                with open(p, "wb") as f:
                    f.write(text.encode("utf-8"))
            n += 1
            print("   CONVERTED:", os.path.relpath(p, ROOT))
        print("\n已转换 %d 个文件 -> UTF-8（无 BOM）。" % n)
        return 0

    print("提示：加 --fix 可自动转换（仅 latin-1/© 与 BOM；GBK/其它会拒绝）。")
    print("      加 --backup 会在转换前把原字节备份到 lua/_lua_enc_backup。")
    return 1


def _cp(path, dest_root):
    import shutil
    rel = os.path.relpath(path, ROOT)
    out = os.path.join(dest_root, rel)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    shutil.copy2(path, out)


if __name__ == "__main__":
    sys.exit(main())
