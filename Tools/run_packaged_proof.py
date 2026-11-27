"""Run the packaged (Shipping) game headless-ish and verify its arena result proof.

The archived package is a staged build, so the game writes its results into the
user's app data instead of the package folder:

    %LOCALAPPDATA%\\WiesbadenSurvivors2\\Saved\\WS2\\arena_<arena>.json

Usage:

    python Tools/run_packaged_proof.py --arena kurpark
    python Tools/run_packaged_proof.py --arena innenstadt --timeout 300

The shipping build has no NullRHI, so the run opens a small window; everything
else is automated (-ws2full/-ws2fast/-ws2auto/-ws2quit/-ws2noboss/-ws2invuln).
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_PACKAGE = ROOT / "Dist" / "Release-0.2.0-LevelUpProof" / "Windows" / "WiesbadenSurvivors2.exe"
EXPECTED_POOL = {"weapons": 40, "tiers": 160, "items": 44, "stats": 24, "elements": 5}


def candidate_paths(arena):
    name = "arena_%s.json" % arena
    paths = []
    local = os.environ.get("LOCALAPPDATA", "")
    if local:
        paths.append(Path(local) / "WiesbadenSurvivors2" / "Saved" / "WS2" / name)
    paths.append(ROOT / "Saved" / "WS2" / name)
    return paths


def find_result(arena, started, timeout=25.0):
    """Newest arena JSON written after the run started."""
    deadline = time.time() + timeout
    while True:
        found = []
        for path in candidate_paths(arena):
            try:
                stat = path.stat()
            except OSError:
                continue
            if stat.st_mtime >= started - 2:
                found.append((stat.st_mtime, path))
        if found:
            found.sort(reverse=True)
            return found[0][1]
        if time.time() >= deadline:
            return None
        time.sleep(1.0)


def validate(report):
    checks = []
    level_ups = report.get("levelUps", {})
    pool = report.get("shopPool", {})
    events = level_ups.get("events", [])
    level_up_sessions = [e for e in events if e.get("levelUp")]
    stat_offers = sum(
        1 for e in level_up_sessions for offer in e.get("offers", []) if offer.startswith("stat:")
    )
    session_ok = bool(events) and all(
        all(key in e for key in ("wave", "level", "levelUp", "offers", "pick")) for e in events
    )

    def check(name, ok, detail):
        checks.append({"check": name, "pass": bool(ok), "detail": detail})

    check("levelUps.count > 0", level_ups.get("count", 0) > 0, level_ups.get("count", 0))
    check("levelUps.levelUpShops > 0", level_ups.get("levelUpShops", 0) > 0, level_ups.get("levelUpShops", 0))
    check("level-up sessions with wave/level/levelUp/offers/pick", session_ok, "%d sessions" % len(events))
    check("stat offers in level-up shops > 0", stat_offers > 0, stat_offers)
    pool_sizes = {key: pool.get(key) for key in EXPECTED_POOL}
    check("shopPool sizes 40/160/44/24/5", pool_sizes == EXPECTED_POOL, pool_sizes)
    check("shopPool.complete", pool.get("complete") is True, pool.get("complete"))
    check("shopPool.fullPoolOfferable", pool.get("fullPoolOfferable") is True, pool.get("fullPoolOfferable"))
    check(
        "shopPool.offerable 40/160/44/24/5",
        pool.get("offerable") == EXPECTED_POOL,
        pool.get("offerable"),
    )
    check("shopPool.offerableMethod present", bool(pool.get("offerableMethod")), pool.get("offerableMethod"))
    check("shopPool.offeredInRun present", bool(pool.get("offeredInRun")), pool.get("offeredInRun"))
    check("arena complete (20/20 waves)", report.get("complete") is True and report.get("waves") == report.get("maxWaves"),
          "%s/%s" % (report.get("waves"), report.get("maxWaves")))
    return checks


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--package", default=str(DEFAULT_PACKAGE))
    parser.add_argument("--arena", default="kurpark")
    parser.add_argument("--timeout", type=float, default=300.0)
    parser.add_argument("--archive-dir", default=str(ROOT / "Saved" / "WS2" / "packaged_release"))
    parser.add_argument("--report", default="")
    args = parser.parse_args()

    exe = Path(args.package)
    if not exe.is_file():
        print("FAIL: package executable missing: %s" % exe)
        return 1

    log_path = ROOT / "Build" / ("packaged_release_%s.log" % args.arena)
    trace = "-ws2trace=%s" % log_path
    command = [
        str(exe),
        "L_Arena?autostart=1?arena=%s" % args.arena,
        "-ws2arena=%s" % args.arena,
        "-game",
        "-windowed",
        "-ResX=960",
        "-ResY=540",
        "-nosound",
        "-nosplash",
        trace,
        "-ws2full=1",
        "-ws2fast=100",
        "-ws2auto=60",
        "-ws2quit=120",
        "-ws2noboss=1",
        "-ws2shot=-1",
        "-ws2invuln=1",
    ]
    if log_path.exists():
        log_path.unlink()

    started = time.time()
    print("RUN  %s" % " ".join(command))
    process = subprocess.Popen(command, cwd=str(exe.parent))
    try:
        code = process.wait(timeout=args.timeout)
    except subprocess.TimeoutExpired:
        subprocess.run(["taskkill", "/F", "/T", "/PID", str(process.pid)], capture_output=True,
                      encoding="utf-8", errors="replace")
        try:
            process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
        code = -1
    print("EXIT %s after %.1fs" % (code, time.time() - started))

    result_path = find_result(args.arena, started)
    if result_path is None:
        print("FAIL: no arena result JSON written for %s" % args.arena)
        return 1

    report = json.loads(result_path.read_text(encoding="utf-8"))
    checks = validate(report)
    failed = [c for c in checks if not c["pass"]]
    for item in checks:
        print("%s  %s  (%s)" % ("PASS" if item["pass"] else "FAIL", item["check"], item["detail"]))

    archive_dir = Path(args.archive_dir)
    archive_dir.mkdir(parents=True, exist_ok=True)
    archived = archive_dir / result_path.name
    shutil.copy2(result_path, archived)

    summary = {
        "package": str(exe),
        "arena": args.arena,
        "resultPath": str(result_path),
        "archivedCopy": str(archived),
        "traceLog": str(log_path),
        "exitCode": code,
        "durationSeconds": round(time.time() - started, 1),
        "checkedAt": time.strftime("%Y-%m-%dT%H:%M:%S"),
        "levelUps": {key: report["levelUps"].get(key) for key in ("count", "finalLevel", "levelUpShops", "shopOpens", "pendingAtEnd")},
        "shopPool": {key: report["shopPool"].get(key) for key in ("weapons", "tiers", "items", "stats", "elements", "complete", "fullPoolOfferable")},
        "bossCount": report.get("bossCount"),
        "bosses": report.get("bosses"),
        "checks": checks,
        "result": "PASS" if not failed else "FAIL",
    }
    report_path = Path(args.report) if args.report else ROOT / "Saved" / "WS2" / ("packaged_proof_%s.json" % args.arena)
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print("RESULT %s  json=%s  report=%s  archived=%s" % (summary["result"], result_path, report_path, archived))
    return 0 if not failed else 1


if __name__ == "__main__":
    sys.exit(main())
