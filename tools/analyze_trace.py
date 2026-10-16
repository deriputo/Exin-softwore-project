"""Offline analysis of pc_monitor CSV; standard library only, no serial access."""
from __future__ import annotations

import argparse
import csv
import html
import json
import math
from pathlib import Path

from pc_monitor import TRACE_COLUMNS, parse_trace_line


def read_csv(path):
    samples = []
    with Path(path).open(encoding="utf-8-sig", newline="") as stream:
        reader = csv.DictReader(stream)
        if not set(TRACE_COLUMNS).issubset(reader.fieldnames or []):
            raise ValueError("CSV缺少TRACE字段，请使用上位机的CSV记录文件")
        for line, row in enumerate(reader, 2):
            if None in row:
                raise ValueError(f"第{line}行字段数量错误")
            sample = parse_trace_line("H," + ",".join(row.get(key) or "" for key in TRACE_COLUMNS))
            if sample is None:
                raise ValueError(f"第{line}行不是有效TRACE数据")
            samples.append(sample)
    if len(samples) < 2:
        raise ValueError("至少需要两条有效样本")
    return samples


def timeline(samples):
    times = [0.0]
    elapsed_ms = 0
    for previous, current in zip(samples, samples[1:]):
        delta = (current.ms - previous.ms) & 0xffffffff
        # Support counter wrap; reject restart, duplicates and ambiguous long gaps.
        if not 0 < delta < 0x80000000:
            raise ValueError("板端时间重复、复位或倒退；请分开记录不同运行")
        elapsed_ms += delta
        times.append(elapsed_ms / 1000)
    return times


def analyze(samples, band=1.0, dwell=1.0, gap=0.15, start=0.0, end=None):
    if not all(math.isfinite(x) and x > 0 for x in (band, dwell, gap)):
        raise ValueError("误差带、保持时间及间断门限必须为正有限数")
    times = timeline(samples)
    end = times[-1] if end is None else end
    if not math.isfinite(start) or not math.isfinite(end) or not 0 <= start < end <= times[-1]:
        raise ValueError("分析区间须位于记录范围内，且起点小于终点")
    selected = [(t, s) for t, s in zip(times, samples) if start <= t <= end]
    if len(selected) < 2:
        raise ValueError("选定区间样本不足")
    targets = [s.target for _, s in selected]
    if max(targets) - min(targets) > 0.200001:
        raise ValueError("区间目标角变化超过0.2°，请用--start-s/--end-s选择一次固定目标测试")
    faults = sum(s.fault not in ("NONE", "OFF") for _, s in selected)
    gaps = sum(b[0] - a[0] > gap + 1e-9 for a, b in zip(selected, selected[1:]))
    gaps += int(selected[0][0] - start > gap + 1e-9)
    gaps += int(end - selected[-1][0] > gap + 1e-9)
    errors = [s.target - s.measured for _, s in selected]
    # Never use transmitted error: it may have different control semantics.
    suffix = None
    for t, s in selected:
        if s.fault not in ("NONE", "OFF") or abs(s.target - s.measured) > band:
            suffix = None
        elif suffix is None:
            suffix = t
    complete = faults == 0 and gaps == 0
    settled = suffix is not None and selected[-1][0] - suffix >= dwell - 1e-9
    valid_suffix = [(t, s) for t, s in selected if suffix is not None and t >= suffix]
    stable_errors = [s.target - s.measured for _, s in valid_suffix]
    stable_angles = [s.measured for _, s in valid_suffix]
    metrics = {
        "start_s": start, "end_s": end, "first_sample_s": selected[0][0],
        "last_sample_s": selected[-1][0], "samples": len(selected),
        "band_deg": band, "dwell_s": dwell, "gap_threshold_s": gap,
        "fault_samples": faults, "gap_count": gaps,
        "saturated_samples": sum(s.saturated for _, s in selected),
        "performance_valid": complete,
        "settled_in_record": bool(complete and settled),
        "recovery_s": suffix - start if complete and settled else None,
        "max_abs_error_deg": max(abs(e) for e in errors),
        "stable_mean_error_deg": sum(stable_errors) / len(stable_errors) if complete and settled else None,
        "stable_peak_to_peak_deg": max(stable_angles) - min(stable_angles) if complete and settled else None,
    }
    return metrics, selected


