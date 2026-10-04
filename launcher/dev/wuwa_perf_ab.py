"""What each WuWa fix costs: alternating off/on windows, frame times and game GPU use.

Stand still with the camera still in a busy, shadowed spot (no pose sweep). Each fix is
switched off and on by the test control in alternating order (off,on / on,off ...) so drift
cancels. Frame times come from the backend (once per game frame); GPU use is Windows' per-process
3D engine counter for the game. Every switch is temporary and restored at the end.

    python wuwa_perf_ab.py [--rounds 3] [--window 8] [--fixes shadow,lod,steady,threadlag]
"""
import argparse
import csv
import datetime
import importlib.util
import io
import json
import statistics
import subprocess
import threading
import time
from pathlib import Path

spec = importlib.util.spec_from_file_location("wuwa_live", Path(__file__).with_name("wuwa-test.py"))
live = importlib.util.module_from_spec(spec)
spec.loader.exec_module(live)
SETTLE = 2.0


class GpuSampler(threading.Thread):
    """Mean 3D-engine utilisation (%) of one process over a window, via typeperf."""

    def __init__(self, pid: int, seconds: int):
        super().__init__(daemon=True)
        self.pid, self.seconds, self.value = pid, seconds, None

    def run(self):
        done = subprocess.run(["typeperf", r"\GPU Engine(*engtype_3D)\Utilization Percentage",
                               "-si", "1", "-sc", str(self.seconds + 1)], capture_output=True, text=True)
        rows = [r for r in csv.reader(io.StringIO(done.stdout)) if r]
        if len(rows) < 3:
            return
        columns = [i for i, name in enumerate(rows[0]) if f"(pid_{self.pid}_" in name]
        if not columns:
            return
        totals = []
        for row in rows[2:]:   # the first sample of a rate counter is not meaningful
            try:
                totals.append(sum(float(row[i]) for i in columns if row[i].strip()))
            except (ValueError, IndexError):
                continue
        self.value = statistics.mean(totals) if totals else None


def fixes(test):
    """name -> (switch(on, seconds), restore()). on=True means the fix is applied."""
    def shadow(on, seconds):
        test.request("shadow_pass", seconds=0)   # a new window is refused while one is open
        test.request("shadow_pass", seconds=seconds, enabled=on)

    def lod(on, seconds):
        test.request("lod_sync", seconds=seconds, enabled=on)

    def steady(on, seconds):
        test.request("steady_view", bypass_seconds=0 if on else seconds)

    def threadlag(on, seconds):
        test.request("console_set", name="r.OneFrameThreadLag", seconds=0)
        if not on:   # the fix is OneFrameThreadLag=0; off means the engine default 1
            test.request("console_set", name="r.OneFrameThreadLag", value=1, seconds=max(5, seconds))

    return {
        "shadow": ("Shadow fix (Same Pass)", shadow, lambda: test.request("shadow_pass", seconds=0)),
        "lod": ("Far LOD sync", lod, lambda: test.request("lod_sync", seconds=0)),
        "steady": ("Steady desktop view", steady, lambda: test.request("steady_view", bypass_seconds=0)),
        "threadlag": ("r.OneFrameThreadLag=0", threadlag,
                      lambda: test.request("console_set", name="r.OneFrameThreadLag", seconds=0)),
    }


def measure(test, window: int) -> dict:
    sampler = GpuSampler(test.pid, window)
    test.request("perf", action="start")
    sampler.start()
    time.sleep(window)
    perf = test.request("perf", action="stop")["perf"]
    sampler.join(timeout=window + 10)
    perf["gpu_percent"] = sampler.value
    return perf


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--rounds", type=int, default=3)
    parser.add_argument("--window", type=int, default=8, help="seconds measured per condition")
    parser.add_argument("--fixes", default="shadow,lod,steady,threadlag")
    parser.add_argument("--refill", action="store_true", help="also time one far-lighting refill hitch")
    args = parser.parse_args()
    test = live.LiveTest()
    table = fixes(test)
    results = {"started": datetime.datetime.now().isoformat(timespec="seconds"), "fixes": {}}
    for key in args.fixes.split(","):
        label, switch, restore = table[key]
        runs = {"off": [], "on": []}
        try:
            for round_index in range(args.rounds):
                order = ("off", "on") if round_index % 2 == 0 else ("on", "off")
                for condition in order:
                    switch(condition == "on", int(args.window + SETTLE + 3))
                    time.sleep(SETTLE)
                    runs[condition].append(measure(test, args.window))
        finally:
            restore()
        summary = {}
        for condition, items in runs.items():
            gpu = [r["gpu_percent"] for r in items if r["gpu_percent"] is not None]
            summary[condition] = {
                "mean_ms": statistics.mean(r["mean_ms"] for r in items),
                "p99_ms": statistics.mean(r["p99_ms"] for r in items),
                "spread_ms": max(r["mean_ms"] for r in items) - min(r["mean_ms"] for r in items),
                "gpu_percent": statistics.mean(gpu) if gpu else None,
                "frames": sum(r["frames"] for r in items),
            }
        off, on = summary["off"], summary["on"]
        gpu_ms = {c: (s["gpu_percent"] / 100 * s["mean_ms"]) if s["gpu_percent"] is not None else None
                  for c, s in summary.items()}
        summary.update(label=label, rounds=runs, gpu_ms=gpu_ms, delta_ms=on["mean_ms"] - off["mean_ms"])
        results["fixes"][key] = summary
        gpu_text = (f"GPU {off['gpu_percent']:.0f}% -> {on['gpu_percent']:.0f}% "
                    f"(~{gpu_ms['off']:.2f} -> {gpu_ms['on']:.2f} ms)") if gpu_ms["off"] is not None else "GPU n/a"
        print(f"{label:24s} off {off['mean_ms']:6.2f} ms  on {on['mean_ms']:6.2f} ms  "
              f"({on['mean_ms'] - off['mean_ms']:+.2f} ms, p99 {off['p99_ms']:.1f} -> {on['p99_ms']:.1f}; "
              f"round spread {max(off['spread_ms'], on['spread_ms']):.2f} ms)  {gpu_text}", flush=True)
    if args.refill:
        test.request("perf", action="start")
        time.sleep(2)
        test.request("clv_refresh", frames=1)
        time.sleep(4)
        perf = test.request("perf", action="stop")["perf"]
        results["refill"] = perf
        print(f"Far-lighting refill: worst frame {perf['max_ms']:.0f} ms (typical {perf['p50_ms']:.1f} ms)")
    out = Path.cwd() / f"perf-ab-{datetime.datetime.now():%Y%m%d-%H%M}.json"
    out.write_text(json.dumps(results, indent=1), encoding="utf-8")
    print(f"Saved {out}. A difference smaller than the round spread is noise.")


if __name__ == "__main__":
    main()
