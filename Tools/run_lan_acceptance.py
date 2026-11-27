#!/usr/bin/env python3
"""Run the two-process LAN acceptance test over this machine's real LAN IP.

The test starts two independent full-game processes:

* host: the Shipping executable from the accepted package, with the normal
  lobby test hook ``-ws2testhost=1``
* client: the same executable, with ``-ws2testjoin=<LAN-IP>``

It waits for the host's UDP listener before starting the client, then checks
the package-safe NETVERIFY trace lines for session join, mutual visibility and
replicated movement in both directions. The result is written to
``Saved/WS2/lan_acceptance_report.json`` (and a timestamped copy).

This is deliberately a two-process test on one Windows host, using the real
WLAN interface address rather than loopback. It exercises the live UDP socket
and the game's real host/join path, but it is not a substitute for a physical
second device: a second machine, its firewall and the Wi-Fi/switch path remain
manual acceptance work.

Requires Python 3.8+ and Windows (PowerShell is used for interface/firewall
inspection). It does not need administrator rights unless the firewall has
explicit block rules for the target executable; in that case run
``Tools/fix_lan_firewall.ps1`` from an elevated PowerShell once.
"""

from __future__ import annotations

import argparse
import ipaddress
import json
import os
import re
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_PACKAGE_ROOT = ROOT / "Dist" / "Acceptance-AudioFinal"
DEFAULT_SHIPPING = (
    DEFAULT_PACKAGE_ROOT
    / "WiesbadenSurvivors2"
    / "Binaries"
    / "Win64"
    / "WiesbadenSurvivors2-Win64-Shipping.exe"
)
DEFAULT_EDITOR = Path("C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe")
DEFAULT_PROJECT = ROOT / "WiesbadenSurvivors2.uproject"
DEFAULT_REPORT = ROOT / "Saved" / "WS2" / "lan_acceptance_report.json"


def _run(args: list[str], *, timeout: float = 45.0) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        args,
        cwd=str(ROOT),
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=timeout,
    )


def _powershell(script: str, *, timeout: float = 45.0) -> str:
    completed = _run(
        [
            "powershell",
            "-NoProfile",
            "-ExecutionPolicy",
            "Bypass",
            "-Command",
            script,
        ],
        timeout=timeout,
    )
    if completed.returncode != 0:
        detail = (completed.stderr or completed.stdout or "").strip()
        raise RuntimeError(f"PowerShell failed ({completed.returncode}): {detail}")
    return completed.stdout.strip().lstrip("\ufeff")


def _json_or_list(raw: str) -> list[dict[str, Any]]:
    raw = raw.strip()
    if not raw:
        return []
    try:
        value = json.loads(raw)
    except json.JSONDecodeError:
        return []
    if isinstance(value, dict):
        return [value]
    if isinstance(value, list):
        return [item for item in value if isinstance(item, dict)]
    return []


def network_candidates() -> list[dict[str, Any]]:
    script = r"""
$rows = @(Get-NetIPConfiguration -ErrorAction SilentlyContinue | Where-Object {
    $_.NetAdapter.Status -eq 'Up' -and $_.IPv4Address -and $_.IPv4DefaultGateway
} | ForEach-Object {
    $address = @($_.IPv4Address)[0]
    $ip = [string]$address.IPAddress
    if ($ip -notlike '127.*' -and $ip -notlike '169.254.*') {
        $profile = Get-NetConnectionProfile -InterfaceAlias $_.InterfaceAlias -ErrorAction SilentlyContinue | Select-Object -First 1
        $category = 'Unknown'
        if ($profile) { $category = [string]$profile.NetworkCategory }
        [pscustomobject]@{
            ip = $ip
            interface = [string]$_.InterfaceAlias
            prefix = [int]$address.PrefixLength
            gateway = [string](@($_.IPv4DefaultGateway)[0].NextHop)
            category = $category
        }
    }
})
$rows | ConvertTo-Json -Compress
"""
    return _json_or_list(_powershell(script))


