"""Plot actual configured nonlinear integral gain and nominal 20ms increments."""
import csv
import os
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
os.environ.setdefault("MPLCONFIGDIR", str(ROOT / "tmp/matplotlib"))
sys.path.insert(0, str(ROOT / "tmp/plotdeps"))
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt


def value(name):
    text = (ROOT / "Application/app_config.h").read_text(encoding="utf-8")
    match = re.search(r"^#define\s+" + name + r"\s+([0-9.]+)f", text, re.M)
    if not match:
        raise ValueError("Missing numeric configuration: " + name)
    return float(match[1])


def main():
    ki = value("APP_HOLD_NORMAL_KI")
    minimum = value("APP_HOLD_INTEGRAL_MIN_KI")
    scale = value("APP_HOLD_INTEGRAL_ERROR_SCALE_DEG")
    deadband = value("APP_HOLD_DEADBAND_DEG")
    errors = [i / 100 for i in range(3001)]
    gains = [minimum + (ki - minimum) * e*e / (e*e + scale*scale) for e in errors]
    old_gains = [0.4 + 0.6 * e*e / (e*e + 16) for e in errors]
    increments = [g * e * 0.02 if e > deadband else 0 for e, g in zip(errors, gains)]
    old = [g * e * 0.02 if e > deadband else 0 for e, g in zip(errors, old_gains)]
    plt.rcParams.update({"font.family": "Microsoft YaHei", "axes.unicode_minus": False,
                         "font.size": 11, "svg.fonttype": "path"})
    fig, axes = plt.subplots(2, 1, figsize=(10, 8), layout="constrained")
    axes[0].plot(errors, gains, color="#1764ab", lw=2.5, label="新版有效 Ki")
    axes[0].plot(errors, old_gains, color="#a96515", ls="--", label="v0.7.0：积分下限 0.4、上限 1.0")
    axes[0].axhline(minimum, color="#999", ls=":", lw=1)
    axes[0].set(ylabel="有效积分增益 Ki（1/s）", ylim=(0, ki * 1.12))
    axes[1].plot(errors, increments, color="#1764ab", lw=2.5, label="新版积分增量绝对值")
    axes[1].plot(errors, old, color="#a96515", ls="--", label="原版积分增量绝对值")
    axes[1].set(ylabel="每 20 ms 积分贡献增量（°）", ylim=(0, max(increments + old) * 1.12))
    example_error = 20
    example_gain = minimum + (ki-minimum)*example_error**2/(example_error**2+scale**2)
    axes[1].annotate(f"误差20°：约{example_gain*20*0.02:.3f}°/周期",
                     xy=(20, example_gain*20*0.02), xytext=(4, 0.45),
                     arrowprops={"arrowstyle": "->", "color": "#444"})
    for ax in axes:
        ax.axvspan(0, deadband, color="#dddddd", alpha=0.8)
        ax.axvline(scale, color="#999", ls=":", lw=1)
        ax.set(xlim=(0, 30), xlabel="姿态误差绝对值 |e|（°）")
        ax.grid(alpha=0.2)
        ax.legend(loc="upper right")
    fig.suptitle(f"有界变速积分：Ki有效 = {minimum:g} + {ki-minimum:g} × e² / (e² + {scale:g}²)", fontsize=17)
    fig.supxlabel(f"灰区：|e| ≤ {deadband:g}° 时停止累积，保留已有积分；曲线为无饱和时的候选增量。\n"
                  "正负误差增量方向相反；遇输出/限速饱和或积分限幅时，实际增量可能更小或为零。", fontsize=10)
    target = ROOT / "docs/nonlinear_integral_curve"
    fig.savefig(target.with_suffix(".png"), dpi=170)
    fig.savefig(target.with_suffix(".svg"))
    with target.with_suffix(".csv").open("w", encoding="utf-8-sig", newline="") as stream:
        writer = csv.writer(stream)
        writer.writerow(("abs_error_deg", "effective_ki", "new_candidate_increment_deg_20ms", "old_increment_deg_20ms"))
        writer.writerows(zip(errors, gains, increments, old))
    print(target.with_suffix(".png"))


if __name__ == "__main__":
    main()
