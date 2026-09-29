"""Score the native benchmark with Levenshtein CER; no reversal or glyph substitutions."""
import argparse
import json
import statistics
import unicodedata
from pathlib import Path

def normalize(text, spaces=True):
    text = unicodedata.normalize("NFC", text)
    return " ".join(text.split()) if spaces else "".join(text.split())

def distance(a, b):
    row = list(range(len(b) + 1))
    for i, ca in enumerate(a, 1):
        nxt = [i]
        for j, cb in enumerate(b, 1):
            nxt.append(min(nxt[-1] + 1, row[j] + 1, row[j-1] + (ca != cb)))
        row = nxt
    return row[-1]

def metrics(rows):
    characters = sum(len(normalize(r["truth"])) for r in rows)
    errors = sum(distance(normalize(r["truth"]), normalize(r["actual"])) for r in rows)
    no_space_chars = sum(len(normalize(r["truth"], False)) for r in rows)
    no_space_errors = sum(distance(normalize(r["truth"], False), normalize(r["actual"], False)) for r in rows)
    warm = sorted(r["ocrMs"] for r in rows if not r["coldModelLoad"])
    return {"images": len(rows), "characters": characters, "edits": errors,
            "cerPercent": round(100 * errors / max(1, characters), 3),
            "cerWithoutSpacesPercent": round(100 * no_space_errors / max(1, no_space_chars), 3),
            "exact": sum(normalize(r["truth"]) == normalize(r["actual"]) for r in rows),
            "warmMedianMs": round(statistics.median(warm), 2) if warm else None,
            "warmP95Ms": round(warm[min(len(warm)-1, int(len(warm)*.95))], 2) if warm else None,
            "coldReadsMs": [round(r["ocrMs"], 2) for r in rows if r["coldModelLoad"]]}

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("reports", nargs="+", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    summary = {"metric": "NFC Unicode codepoint Levenshtein CER; whitespace collapsed; lower is better",
               "limitations": "28 synthetic sentences only; not an independent real-game evaluation. Windows CPU only."}
    for path in args.reports:
        data = json.loads(path.read_text(encoding="utf-8"))
        rows = data["rows"]
        summary[data["mode"]] = {"total": metrics(rows), "peakWorkingSetMiB": round(data["peakWorkingSetBytes"]/1024**2, 2),
            "totalMs": data["totalMs"],
            "languages": {lang: metrics([r for r in rows if r["lang"] == lang]) for lang in dict.fromkeys(r["lang"] for r in rows)}}
    args.output.write_text(json.dumps(summary, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    for mode, data in summary.items():
        if isinstance(data, dict): print(mode, json.dumps(data["total"]), "peak MiB", data["peakWorkingSetMiB"])

if __name__ == "__main__":
    main()
