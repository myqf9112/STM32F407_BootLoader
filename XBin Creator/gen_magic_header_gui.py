#! /usr/bin/env python3
# -*- coding: utf-8 -*-

"""
Magic Header Generator - GUI
为 STM32F407 BootLoader 生成带 magic header 的升级固件包 (.xbin)
"""

import os
import sys
import time
import zlib
import tkinter as tk
from tkinter import ttk, filedialog, messagebox

# ============================================================
# 默认配置
# ============================================================
MAGIC_HEADER_MAGIC = 0x4D414749  # "MAGI"
DATA_TYPE_FIRMWARE = 1
DATA_OFFSET = 4096
DATA_ADDRESS = 0x08010000
THIS_ADDRESS = 0x0800C000
BL_VERSION_MAJOR = 1
BL_VERSION_MINOR = 0
BL_VERSION_PATCH = 0
BL_VERSION_EXTRA = "alpha"


def crc32(data: bytes) -> int:
    """计算 CRC32（与 STM32 硬件 CRC 兼容：输出异或 0xFFFFFFFF）"""
    return zlib.crc32(data) & 0xFFFFFFFF


def build_xbin(bin_data: bytes, config: dict) -> bytes:
    """根据配置构建 magic header + 固件数据的 .xbin 文件内容"""
    header_parts = []

    # --- 固定头 ---
    header_parts.append(config["magic"].to_bytes(4, "little"))
    header_parts.append((0).to_bytes(4, "little"))          # bitmask
    header_parts.append((0).to_bytes(4 * 6, "little"))      # reserved1

    # --- 固件描述 ---
    header_parts.append(config["data_type"].to_bytes(4, "little"))
    header_parts.append(config["data_offset"].to_bytes(4, "little"))
    header_parts.append(config["data_address"].to_bytes(4, "little"))
    header_parts.append(len(bin_data).to_bytes(4, "little"))
    header_parts.append(crc32(bin_data).to_bytes(4, "little"))  # data_crc32
    header_parts.append((0).to_bytes(4 * 11, "little"))     # reserved2

    # --- 版本字符串 ---
    version_date = time.strftime("%y%m%d", time.localtime())
    version_time = time.strftime("%H%M", time.localtime())
    version_str = (
        f"v{config['ver_major']}.{config['ver_minor']}.{config['ver_patch']}"
        f"-{version_date}-{version_time}-{config['ver_extra']}"
    )
    version_bytes = version_str.encode("ascii").ljust(128, b"\x00")
    header_parts.append(version_bytes)

    # --- 尾 ---
    header_parts.append((0).to_bytes(4 * 6, "little"))      # reserved3
    header_parts.append(config["this_address"].to_bytes(4, "little"))
    header_parts.append((0).to_bytes(4, "little"))           # this_crc32 占位

    # 拼接 header 结构体（不含尾部 padding）
    header = b"".join(header_parts)

    # 计算 this_crc32：对结构体中 this_crc32 字段之前的所有字节做 CRC32
    this_crc = crc32(header[:-4])  # 去掉末尾的 4 字节占位 0
    header = bytearray(header)
    header[-4:] = this_crc.to_bytes(4, "little")
    header = bytes(header)

    # 填充到 data_offset 大小
    header = header.ljust(config["data_offset"], b"\x00")

    return header + bin_data