def resolve_lan_ip(explicit: str | None) -> tuple[str, dict[str, Any], list[dict[str, Any]]]:
    candidates = network_candidates()
    if explicit:
        try:
            address = ipaddress.ip_address(explicit)
        except ValueError as error:
            raise RuntimeError(f"--lan-ip is not a valid IP address: {explicit}") from error
        if address.is_loopback:
            raise RuntimeError("--lan-ip must not be a loopback address")
        script = (
            "$a = Get-NetIPAddress -AddressFamily IPv4 -IPAddress '"
            + explicit.replace("'", "''")
            + "' -ErrorAction SilentlyContinue; "
            "$p = Get-NetConnectionProfile -InterfaceAlias $a.InterfaceAlias -ErrorAction SilentlyContinue | Select-Object -First 1; "
            "if ($a) { $category = 'Unknown'; if ($p) { $category = [string]$p.NetworkCategory }; "
            "[pscustomobject]@{ ip=[string]$a.IPAddress; interface=[string]$a.InterfaceAlias; "
            "prefix=[int]$a.PrefixLength; category=$category } | ConvertTo-Json -Compress }"
        )
        found = _json_or_list(_powershell(script))
        if not found:
            raise RuntimeError(f"--lan-ip {explicit} is not an IPv4 address on this machine")
        info = dict(found[0])
        info.setdefault("gateway", "")
        return explicit, info, candidates

    if not candidates:
        raise RuntimeError(
            "No non-loopback IPv4 interface with a default gateway was found; "
            "connect WLAN/LAN or pass --lan-ip explicitly"
        )
    # Prefer a private WLAN/LAN address with a gateway; the machine currently
    # exposes exactly one such interface, but keep the choice deterministic.
    candidates.sort(
        key=lambda item: (
            0 if str(item.get("ip", "")).startswith("192.168.") else 1,
            0 if str(item.get("category", "")).lower() in {"private", "domainauthenticated"} else 1,
            str(item.get("interface", "")),
        )
    )
    chosen = candidates[0]
    return str(chosen["ip"]), chosen, candidates


def firewall_rules_for(exe: Path) -> list[dict[str, Any]]:
    script = r"""
$exe = '__EXE__'
$rows = @(Get-NetFirewallApplicationFilter -ErrorAction SilentlyContinue | Where-Object {
    $_.Program -eq $exe
} | ForEach-Object {
    $rule = $_ | Get-NetFirewallRule -ErrorAction SilentlyContinue
    if ($rule) {
        [pscustomobject]@{
            display = [string]$rule.DisplayName
            action = [string]$rule.Action
            enabled = [bool]$rule.Enabled
            direction = [string]$rule.Direction
            profile = [string]$rule.Profile
        }
    }
})
$rows | ConvertTo-Json -Compress
""".replace("__EXE__", str(exe).replace("'", "''"))
    return _json_or_list(_powershell(script))


def udp_listeners(port: int) -> list[dict[str, Any]]:
    try:
        completed = _run(["netstat", "-ano", "-p", "UDP"], timeout=15.0)
    except (OSError, subprocess.SubprocessError):
        return []
    listeners: list[dict[str, Any]] = []
    suffix = f":{port}"
    for line in completed.stdout.splitlines():
        parts = line.split()
        if len(parts) >= 4 and parts[0].upper() == "UDP" and parts[1].endswith(suffix):
            listeners.append({"local": parts[1], "pid": parts[-1], "line": line.strip()})
    return listeners


def read_lines(path: Path) -> list[str]:
    # The Shipping trace writer can hold the file exclusively for a very short
    # moment between records.  A transient sharing violation must not abort a
    # live acceptance run, so retry briefly instead of treating it as failure.
    for _ in range(40):
        try:
            return path.read_text(encoding="utf-8", errors="replace").splitlines()
        except FileNotFoundError:
            return []
        except PermissionError:
            time.sleep(0.05)
        except OSError as error:
            if getattr(error, "winerror", None) in (32, 33) or error.errno in (13, 32):
                time.sleep(0.05)
                continue
            return []
    return []


