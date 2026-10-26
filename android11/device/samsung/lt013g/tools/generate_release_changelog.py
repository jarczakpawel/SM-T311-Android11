#!/usr/bin/env python3
"""Generate an internally reviewable SM-T311 change ledger, not a public release."""
import argparse
import csv
import hashlib
import json
from collections import Counter
from pathlib import Path


def safe(value):
    return str(value or "").replace("|", "/").replace("\r", " ").replace("\n", " ").strip()


def run(matrix, manifest, target):
    with matrix.open(encoding="utf8", newline="") as handle:
        rows = list(csv.DictReader(handle, delimiter="\t"))
    if not rows or len({r["ID"] for r in rows}) != len(rows):
        raise ValueError("Empty or duplicate release matrix")
    repos = json.loads(manifest.read_text())
    lines = [
        "# Samsung SM-T311 - historia modyfikacji do przyszlego GitHuba",
        "",
        "SZKIC PRYWATNY: kazdy wpis jest stanem historycznym, nie dowodem fizycznego wdrozenia.",
        "Stan obecny ustala CURRENT_RELEASE.json, odczyt SHA urzadzenia i najnowszy handoff.",
        "Przed publikacja sprawdzic tajne dane, vendor blob licensing, stan SELinux i realne obrazy.",
        "",
        "Liczba zapisanych zmian: %d; repozytoria: %d." % (len(rows), len(repos)),
        "",
        "## Repozytoria",
        "",
        "| Repozytorium | Git HEAD | Galaz | Zachowane WIP |",
        "|---|---|---|---|",
    ]
    for name, info in sorted(repos.items()):
        lines.append("| %s | %s | %s | %s |" % (
            safe(name), safe(info.get("active_commit", "")[:12]),
            safe(info.get("active_branch")), len(info.get("wip_preserved_branches", []))))
    lines.extend(["", "## Wszystkie pozycje ze sprawdzalnym statusem", "",
                  "| ID | Priorytet | Podsystem | Historyczny stan |",
                  "|---|---|---|---|"])
    for row in rows:
        lines.append("| %s | %s | %s | %s |" % tuple(
            safe(row[k]) for k in ("ID", "Priorytet", "Podsystem", "Stan")))
    lines.extend(["", "## Material dowodowy i zaleglosci", ""])
    for row in rows:
        lines.extend(["### %s: %s" % (safe(row["ID"]), safe(row["Podsystem"])), "",
            "Priorytet: %s; historyczny status: %s" % (safe(row["Priorytet"]), safe(row["Stan"])),
            "Dowod/staging: %s" % safe(row["Dowod/staging"]),
            "Ryzyko: %s" % safe(row["Ryzyko/warunek"]),
            "Nastepny krok: %s" % safe(row["Nastepny krok"]), ""])
    lines.extend([
        "## Wymagania przed publicznym wydaniem", "",
        "- Wydrukowac liste faktycznie zastosowanych commitow z finalnego obrazu ROM, bez eksperymentalnych WIP.",
        "- Sprawdzic SHA calego BOOT, SYSTEM, vendor oraz przetestowane dzialanie.",
        "- Usunac prywatne SIM PIN, sekrety, identyfikatory, klucze i dane kont.",
        "- Sprawdzic prawa dystrybucji wlasnosciowych bibliotek i firmware.",
        "- Potwierdzic SELinux Enforcing i finalne testy sprzetu; nie publikowac tej wersji roboczej jako finalnej.",
        "",
    ])
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text("\n".join(lines), encoding="utf8")
    return {"rows": len(rows), "repos": len(repos), "bytes": target.stat().st_size,
            "sha256": hashlib.sha256(target.read_bytes()).hexdigest(),
            "priorities": dict(Counter(r["Priorytet"] for r in rows))}


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--matrix", type=Path, required=True)
    parser.add_argument("--git-manifest", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(run(args.matrix, args.git_manifest, args.out), indent=2))
