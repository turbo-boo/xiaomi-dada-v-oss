#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""Compare built MCA exports, aliases and wire calls with Dada stock evidence.

This checks names and observed wire arguments. It does not establish symbol
version CRC, structure layout, kernel KMI or runtime compatibility.
"""
import argparse
import json
from pathlib import Path
import re
import subprocess


def readelf(path, *args):
    return subprocess.check_output(["readelf", *args, str(path)], text=True)


def wire_calls(path, objdump):
    """Record constant x0/x2 arguments at AArch64 direct-call relocations.

    Unknown register values stay unknown. Calls invalidate caller-saved
    registers; this avoids attributing a previous call's constants to a
    dynamic VDM or PDO operation.
    """
    text = subprocess.check_output([objdump, "-dr", str(path)], text=True)
    functions = {}
    calls = []
    values = {}
    for line in text.splitlines():
        match = re.match(r"^[0-9a-f]+ <([^>]+)>:", line)
        if match:
            calls = functions.setdefault(match[1], [])
            values = {}
            continue
        match = re.search(r"R_AARCH64_(?:CALL26|JUMP26)\s+(\S+)", line)
        if match:
            calls.append({"target": match[1], "property": values.get("0"),
                          "size": values.get("2")})
            values = {}
            continue
        match = re.search(r"\bmov\s+w([02]), #(?:0x([0-9a-f]+)|(-?\d+))", line)
        if match:
            values[match[1]] = int(match[2], 16) if match[2] else int(match[3])
            continue
        match = re.search(r"\bmovk\s+w([02]), #0x([0-9a-f]+)(?:, lsl #(\d+))?", line)
        if match:
            shift = int(match[3] or 0)
            if match[1] in values:
                values[match[1]] = ((values[match[1]] & ~(65535 << shift)) |
                                    (int(match[2], 16) << shift))
            continue
        # Any other instruction writing x0/w0 or x2/w2 loses its constant.
        match = re.search(r"\t([a-z][a-z0-9.]*)\s+[xw]([02]),", line)
        if match and match[1] not in {"str", "stur", "stp", "cmp", "cmn", "tst"}:
            values.pop(match[2], None)

    def expand(name, visited):
        if name in visited:
            return []
        visited = visited | {name}
        found = []
        for call in functions.get(name, []):
            target = call["target"]
            for operation in ("read", "write"):
                if target in {f"mca_adsp_glink_{operation}_prop", f"adsp_{operation}"}:
                    found.append({"op": operation, "property": call["property"],
                                  "size": call["size"]})
                    break
            else:
                found.extend(expand(target, visited))
        return found

    return {name: expand(name, set()) for name in functions}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--kernel-out", required=True, type=Path)
    parser.add_argument("--objdump", default="llvm-objdump")
    args = parser.parse_args()
    fixture = json.loads((Path(__file__).parent / "dada_stock_contract.json").read_text())
    built = {p.name: p for p in
             (args.kernel_out / "drivers/power/supply/mca").rglob("*.ko")}
    exports_count = 0
    for row in fixture["modules"]:
        assert row["name"] in built, f"Missing stock module name: {row['name']}"
        path = built[row["name"]]
        symbols = readelf(path, "-Ws")
        exports = set(re.findall(r"\b__ksymtab_(\w+)\s*$", symbols, re.M))
        missing = set(row["exports"]) - exports
        assert not missing, f"{row['name']}: missing stock exports {sorted(missing)}"
        aliases = set(re.findall(r"alias=([^\n]+)", readelf(path, "-p", ".modinfo")))
        missing = set(row["aliases"]) - aliases
        assert not missing, f"{row['name']}: missing stock aliases {sorted(missing)}"
        exports_count += len(row["exports"])
    disassembly = {}
    for row in fixture["wire"]:
        module = row["module"]
        if module not in disassembly:
            disassembly[module] = wire_calls(built[module], args.objdump)
        calls = disassembly[module].get(row["built_function"], [])
        expected = {key: row[key] for key in ("op", "property", "size")}
        assert any(all(expected[k] is None or call[k] == expected[k]
                       for k in expected) for call in calls), (
            f"{module}:{row['built_function']}: expected {expected}, got {calls}")
    print(f"Verified {len(fixture['modules'])} stock module names, "
          f"{exports_count} exports, aliases and {len(fixture['wire'])} wire contracts")


if __name__ == "__main__":
    main()