def file_size(path: Path) -> int:
    try:
        return path.stat().st_size
    except OSError:
        return 0


def matching(lines: list[str], pattern: str) -> list[str]:
    return [line for line in lines if re.search(pattern, line)]


def first(lines: list[str], pattern: str) -> str | None:
    found = matching(lines, pattern)
    return found[0] if found else None


def parse_pawn(host_join: str | None) -> str | None:
    if not host_join:
        return None
    match = re.search(r"pawn=(\S+)", host_join)
    return match.group(1) if match else None


def evidence_from_traces(host: list[str], client: list[str]) -> dict[str, Any]:
    host_join = first(host, r"PLAYER JOIN slot=\d+ .*players=2")
    host_client_pawn = parse_pawn(host_join)
    client_begin = first(client, r"NETVERIFY CLIENT BEGIN players=2")
    host_begin = first(host, r"NETVERIFY HOST BEGIN players=2")
    client_host_visible = first(client, r"NETVERIFY CLIENT HOST VISIBLE")
    client_local_moved = first(client, r"NETVERIFY CLIENT MOVED .*distance=")
    client_host_moved = first(client, r"NETVERIFY CLIENT HOST MOVED .*distance=")
    host_own_moved = first(host, r"NETVERIFY SERVER MOVED .* local=1 .*distance=")
    host_client_input = (
        first(host, r"NETVERIFY SERVER INPUT pawn=" + re.escape(host_client_pawn))
        if host_client_pawn
        else None
    )
    host_client_moved = (
        first(host, r"NETVERIFY SERVER MOVED pawn=" + re.escape(host_client_pawn) + r" local=0 .*distance=")
        if host_client_pawn
        else None
    )
    host_client_postlogin = first(host, r"POSTLOGIN .* local=0 .*players=1")
    host_complete = first(host, r"NETVERIFY HOST COMPLETE")
    client_complete = first(client, r"NETVERIFY CLIENT COMPLETE")
    client_local_probe = first(client, r"NETVERIFY CLIENT LOCAL PROBE .* local=1")
    host_wave = None
    client_wave = None
    if host_begin:
        wave = re.search(r"wave=(\d+)", host_begin)
        host_wave = int(wave.group(1)) if wave else None
    if client_begin:
        wave = re.search(r"wave=(\d+)", client_begin)
        client_wave = int(wave.group(1)) if wave else None
    return {
        "host_postlogin": host_client_postlogin,
        "host_join": host_join,
        "host_client_pawn": host_client_pawn,
        "host_begin": host_begin,
        "client_begin": client_begin,
        "host_wave": host_wave,
        "client_wave": client_wave,
        "client_host_visible": client_host_visible,
        "client_local_moved": client_local_moved,
        "client_host_moved": client_host_moved,
        "client_local_probe": client_local_probe,
        "host_own_moved": host_own_moved,
        "host_client_input": host_client_input,
        "host_client_moved": host_client_moved,
        "host_complete": host_complete,
        "client_complete": client_complete,
    }


def launch(exe: Path, args: list[str], cwd: Path) -> subprocess.Popen[str]:
    return subprocess.Popen(
        [str(exe), *args],
        cwd=str(cwd),
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        creationflags=getattr(subprocess, "CREATE_NEW_PROCESS_GROUP", 0),
    )


