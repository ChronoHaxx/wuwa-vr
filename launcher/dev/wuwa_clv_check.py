"""One-shot far-lighting check: CLV refill status from the test control plus the eye-aligned gap of a fresh capture.

    python wuwa_clv_check.py <capture-dir>
"""
import importlib.util, json, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from wuwa_light_batch import gap

spec = importlib.util.spec_from_file_location("t", Path(__file__).with_name("wuwa-test.py"))
t = importlib.util.module_from_spec(spec); argv = sys.argv; sys.argv = ["x"]; spec.loader.exec_module(t); sys.argv = argv
out = Path(argv[1])
c = t.LiveTest()
with c.exclusive():
    clv = c.request("clv_refresh", frames=0)["clv"]
    c.capture(out, "projection")
left, right = gap(out / "image.png", (0.15, 0.35))
print(json.dumps({"pid": c.pid, "clv": clv, "left": round(left, 1), "right": round(right, 1), "gap": round(right - left, 1)}))
