"""Regenerate the README figures.

Usage: python3 docs/figures/make_figures.py [output_dir]
Needs matplotlib (Pillow comes with it) and a CJK font (Microsoft YaHei, Noto Sans CJK SC or similar).

- pinmux_{light,dark}.png      示意图：I2C1 换到 CSI 的两个脚前后
- uart_frame_{light,dark}.png  逻辑分析仪抓到的那一帧 0x48，按 8N1、115200 重画
- board_mpu6500.jpg            开发板照片裁剪、缩小（原图在 02 的 assets/）
- evtest_mpu6500.png           evtest 截图的上半部分（原图在 05 的 assets/）
每张图的出处写在对应函数里。
"""
import sys
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib import font_manager
from matplotlib.patches import FancyArrowPatch, FancyBboxPatch
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
OUT = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent
OUT.mkdir(parents=True, exist_ok=True)

for f in ("msyh.ttc", "msyhbd.ttc"):
    for base in ("/mnt/c/Windows/Fonts", "C:/Windows/Fonts"):
        if Path(base, f).exists():
            font_manager.fontManager.addfont(str(Path(base, f)))
plt.rcParams["font.family"] = ["Microsoft YaHei", "Noto Sans CJK SC", "PingFang SC", "DejaVu Sans"]
plt.rcParams["axes.unicode_minus"] = False

THEMES = {
    "light": dict(surface="#ffffff", panel="#f6f8fa", ink="#1f2328", ink2="#59636e", muted="#818b98",
                  line="#d1d9e0", off="#8c959f", off_bg="#f6f8fa",
                  blue="#2a78d6", blue_bg="#ddeafb", mine="#1a7f5a", mine_bg="#d8f3e6",
                  orange="#e0731f"),
    "dark": dict(surface="#0d1117", panel="#161b22", ink="#f0f6fc", ink2="#9198a1", muted="#6e7681",
                 line="#30363d", off="#6e7681", off_bg="#161b22",
                 blue="#4493f8", blue_bg="#0f2744", mine="#3fb98a", mine_bg="#0f2e24",
                 orange="#f0883e"),
}
WIDTH_IN, DPI = 7.6, 180
SMALL = 9


def canvas(height_in, rect, t):
    """一张图加一块画布：横向 0–100，纵向按宽高比换算，方框不会被拉扁。"""
    fig = plt.figure(figsize=(WIDTH_IN, height_in))
    fig.patch.set_facecolor(t["surface"])
    ax = fig.add_axes(rect)
    h = 100 * (height_in * rect[3]) / (WIDTH_IN * rect[2])
    ax.set_xlim(0, 100)
    ax.set_ylim(0, h)
    ax.axis("off")
    return fig, ax, h


def titles(fig, t, title, subtitle=None):
    fig.text(0.02, 0.965, title, fontsize=12, fontweight="bold", color=t["ink"], va="top")
    if subtitle:
        fig.text(0.02, 0.965 - 0.32 / fig.get_figheight(), subtitle, fontsize=SMALL + 0.5,
                 color=t["ink2"], va="top")


def note(fig, t, text):
    fig.text(0.02, 0.025, text, fontsize=SMALL - 0.5, color=t["muted"], va="bottom")


def box(ax, x, y, w, h, text, fc, ec, color, size=10, weight="normal", ls="-"):
    ax.add_patch(FancyBboxPatch((x, y), w, h, boxstyle="round,pad=0,rounding_size=0.8",
                                fc=fc, ec=ec, lw=1.3, ls=ls))
    ax.text(x + w / 2, y + h / 2, text, ha="center", va="center", fontsize=size, color=color,
            weight=weight, linespacing=1.3)


def label(ax, x, y, text, color, size=SMALL, ha="left", va="center", weight="normal"):
    ax.text(x, y, text, ha=ha, va=va, fontsize=size, color=color, weight=weight, linespacing=1.3)


def arrow(ax, x0, y0, x1, y1, color, style="-|>", lw=1.4, ls="-"):
    ax.add_patch(FancyArrowPatch((x0, y0), (x1, y1), arrowstyle=style, mutation_scale=11,
                                 color=color, lw=lw, ls=ls, shrinkA=0, shrinkB=0))


def save(fig, name, t_name, t):
    fig.savefig(OUT / f"{name}_{t_name}.png", dpi=DPI, facecolor=t["surface"])
    plt.close(fig)