def stop_process(process: subprocess.Popen[str] | None) -> None:
    if process is None:
        return
    if process.poll() is not None:
        return
    try:
        process.terminate()
        process.wait(timeout=8)
        return
    except (OSError, subprocess.SubprocessError):
        pass
    try:
        subprocess.run(
            ["taskkill", "/PID", str(process.pid), "/T", "/F"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            timeout=15,
            check=False,
        )
    except (OSError, subprocess.SubprocessError):
        pass


def json_safe(value: Any) -> Any:
    if isinstance(value, Path):
        return str(value)
    if isinstance(value, dict):
        return {str(key): json_safe(item) for key, item in value.items()}
    if isinstance(value, list):
        return [json_safe(item) for item in value]
    return value


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--lan-ip", help="LAN IPv4 address; default is the active non-loopback interface")
    parser.add_argument("--port", type=int, default=7777, help="UDP port (default: 7777)")
    parser.add_argument(
        "--mode",
        choices=("shipping", "editor"),
        default="shipping",
        help="shipping uses the accepted package's Shipping binary; editor uses the project in -game mode",
    )
    parser.add_argument("--shipping-exe", type=Path, default=DEFAULT_SHIPPING)
    parser.add_argument("--editor-exe", type=Path, default=DEFAULT_EDITOR)
    parser.add_argument("--project", type=Path, default=DEFAULT_PROJECT)
    parser.add_argument("--out-dir", type=Path, default=None)
    parser.add_argument("--report", type=Path, default=DEFAULT_REPORT)
    parser.add_argument("--timeout", type=float, default=75.0, help="seconds to wait for the run to produce evidence")
    args = parser.parse_args()

    if os.name != "nt":
        print("This acceptance runner currently requires Windows.", file=sys.stderr)
        return 2

    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    if args.out_dir is None:
        out_dir = ROOT / "Saved" / "WS2" / "lan_acceptance" / stamp
    else:
        out_dir = args.out_dir if args.out_dir.is_absolute() else ROOT / args.out_dir
    out_dir.mkdir(parents=True, exist_ok=True)
    host_trace = out_dir / "host.trace.log"
    client_trace = out_dir / "client.trace.log"
    host_log = out_dir / "host.engine.log"
    client_log = out_dir / "client.engine.log"
    for path in (host_trace, client_trace, host_log, client_log):
        try:
            path.unlink()
        except FileNotFoundError:
            pass

    report: dict[str, Any] = {
        "schema": "ws2.lan_acceptance.v1",
        "generated_utc": datetime.now(timezone.utc).isoformat(),
        "result": "FAIL",
        "command": sys.argv,
        "checks": {},
        "execution": {},
        "network": {},
        "firewall": {},
        "evidence": {},
        "limits": [],
        "logs": {},
    }
    host_process: subprocess.Popen[str] | None = None
    client_process: subprocess.Popen[str] | None = None
    failure: str | None = None
    listener: dict[str, Any] | None = None

    try:
        lan_ip, lan_info, candidates = resolve_lan_ip(args.lan_ip)
        report["network"] = {
            **lan_info,
            "ip": lan_ip,
            "is_loopback": False,
            "candidates": candidates,
            "port": args.port,
        }
        if args.mode == "shipping" and not DEFAULT_PACKAGE_ROOT.is_dir():
            raise RuntimeError(
                f"Accepted package directory is missing: {DEFAULT_PACKAGE_ROOT}. "
                "Use --mode editor or extract the accepted package first."
            )

        if args.mode == "shipping":
            exe = args.shipping_exe if args.shipping_exe.is_absolute() else ROOT / args.shipping_exe
            cwd = exe.parent
            if not exe.is_file():
                raise RuntimeError(f"Shipping executable is missing: {exe}")
            common = [
                "-windowed",
                "-ResX=800",
                "-ResY=600",
                "-nosplash",
                "-nosound",
                "-ws2netverify=1",
                "-ws2invuln=1",
            ]
            host_args = [*common, "-ws2testhost=1", f"-ws2trace={host_trace}"]
            client_args = [*common, f"-ws2testjoin={lan_ip}", "-ws2hero=manni", f"-ws2trace={client_trace}"]
            entrypoint = "shipping"
        else:
            exe = args.editor_exe
            cwd = ROOT
            if not exe.is_file():
                raise RuntimeError(f"Editor executable is missing: {exe}")
            if not args.project.is_file():
                raise RuntimeError(f"Project file is missing: {args.project}")
            common = [
                str(args.project),
                "-game",
                "-nullrhi",
                "-unattended",
                "-nosplash",
                "-nosound",
                "-ws2netverify=1",
                "-ws2invuln=1",
            ]
            host_args = [*common, "-ws2testhost=1", f"-ws2trace={host_trace}", f"-abslog={host_log}"]
            client_args = [*common, f"-ws2testjoin={lan_ip}", "-ws2hero=manni", f"-ws2trace={client_trace}", f"-abslog={client_log}"]
            entrypoint = "editor"

        report["execution"] = {
            "entrypoint": entrypoint,
            "executable": str(exe),
            "working_directory": str(cwd),
            "host_args": host_args,
            "client_args": client_args,
            "client_target": f"{lan_ip}:{args.port}",
            "port": args.port,
            "host_pid": None,
            "client_pid": None,
            "host_udp_endpoint": None,
            "host_udp_pid": None,
            "host_exit_code": None,
            "client_exit_code": None,
            "cleanup": "not-needed",
        }

        rules = firewall_rules_for(exe)
        blocking = [
            rule for rule in rules
            if str(rule.get("action", "")).lower() == "block" and bool(rule.get("enabled", False))
        ]
        allowed = [
            rule for rule in rules
            if str(rule.get("action", "")).lower() == "allow" and bool(rule.get("enabled", False))
        ]
        report["firewall"] = {
            "target": str(exe),
            "rules": rules,
            "blocking_rules": blocking,
            "allow_rules": allowed,
            "status": "BLOCKED" if blocking else "OK",
        }
        if blocking:
            raise RuntimeError(
                "Inbound firewall block rules exist for the test executable; "
                "run Tools/fix_lan_firewall.ps1 from an elevated PowerShell and retry"
            )

        existing = udp_listeners(args.port)
        if existing:
            raise RuntimeError(
                f"UDP port {args.port} already has a listener; stop it before the test: {existing}"
            )

        host_process = launch(exe, host_args, cwd)
        report["execution"]["host_pid"] = host_process.pid
        print(f"host started pid={host_process.pid} target={lan_ip}:{args.port}")

        deadline = time.monotonic() + args.timeout
        while time.monotonic() < deadline:
            listener = next(
                (item for item in udp_listeners(args.port) if item["local"] in ("0.0.0.0:" + str(args.port), "[::]:" + str(args.port))),
                None,
            )
            if listener is not None:
                break
            if host_process.poll() is not None:
                raise RuntimeError(f"host exited before binding UDP {args.port} (code {host_process.returncode})")
            time.sleep(0.25)
        if listener is None:
            raise RuntimeError(f"host did not bind UDP {args.port} within {args.timeout:.0f}s")

        report["execution"]["host_udp_endpoint"] = listener["local"]
        report["execution"]["host_udp_pid"] = listener["pid"]
        if listener["pid"] != str(host_process.pid) and entrypoint == "shipping":
            raise RuntimeError(
                f"UDP {args.port} is owned by PID {listener['pid']}, not the test host PID {host_process.pid}"
            )
        print(f"host listener {listener['local']} pid={listener['pid']}")

        client_process = launch(exe, client_args, cwd)
        report["execution"]["client_pid"] = client_process.pid
        print(f"client started pid={client_process.pid}")

        evidence: dict[str, Any] = {}
        deadline = time.monotonic() + args.timeout
        while time.monotonic() < deadline:
            host_lines_now = read_lines(host_trace)
            client_lines_now = read_lines(client_trace)
            evidence = evidence_from_traces(host_lines_now, client_lines_now)
            required = (
                evidence["host_postlogin"]
                and evidence["host_join"]
                and evidence["host_begin"]
                and evidence["client_begin"]
                and evidence["client_host_visible"]
                and evidence["client_local_moved"]
                and evidence["client_host_moved"]
                and evidence["host_own_moved"]
                and evidence["host_client_input"]
                and evidence["host_client_moved"]
            )
            if required:
                # Give the deterministic netverify window its normal end so the
                # report also captures the host's COMPLETE line.  The required
                # join/visibility/movement evidence is already present here.
                settle_deadline = time.monotonic() + 15.0
                while time.monotonic() < settle_deadline:
                    settled = evidence_from_traces(read_lines(host_trace), read_lines(client_trace))
                    if settled["host_complete"] or host_process.poll() is not None:
                        break
                    time.sleep(0.25)
                time.sleep(0.5)
                evidence = evidence_from_traces(read_lines(host_trace), read_lines(client_trace))
                break
            if host_process.poll() is not None and client_process.poll() is not None:
                break
            time.sleep(0.25)

        evidence = evidence_from_traces(read_lines(host_trace), read_lines(client_trace))
        report["evidence"] = evidence
        report["execution"]["host_exit_code"] = host_process.poll()
        report["execution"]["client_exit_code"] = client_process.poll()

        checks = {
            "non_loopback_lan_ip": not ipaddress.ip_address(lan_ip).is_loopback,
            "host_udp_listener": bool(listener),
            "client_target_uses_lan_ip": f"-ws2testjoin={lan_ip}" in client_args,
            "host_joined_session": bool(evidence["host_postlogin"] and evidence["host_join"] and evidence["host_begin"]),
            "client_joined_session": bool(evidence["client_begin"]),
            "host_sees_client": bool(evidence["host_postlogin"] and evidence["host_join"]),
            "client_sees_host": bool(evidence["client_host_visible"]),
            "client_movement_replicated_to_host": bool(evidence["host_client_input"] and evidence["host_client_moved"]),
            "host_movement_replicated_to_client": bool(evidence["client_host_moved"] and evidence["host_own_moved"]),
            "same_player_count": evidence["host_wave"] is not None
            and evidence["client_wave"] is not None
            and "players=2" in (evidence["host_begin"] or "")
            and "players=2" in (evidence["client_begin"] or ""),
            "client_self_movement": bool(evidence["client_local_moved"]),
        }
        report["checks"] = checks
        report["result"] = "PASS" if all(checks.values()) else "FAIL"
        if report["result"] != "PASS":
            raise RuntimeError("one or more LAN acceptance checks failed")
    except Exception as error:  # noqa: BLE001 - the report must record the exact failure
        failure = str(error)
        report["failure"] = failure
        report["result"] = "FAIL"
        print(f"FAIL: {failure}", file=sys.stderr)

    finally:
        before_cleanup = {
            "host_pid": host_process.pid if host_process else None,
            "client_pid": client_process.pid if client_process else None,
        }
        for process in (client_process, host_process):
            stop_process(process)
        report["execution"]["host_exit_code"] = host_process.poll() if host_process else None
        report["execution"]["client_exit_code"] = client_process.poll() if client_process else None
        report["execution"]["cleanup"] = {
            "pids_before": before_cleanup,
            "pids_running_after": [
                {"pid": process.pid, "poll": process.poll()}
                for process in (client_process, host_process)
                if process is not None and process.poll() is None
            ],
        }
        report["logs"] = {
            "host_trace": str(host_trace),
            "client_trace": str(client_trace),
            "host_engine_log": str(host_log) if host_log.exists() else None,
            "client_engine_log": str(client_log) if client_log.exists() else None,
            "host_trace_size": file_size(host_trace),
            "client_trace_size": file_size(client_trace),
        }
        report["limits"] = [
            "Two independent game processes on the same Windows host, using the real non-loopback WLAN IPv4 interface; no loopback address is used for the client target.",
            "A physical second Windows device, its own firewall/NAT state and the actual WLAN/switch path are not exercised by this runner.",
            "The Shipping build writes NETVERIFY trace lines but no engine log; this runner therefore proves the session via the client target, the observed host UDP listener and the two trace streams.",
            "A real two-device WLAN/LAN session remains manual acceptance work; completing this runner does not claim that physical check.",
        ]
        report["limits"].append(
            "The deterministic netverify run is intentionally short; the host may exit its proof window just before the client writes its own COMPLETE line. Session join, mutual visibility and both movement directions are captured before that point."
        )
        if failure:
            report["failure"] = failure
        report = json_safe(report)
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
        (out_dir / "report.json").write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
        print(f"report: {args.report}")
        print(f"logs:   {out_dir}")
        print(f"result: {report['result']}")
        print(json.dumps(report["checks"], indent=2))
    return 0 if report.get("result") == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