def chart(selected, gap, band):
    width, height = 1000, 430
    t0, t1 = selected[0][0], selected[-1][0]
    def x(t):
        return 70 + (t - t0) / (t1 - t0) * 900
    parts = [f'<svg viewBox="0 0 {width} {height}" role="img" aria-label="姿态与误差曲线">']
    for top, bottom, series in ((35, 210, ("target", "measured")), (265, 400, ("error",))):
        values = [s.target - s.measured if key == "error" else getattr(s, key)
                  for _, s in selected for key in series]
        if series == ("error",):
            values += [-band, band]
        low, high = min(values) - 1, max(values) + 1
        def y(value):
            return bottom - (value - low) / (high - low) * (bottom - top)
        parts.append(f'<path d="M70 {top}V{bottom}H970" fill="none" stroke="#999"/>')
        parts.append(f'<text x="4" y="{top + 12}">{high:.1f}°</text><text x="4" y="{bottom}">{low:.1f}°</text>')
        if series == ("error",):
            parts.append(f'<rect x="70" y="{y(band):.2f}" width="900" height="{y(-band)-y(band):.2f}" fill="#e2f4e6"/>')
        for index, (t, s) in enumerate(selected):
            if s.saturated:
                parts.append(f'<circle cx="{x(t):.2f}" cy="{top}" r="2.5" fill="#8e44ad"/>')
            if s.fault not in ("NONE", "OFF"):
                parts.append(f'<path d="M{x(t):.2f} {top}V{bottom}" stroke="#e66" opacity="0.3"/>')
            if index and t - selected[index-1][0] > gap + 1e-9:
                parts.append(f'<rect x="{x(selected[index-1][0]):.2f}" y="{top}" width="{x(t)-x(selected[index-1][0]):.2f}" height="{bottom-top}" fill="#ddd"/>')
        for key, color in zip(series, ("#2166ac", "#d66000")):
            run = []
            for index, (t, s) in enumerate(selected):
                if index and t - selected[index-1][0] > gap + 1e-9:
                    parts.append(f'<polyline points="{" ".join(run)}" fill="none" stroke="{color}"/>')
                    run = []
                value = s.target - s.measured if key == "error" else getattr(s, key)
                run.append(f"{x(t):.2f},{y(value):.2f}")
            parts.append(f'<polyline points="{" ".join(run)}" fill="none" stroke="{color}" stroke-width="1.5"/>')
    parts.append(f'<text x="70" y="428">{t0:.3f}s</text><text x="880" y="428">{t1:.3f}s</text></svg>')
    return "".join(parts)


def report(metrics, selected, source):
    labels = {
        "recovery_s": "恢复时间（秒）", "stable_mean_error_deg": "稳定段平均有符号误差（度）",
        "stable_peak_to_peak_deg": "稳定段实测角峰峰值（度）", "max_abs_error_deg": "全区间最大绝对误差（度）",
        "fault_samples": "故障样本数", "gap_count": "数据间断数", "saturated_samples": "输出饱和样本数",
    }
    rows = "".join(f"<tr><th>{label}</th><td>{'不可用' if metrics[key] is None else format(metrics[key], '.4g')}</td></tr>" for key, label in labels.items())
    status = "通过所选区间的连续性检查" if metrics["performance_valid"] else "存在故障或间断：恢复及稳定指标作废"
    return ('<!doctype html><html lang="zh-CN"><meta charset="utf-8"><title>姿态性能分析</title>'
            '<style>body{font:16px sans-serif;max-width:1100px;margin:30px auto;padding:0 20px}svg{width:100%}th,td{padding:8px;text-align:left;border-bottom:1px solid #ddd}</style>'
            f'<h1>姿态性能分析</h1><p>来源：{html.escape(str(source))}</p><p>{status}</p>'
            f'<p>区间 {metrics["start_s"]:.3f}～{metrics["end_s"]:.3f}s；误差带 ±{metrics["band_deg"]}°，保持 {metrics["dwell_s"]}s。</p>'
            '<p>上图蓝线为目标、橙线为实测；下图蓝线为误差，绿色为误差带。红色标记故障，灰色区域为数据间断，紫点为输出饱和。</p>'
            + chart(selected, metrics["gap_threshold_s"], metrics["band_deg"]) + f'<table>{rows}</table>'
            '<p>恢复起点由所选区间起点定义，须与目标确认或扰动释放时刻对应。恢复终点为进入误差带且直到记录结束均未离开的首个采样时刻，后续至少保持指定时长；未满足则不可用。'
            '时间以板端计数计算，20Hz数据的时间分辨率约50ms；不推算样本间行为。饱和单独标记，不自动作废。'
            '均值为样本平均；曲线及指标来自IMU，并非外部角度精度或考核认证。CSV不包含HOLD开关，须自行确认测试全程开启HOLD。</p></html>')


def main():
    parser = argparse.ArgumentParser(description="离线TRACE CSV性能分析，不连接串口")
    parser.add_argument("csv", type=Path)
    parser.add_argument("--output", type=Path, help="输出HTML路径，JSON保存在同名文件")
    parser.add_argument("--band-deg", type=float, default=1.0)
    parser.add_argument("--dwell-s", type=float, default=1.0)
    parser.add_argument("--gap-s", type=float, default=0.15)
    parser.add_argument("--start-s", type=float, default=0.0)
    parser.add_argument("--end-s", type=float)
    args = parser.parse_args()
    try:
        metrics, selected = analyze(read_csv(args.csv), args.band_deg, args.dwell_s,
                                    args.gap_s, args.start_s, args.end_s)
        output = args.output or args.csv.with_name(args.csv.stem + "_analysis.html")
        json_path = output.with_suffix(".json")
        if output.suffix.lower() != ".html" or args.csv.resolve() in (output.resolve(), json_path.resolve()):
            raise ValueError("输出必须为.html，且不能覆盖输入CSV")
        if output.exists() or json_path.exists():
            raise ValueError("输出已存在，请换一个--output文件名")
        output.write_text(report(metrics, selected, args.csv), encoding="utf-8")
        json_path.write_text(json.dumps(metrics, ensure_ascii=False, indent=2, allow_nan=False), encoding="utf-8")
    except (OSError, ValueError) as error:
        parser.exit(2, f"分析失败：{error}\n")
    print(f"HTML: {output}\nJSON: {json_path}")


if __name__ == "__main__":
    main()