# ============================================================
# GUI
# ============================================================
class MagicHeaderApp:
    def __init__(self, root: tk.Tk):
        self.root = root
        self.root.title("Magic Header Generator")
        self.root.resizable(False, False)

        # 输入文件路径
        self.input_path = tk.StringVar()
        self.output_dir = tk.StringVar(value=os.path.realpath("."))

        # header 参数
        self.var_magic = tk.StringVar(value=f"0x{MAGIC_HEADER_MAGIC:08X}")
        self.var_data_type = tk.StringVar(value=str(DATA_TYPE_FIRMWARE))
        self.var_data_offset = tk.StringVar(value=str(DATA_OFFSET))
        self.var_data_address = tk.StringVar(value=f"0x{DATA_ADDRESS:08X}")
        self.var_this_address = tk.StringVar(value=f"0x{THIS_ADDRESS:08X}")
        self.var_ver_major = tk.StringVar(value=str(BL_VERSION_MAJOR))
        self.var_ver_minor = tk.StringVar(value=str(BL_VERSION_MINOR))
        self.var_ver_patch = tk.StringVar(value=str(BL_VERSION_PATCH))
        self.var_ver_extra = tk.StringVar(value=BL_VERSION_EXTRA)

        self._build_ui()

    # ---- UI 构建 ----
    def _build_ui(self):
        pad = {"padx": 8, "pady": 4}
        root = self.root

        # 文件选择
        file_frame = ttk.LabelFrame(root, text="文件路径", padding=8)
        file_frame.pack(fill="x", padx=10, pady=(10, 0))

        ttk.Label(file_frame, text="输入 .bin:").grid(row=0, column=0, sticky="w", **pad)
        ttk.Entry(file_frame, textvariable=self.input_path, width=52).grid(row=0, column=1, **pad)
        ttk.Button(file_frame, text="浏览...", command=self._browse_input).grid(row=0, column=2, **pad)

        ttk.Label(file_frame, text="输出目录:").grid(row=1, column=0, sticky="w", **pad)
        ttk.Entry(file_frame, textvariable=self.output_dir, width=52).grid(row=1, column=1, **pad)
        ttk.Button(file_frame, text="浏览...", command=self._browse_output).grid(row=1, column=2, **pad)

        # 参数配置
        cfg_frame = ttk.LabelFrame(root, text="Header 配置", padding=8)
        cfg_frame.pack(fill="x", padx=10, pady=(10, 0))

        row = 0
        ttk.Label(cfg_frame, text="Magic:").grid(row=row, column=0, sticky="w", **pad)
        ttk.Entry(cfg_frame, textvariable=self.var_magic, width=18).grid(row=row, column=1, sticky="w", **pad)

        ttk.Label(cfg_frame, text="Data Type:").grid(row=row, column=2, sticky="w", **pad)
        ttk.Entry(cfg_frame, textvariable=self.var_data_type, width=18).grid(row=row, column=3, sticky="w", **pad)

        row += 1
        ttk.Label(cfg_frame, text="Data Offset:").grid(row=row, column=0, sticky="w", **pad)
        ttk.Entry(cfg_frame, textvariable=self.var_data_offset, width=18).grid(row=row, column=1, sticky="w", **pad)

        ttk.Label(cfg_frame, text="Data Address:").grid(row=row, column=2, sticky="w", **pad)
        ttk.Entry(cfg_frame, textvariable=self.var_data_address, width=18).grid(row=row, column=3, sticky="w", **pad)

        row += 1
        ttk.Label(cfg_frame, text="This Address:").grid(row=row, column=0, sticky="w", **pad)
        ttk.Entry(cfg_frame, textvariable=self.var_this_address, width=18).grid(row=row, column=1, sticky="w", **pad)

        # 版本号
        row += 1
        ttk.Label(cfg_frame, text="版本:").grid(row=row, column=0, sticky="w", **pad)
        ver_frame = ttk.Frame(cfg_frame)
        ver_frame.grid(row=row, column=1, columnspan=3, sticky="w", **pad)
        ttk.Entry(ver_frame, textvariable=self.var_ver_major, width=5).pack(side="left")
        ttk.Label(ver_frame, text=".").pack(side="left")
        ttk.Entry(ver_frame, textvariable=self.var_ver_minor, width=5).pack(side="left")
        ttk.Label(ver_frame, text=".").pack(side="left")
        ttk.Entry(ver_frame, textvariable=self.var_ver_patch, width=5).pack(side="left")
        ttk.Label(ver_frame, text="  extra:").pack(side="left")
        ttk.Entry(ver_frame, textvariable=self.var_ver_extra, width=10).pack(side="left")

        # 生成按钮
        btn_frame = ttk.Frame(root)
        btn_frame.pack(fill="x", padx=10, pady=10)
        self.btn_generate = ttk.Button(btn_frame, text="生成 .xbin", command=self._generate)
        self.btn_generate.pack()

        # 状态输出
        status_frame = ttk.LabelFrame(root, text="输出日志", padding=8)
        status_frame.pack(fill="both", expand=True, padx=10, pady=(0, 10))
        self.status_text = tk.Text(status_frame, height=8, width=72, state="disabled", font=("Consolas", 9))
        self.status_text.pack(fill="both", expand=True)

    # ---- 辅助 ----
    def _log(self, msg: str):
        self.status_text.configure(state="normal")
        self.status_text.insert("end", msg + "\n")
        self.status_text.see("end")
        self.status_text.configure(state="disabled")
        self.root.update()

    def _parse_hex_or_int(self, s: str) -> int:
        s = s.strip()
        if s.lower().startswith("0x"):
            return int(s, 16)
        return int(s)

    # ---- 事件处理 ----
    def _browse_input(self):
        path = filedialog.askopenfilename(
            title="选择固件 .bin 文件",
            filetypes=[("Binary files", "*.bin"), ("All files", "*.*")],
        )
        if path:
            self.input_path.set(os.path.realpath(path))

    def _browse_output(self):
        path = filedialog.askdirectory(title="选择输出目录")
        if path:
            self.output_dir.set(os.path.realpath(path))

    def _generate(self):
        # 清日志
        self.status_text.configure(state="normal")
        self.status_text.delete("1.0", "end")
        self.status_text.configure(state="disabled")

        # 校验输入文件
        bin_path = self.input_path.get().strip()
        if not bin_path:
            messagebox.showerror("错误", "请选择输入 .bin 文件")
            return
        if not os.path.isfile(bin_path):
            messagebox.showerror("错误", f"文件不存在:\n{bin_path}")
            return

        # 解析参数
        try:
            config = {
                "magic":        self._parse_hex_or_int(self.var_magic.get()),
                "data_type":    self._parse_hex_or_int(self.var_data_type.get()),
                "data_offset":  self._parse_hex_or_int(self.var_data_offset.get()),
                "data_address": self._parse_hex_or_int(self.var_data_address.get()),
                "this_address": self._parse_hex_or_int(self.var_this_address.get()),
                "ver_major":    int(self.var_ver_major.get()),
                "ver_minor":    int(self.var_ver_minor.get()),
                "ver_patch":    int(self.var_ver_patch.get()),
                "ver_extra":    self.var_ver_extra.get().strip(),
            }
        except ValueError as e:
            messagebox.showerror("错误", f"参数解析失败:\n{e}")
            return

        # 读 bin
        self._log(f"读取: {bin_path}")
        with open(bin_path, "rb") as f:
            bin_data = f.read()
        self._log(f"固件大小: {len(bin_data)} 字节 ({len(bin_data)/1024:.1f} KB)")

        # 构建 xbin
        self._log("构建 magic header ...")
        xbin_data = build_xbin(bin_data, config)
        header_size = config["data_offset"]
        total_size = len(xbin_data)
        self._log(f"Header: {header_size} B  |  固件: {len(bin_data)} B  |  总计: {total_size} B")

        # 输出文件
        base = os.path.splitext(os.path.basename(bin_path))[0]
        out_name = f"{base}_upgrade.xbin"
        out_path = os.path.join(self.output_dir.get(), out_name)
        os.makedirs(os.path.dirname(out_path), exist_ok=True)
        with open(out_path, "wb") as f:
            f.write(xbin_data)

        self._log(f"✓ 生成成功: {out_path}")

        # 回显关键 CRC 信息
        data_crc = crc32(bin_data)
        header_crc = crc32(xbin_data[: config["data_offset"] - 4])  # header CRC: 不含 this_crc32 自身
        self._log(f"data_crc32:  0x{data_crc:08X}")
        self._log(f"this_crc32:  0x{header_crc:08X}")


def main():
    root = tk.Tk()
    app = MagicHeaderApp(root)
    root.mainloop()


if __name__ == "__main__":
    main()
