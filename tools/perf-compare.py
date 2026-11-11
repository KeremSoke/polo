#!/usr/bin/env python3
# Copyright 2026 Kerem Soke
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare perf-budgets results with a baseline.

  perf-compare.py BASELINE.json RESULTS.json

Every metric is lower-is-better. One more than 10% above its baseline (and
above it by more than its slack, so noise near zero does not count) is
reported as a regression: a GitHub warning in CI. Exits 0 either way; the
budgets themselves are checked by perf-budgets. To move the baseline, copy
a CI run's perf-results.json over tests/perf/baseline.json.
"""

import json
import os
import sys

SLACK = {
    "idle_wakeups_per_second": 0.5,
    "idle_cpu_percent": 0.5,
    "rss_30_windows_mb": 4.0,
    "alt_tab_first_ms": 10.0,
    "alt_tab_ms": 10.0,
    "key_to_frame_p50_ms": 2.0,
    "key_to_frame_p95_ms": 2.0,
}


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    with open(sys.argv[1]) as f:
        baseline = json.load(f)
    with open(sys.argv[2]) as f:
        results = json.load(f)
    in_ci = os.environ.get("GITHUB_ACTIONS") == "true"
    regressions = 0
    for name, now in sorted(results.items()):
        was = baseline.get(name)
        if was is None:
            print(f"{name}: {now} (new, no baseline)")
            continue
        worse = now - was
        flagged = now > was * 1.10 and worse > SLACK.get(name, 0.0)
        mark = "REGRESSION" if flagged else "ok"
        print(f"{name}: {now} (baseline {was}) {mark}")
        if flagged:
            regressions += 1
            if in_ci:
                print(f"::warning title=Performance regression::{name} is {now}, "
                      f"baseline {was} (+{100 * worse / was if was else 0:.0f}%)")
    print(f"{regressions} regression(s)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