# ---------------------------------------------------------------- 1. pin muxing
# 引脚、ALT3、pad 值、uart6 disabled：03-Configuring-Pin-Muxing/code/imx6ull-100ask-custom.dts
# 右栏的扫描结果：03-Configuring-Pin-Muxing/assets/i2cdetect_uu.png（0x68 是 UU，0x1e 没了）
# 左栏：默认 pad 上接的是板载 AP3216C（0x1e），出自当时的实验笔记
def fig_pinmux(t_name, t):
    fig, ax, H = canvas(4.3, [0.02, 0.08, 0.96, 0.73], t)
    titles(fig, t, "控制器还是 I2C1，换的是它接哪两个 pad",
           "新建 pinctrl 组把两个 CSI 脚切到 ALT3，i2c1 的 pinctrl-0 指向它，uart6 关掉免得抢脚")

    def column(x0, mine):
        on, on_bg = (t["mine"], t["mine_bg"]) if mine else (t["blue"], t["blue_bg"])
        off, off_bg = t["off"], t["off_bg"]
        label(ax, x0 + 23.5, H - 1.8, "我的 dtb" if mine else "原厂 dtb", on, size=11, ha="center",
              weight="bold")
        cy, ch = H - 10.5, 5.0
        box(ax, x0 + 4, cy, 20, ch, "I2C1 控制器", t["surface"], t["ink"], t["ink"], weight="bold")
        py, ph, pw = H - 23.5, 6.6, 21.5
        lx, rx = x0 + 0.5, x0 + 25
        if mine:
            box(ax, lx, py, pw, ph, "默认的两个 pad\n不再是 I2C1", off_bg, t["line"], off, size=SMALL)
            box(ax, rx, py, pw, ph, "CSI_PIXCLK → SCL\nCSI_MCLK → SDA", on_bg, on, t["ink"], size=SMALL)
            label(ax, rx + pw / 2, py - 0.8, "ALT3，pad 0x4001b8b1", on, ha="center", va="top")
            arrow(ax, x0 + 18, cy, rx + 6, py + ph, on, lw=1.6)
            label(ax, x0 + 7.5, cy - 4.0, "pinctrl-0 →\nmy_i2c1grp", on, ha="center")
            box(ax, x0 + 28, cy, 18.5, ch, "uart6：disabled", t["surface"], off, t["ink2"], size=SMALL,
                ls="--")
            arrow(ax, x0 + 41, cy, x0 + 41, py + ph, off, style="-", lw=1.1, ls="--")
            label(ax, x0 + 40.2, cy - 3.2, "ALT8 也是\n这两个脚", t["ink2"], ha="right")
        else:
            box(ax, lx, py, pw, ph, "默认的两个 pad\nUART4 那一对", on_bg, on, t["ink"], size=SMALL)
            box(ax, rx, py, pw, ph, "CSI_PIXCLK\nCSI_MCLK", off_bg, t["line"], off, size=SMALL)
            label(ax, rx + pw / 2, py - 0.8, "没接到 I2C1", off, ha="center", va="top")
            arrow(ax, x0 + 10, cy, lx + pw / 2, py + ph, on, lw=1.6)
            label(ax, x0 + 26, cy - 4.0, "pinctrl-0 → 原厂的组", on)
        dy, dh = H - 35.0, 6.0
        for x, text, active in ((lx, "板载 AP3216C\n0x1e", not mine),
                                (rx, "排针上的 MPU6500\n0x68", mine)):
            box(ax, x, dy, pw, dh, text, on_bg if active else t["surface"], on if active else t["line"],
                t["ink"] if active else off, size=SMALL)
            ty = py - (3.4 if x == rx else 0)
            ax.plot([x + pw / 2, x + pw / 2], [dy + dh, ty], color=on if active else t["line"], lw=1.4)
        res = ("i2cdetect -y 0：1e 消失、68 出现\n绑上驱动以后 68 显示 UU" if mine
               else "i2cdetect -y 0：只看得到 1e\nMPU6500 接着线也扫不到")
        label(ax, x0 + 23.5, dy - 4.2, res, on, ha="center", weight="bold")

    column(1, mine=False)
    ax.plot([50, 50], [1, H - 0.5], color=t["line"], lw=1.2)
    column(52.5, mine=True)
    note(fig, t, "示意，两个传感器一直接在各自的脚上。引脚和 pad 值见实验 03 的 imx6ull-100ask-custom.dts，"
                 "右栏的结果见实验 03 的 i2cdetect 截图")
    save(fig, "pinmux", t_name, t)


