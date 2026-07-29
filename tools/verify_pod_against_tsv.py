#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Verify a built JAPANESE.POD against a translation TSV."""

import argparse
import csv
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from podtool import Pod3  # noqa: E402
from textio import apply_gbk_fallbacks, dialect_for, split_msglist_pair  # noqa: E402


def read_sheet(path):
    with open(path, encoding="utf-8-sig", newline="") as f:
        return list(csv.DictReader(f, delimiter=dialect_for(path)))


def read_pod_texts(path):
    pod = Pod3.load(path)
    out = {}
    for e in pod.entries:
        name = e["name"].rsplit("\\", 1)[-1].upper()
        if name.endswith(".TXT"):
            out[name] = pod.data(e).decode("cp936", "replace").split("\r\n")
    return out


def line_text(name, line):
    if name == "MSGLIST.TXT":
        pair = split_msglist_pair(line)
        return pair[1] if pair else None
    parts = line.split(",", 2)
    if len(parts) < 3:
        return None
    return parts[2].strip()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("pod")
    ap.add_argument("tsv")
    ap.add_argument("--basis", choices=["en", "jp"], required=True)
    args = ap.parse_args()

    rows = read_sheet(args.tsv)
    files = read_pod_texts(args.pod)
    bad = []
    checked = 0

    for row_index, row in enumerate(rows, start=2):
        line_value = str(row.get("line", "")).strip()
        if line_value == "+":
            continue
        try:
            line_no = int(line_value)
        except ValueError:
            continue
        name = row.get("file", "").upper()
        if name not in files or line_no >= len(files[name]):
            bad.append((row_index, name, line_value, "<missing line>", ""))
            continue
        expected = (row.get("chinese") or "").strip()
        if not expected:
            expected = row.get("japanese" if args.basis == "jp" else "english", "")
        expected, _used = apply_gbk_fallbacks(expected)
        actual = line_text(name, files[name][line_no])
        checked += 1
        if actual != expected:
            bad.append((row_index, name, line_value, actual, expected))
            if len(bad) >= 20:
                break

    if bad:
        print("VERIFY FAIL: %d mismatch(es), showing first %d" % (len(bad), len(bad)))
        for row_index, name, line_no, actual, expected in bad:
            print("row %s %s:%s" % (row_index, name, line_no))
            print("  actual  :", actual)
            print("  expected:", expected)
        return 1
    print("VERIFY OK: %d row(s)" % checked)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
