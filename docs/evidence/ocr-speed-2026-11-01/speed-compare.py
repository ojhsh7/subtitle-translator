"""Before/after table for the OCR speed plan: CER, identical outputs, warm and first-read times."""
import json, sys, statistics, unicodedata
from pathlib import Path

def norm(t):
    return " ".join(unicodedata.normalize("NFC", t).split())

def dist(a, b):
    row = list(range(len(b) + 1))
    for i, ca in enumerate(a, 1):
        n = [i]
        for j, cb in enumerate(b, 1):
            n.append(min(n[-1] + 1, row[j] + 1, row[j - 1] + (ca != cb)))
        row = n
    return row[-1]

def q(v, p):
    v = sorted(v)
    return v[min(len(v) - 1, int(len(v) * p))]

def stats(path):
    d = json.loads(Path(path).read_text(encoding="utf-8"))
    rows = d["rows"]
    warm = [r["ocrMs"] for r in rows if not r["coldModelLoad"]]
    cold = [r["ocrMs"] for r in rows if r["coldModelLoad"]]
    chars = sum(len(norm(r["truth"])) for r in rows)
    edits = sum(dist(norm(r["truth"]), norm(r["actual"])) for r in rows)
    return {"rows": rows, "cer": round(100 * edits / chars, 3), "exact": sum(norm(r["truth"]) == norm(r["actual"]) for r in rows),
            "warmMedian": round(q(warm, .5), 1), "warmP95": round(q(warm, .95), 1),
            "firstReadMedian": round(statistics.median(cold), 1), "firstReadMax": round(max(cold), 1),
            "threads": d.get("threads", 1), "warmUp": d.get("warmUp", False)}

out = {}
folder = Path(sys.argv[1])
for name, before, after in [("dialogue, language selected", "base-scene.json", "final-scene.json"),
                            ("one line, language selected", "base-ocr.json", "final-ocr.json"),
                            ("dialogue, default kor+jpn+eng", "base-scene-legacy.json", "final-scene-legacy.json"),
                            ("one line, default kor+jpn+eng", "base-ocr-legacy.json", "final-ocr-legacy.json"),
                            ("dialogue, no warm-up", "base-scene.json", "final-scene-nowarm.json"),
                            ("one line, no warm-up", "base-ocr.json", "final-ocr-nowarm.json")]:
    a, b = stats(folder / before), stats(folder / after)
    changed = sum(x["actual"] != y["actual"] for x, y in zip(a["rows"], b["rows"]))
    for s in (a, b):
        del s["rows"]
    out[name] = {"before": a, "after": b, "changedOutputs": changed}
    print(f"{name:32s} CER {a['cer']}->{b['cer']} exact {a['exact']}->{b['exact']} changed {changed} | "
          f"warm med {a['warmMedian']}->{b['warmMedian']} p95 {a['warmP95']}->{b['warmP95']} | "
          f"first read med {a['firstReadMedian']}->{b['firstReadMedian']} max {a['firstReadMax']}->{b['firstReadMax']}")
(folder / "speed-comparison.json").write_text(json.dumps(out, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