# ---------------------------------------------------------------- 2. UART frame
# 0x48 是实验 06 用逻辑分析仪（PulseView 解码）抓到的字节：
# 06-Accessing-IO-Memory-and-Ports/assets/dmesg_uart.png（文件名和内容不对应，内容是波形）
def fig_uart_frame(t_name, t):
    fig, ax, H = canvas(2.75, [0.02, 0.1, 0.96, 0.64], t)
    titles(fig, t, "逻辑分析仪抓到的那一帧：0x48（字符 H）",
           "空闲是高，起始位拉低，8 个数据位低位先发，停止位回到高；接收方在每一位的中间采样")
    w, s0, h = 6.6, 20.0, 3.6
    vals = [0] + [(0x48 >> i) & 1 for i in range(8)] + [1]
    names = ["起始"] + [f"b{i}" for i in range(8)] + ["停止"]
    y = H - 11.0
    xs, ys, level = [12], [y + h], 1
    for i, v in enumerate(vals):
        if v != level:
            e = s0 + i * w
            xs += [e, e]
            ys += [y + h * level, y + h * v]
            level = v
    xs.append(s0 + 10 * w + 6)
    ys.append(y + h * level)
    ax.plot(xs, ys, color=t["ink"], lw=1.7, solid_joinstyle="miter")
    label(ax, 1.5, y + h / 2, "TX", t["ink"], size=10)
    for i, (v, n) in enumerate(zip(vals, names)):
        cx = s0 + i * w + w / 2
        ax.plot([s0 + i * w] * 2, [y - 0.8, y + h + 0.8], color=t["line"], lw=0.8, zorder=0)
        label(ax, cx, y - 1.4, n, t["ink2"], ha="center", va="top")
        label(ax, cx, y + h + 1.2, str(v), t["blue"] if 1 <= i <= 8 else t["ink"], ha="center")
        ax.plot(cx, y + h * v, "o", ms=4.5, color=t["orange"], zorder=5)
    ax.plot([s0 + 10 * w] * 2, [y - 0.8, y + h + 0.8], color=t["line"], lw=0.8, zorder=0)
    label(ax, 15.5, y + h + 1.2, "空闲", t["ink2"], ha="center")
    a, b = s0 + w, s0 + 9 * w
    ax.plot([a + 0.3, a + 0.3, b - 0.3, b - 0.3], [y + h + 3.0, y + h + 3.6, y + h + 3.6, y + h + 3.0],
            color=t["blue"], lw=1.0)
    label(ax, (a + b) / 2, y + h + 4.6, "数据 0x48 = 0100 1000，从 b0 开始发", t["blue"], ha="center")
    yd = y - 5.6
    arrow(ax, s0, yd, s0 + 10 * w, yd, t["ink2"], style="<|-|>", lw=1.1)
    label(ax, s0 + 5 * w, yd - 0.9, "一帧 10 位，115200 下约 87 µs", t["ink2"], ha="center", va="top")
    label(ax, 1.5, y - 1.4, "橙点：采样点", t["orange"], va="top")
    note(fig, t, "按 8N1、115200 重画；原始波形和解码见 06-Accessing-IO-Memory-and-Ports/assets/"
                 "dmesg_uart.png（文件名和内容不对应）")
    save(fig, "uart_frame", t_name, t)


# ---------------------------------------------------------------- 3. photos
def photos():
    # 02-Describing-Hardware-Devices/assets/board_photo_2.jpg：开发板和外接的 MPU6500 模块，裁掉桌面再缩小
    im = Image.open(ROOT / "02-Describing-Hardware-Devices/assets/board_photo_2.jpg").convert("RGB")
    im = im.crop((0, 940, 2620, 3280))
    im.thumbnail((900, 900), Image.LANCZOS)
    im.save(OUT / "board_mpu6500.jpg", quality=85, optimize=True)
    # 05-Input-Interface/assets/input_registered.png：evtest 打开设备、列出三个轴的部分；
    # 下面的事件行打印错位（推断是 evtest 按 64 位 time_t 解析），这张图只截上半部分
    im = Image.open(ROOT / "05-Input-Interface/assets/input_registered.png")
    im.crop((0, 5, im.width, 513)).save(OUT / "evtest_mpu6500.png", optimize=True)


if __name__ == "__main__":
    for name, theme in THEMES.items():
        for fn in (fig_pinmux, fig_uart_frame):
            fn(name, theme)
    photos()
    print("wrote", len(list(OUT.glob("*.png")) + list(OUT.glob("*.jpg"))), "files to", OUT)
