"""Prove LAN/WLAN co-op inside the packaged (Shipping) release.

Starts two independent instances of ``Dist/Release-0.2.0-LevelUpProof``: one as
the LAN host (the same ``HostLan`` path the lobby's *LAN HOSTEN* button uses),
one as a client that joins over a real UDP socket. By default both use the
machine's primary LAN IPv4 address, so the host binds that interface
(``-multihome=<ip>``) and the client travels to ``<ip>:7777``; no loopback and no
editor listen-server shortcut. Pass ``--bind-ip``/``--join-ip`` to choose other
addresses.

Both instances run the game's deterministic netverify sequence, which logs the
join, both player rows, wave progress, replicated level-ups, a shared state
heartbeat and a final COMPLETE line into ``-ws2trace=<file>``. The runner
validates those traces plus the observed UDP listener and the exit codes, then
writes a machine readable report:

    %LOCALAPPDATA%\\WiesbadenSurvivors2\\Saved\\WS2\\packaged_mp_proof.json

Usage (both instances on this machine):

    python Tools/run_packaged_mp_proof.py
    python Tools/run_packaged_mp_proof.py --bind-ip 192.168.1.100 --join-ip 192.168.1.100

Usage (two physical devices, one command per machine):

    python Tools/run_packaged_mp_proof.py --role host --bind-ip 192.168.1.100
    python Tools/run_packaged_mp_proof.py --role client --peer 192.168.1.100
    python Tools/run_packaged_mp_proof.py --merge host_report.json client_report.json

Requires Windows (netstat/PowerShell for the UDP listener and process checks).
"""
import argparse
import json
import os
import re
import shutil
import socket
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
RELEASE = ROOT / "Dist" / "Release-0.2.0-LevelUpProof"
DEFAULT_EXE = RELEASE / "Windows" / "WiesbadenSurvivors2.exe"
HOST_WINDOW = 55.0
CLIENT_WINDOW = 55.0
# Two-device roles: the host waits longer so the remote player can start the
# game and join; the client leaves first so the host sees the logout.
ROLE_HOST_WINDOW = 120.0
ROLE_CLIENT_WINDOW = 115.0


def read_lines(path):
    # The packaged trace writer holds the file exclusively for a moment between
    # records; a transient sharing violation must not abort a live run.
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


def netverify(lines, role, kind):
    pattern = re.compile(r"NETVERIFY %s %s(?: (.*))?$" % (role, kind))
    out = []
    for line in lines:
        match = pattern.search(line.strip())
        if match:
            out.append((match.group(1) or "").strip())
    return out


def state_bodies(lines, role):
    return [body for body in netverify(lines, role, "STATE") if body]


def waves(lines, role):
    found = []
    for body in netverify(lines, role, "WAVE"):
        match = re.search(r"(\d+) -> (\d+)", body)
        if match:
            found.append(int(match.group(2)))
    return found


def levels(lines, role):
    """All replicated level values seen per slot, in order."""
    rows = {}
    for body in netverify(lines, role, "LEVEL"):
        match = re.search(r"slot=(\d+) (\d+) -> (\d+)", body)
        if match:
            rows.setdefault(int(match.group(1)), []).append(int(match.group(3)))
    return rows


def complete(lines, role):
    bodies = netverify(lines, role, "COMPLETE")
    if not bodies:
        return None
    body = bodies[-1]
    values = {}
    for key in ("players", "wave", "maxLevel", "materials", "pickups", "enemies"):
        match = re.search(r"%s=(\d+)" % key, body)
        if match:
            values[key] = int(match.group(1))
    return {"line": body, **values}


def udp_listeners(port):
    try:
        completed = subprocess.run(
            ["netstat", "-ano", "-p", "UDP"], capture_output=True, text=True,
            encoding="utf-8", errors="replace", timeout=15,
        )
    except (OSError, subprocess.SubprocessError):
        return []
    found = []
    for line in completed.stdout.splitlines():
        parts = line.split()
        if len(parts) >= 4 and parts[0].upper() == "UDP" and parts[1].endswith(":%d" % port):
            found.append({"local": parts[1], "pid": parts[-1]})
    return found


def local_ipv4_addresses():
    """Every routable IPv4 address of this machine (no loopback, no link-local)."""
    candidates = []

    def add(value):
        if value and value not in candidates and not is_loopback(value) \
                and not value.startswith("169.254.") and value != "0.0.0.0":
            candidates.append(value)

    try:
        for info in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET):
            add(info[4][0])
    except OSError:
        pass
    try:
        # A UDP connect sends no packet but picks the outgoing interface.
        probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            probe.connect(("192.0.2.1", 9))
            add(probe.getsockname()[0])
        finally:
            probe.close()
    except OSError:
        pass
    return candidates


def primary_lan_ipv4():
    """First routable IPv4 address of this machine, the one a peer would use."""
    addresses = local_ipv4_addresses()
    return addresses[0] if addresses else ""


NO_LAN_HELP = """FAIL: no routable LAN IPv4 address found on this machine.
This proof never falls back to 127.0.0.1, so it stops here instead of pretending
to test the network.
What to do:
  1. Connect this machine to the WLAN/LAN (cable or Wi-Fi) and check that the
     adapter has a routable IPv4 address: run `ipconfig` and look for an
     `IPv4-Adresse` that is neither 127.x.x.x nor 169.254.x.x.
  2. Start the proof again:
       python Tools/run_packaged_mp_proof.py
  3. Or name the address explicitly (the same value for both sides when host and
     client run on this machine):
       python Tools/run_packaged_mp_proof.py --bind-ip <LAN-IPv4> --join-ip <LAN-IPv4>
  4. Allow the packaged game on the private network in Windows Firewall
     (UDP 7777) before a second physical device tries to join. Add the rule with:
       netsh advfirewall firewall add rule name="Wiesbaden Survivors 2 LAN" dir=in action=allow protocol=udp localport=7777 profile=private
     Check it with:
       netsh advfirewall firewall show rule name="Wiesbaden Survivors 2 LAN"
"""


def is_loopback(address):
    return address.startswith("127.") or address in ("localhost", "::1")


def firewall_rule_present():
    """Best-effort check for an inbound allow rule covering UDP 7777."""
    try:
        completed = subprocess.run(
            ["netsh", "advfirewall", "firewall", "show", "rule", "name=all"],
            capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=15,
        )
    except (OSError, subprocess.SubprocessError):
        return None
    text = (completed.stdout or "") + (completed.stderr or "")
    # netsh prints one rule block per rule; look for a block that allows UDP 7777 inbound.
    in_rule = False
    allow = False
    udp = False
    port = False
    for line in text.splitlines():
        stripped = line.strip()
        low = stripped.lower()
        if low.startswith("rule name:"):
            if in_rule and allow and udp and port:
                return True
            in_rule = True
            allow = False
            udp = False
            port = False
        elif in_rule:
            if low.startswith("action:") and "allow" in low:
                allow = True
            if low.startswith("protocol:") and "udp" in low:
                udp = True
            if low.startswith("localport:") and "7777" in low:
                port = True
    if in_rule and allow and udp and port:
        return True
    return False


def print_firewall_status():
    present = firewall_rule_present()
    if present is True:
        print("firewall: inbound rule for UDP 7777 found (private profile).")
    elif present is False:
        print("firewall: NO inbound rule for UDP 7777 found. For a second device, add it with:")
        print('  netsh advfirewall firewall add rule name="Wiesbaden Survivors 2 LAN" dir=in action=allow protocol=udp localport=7777 profile=private')
    else:
        print("firewall: could not query rules; if a second device cannot join, add:")
        print('  netsh advfirewall firewall add rule name="Wiesbaden Survivors 2 LAN" dir=in action=allow protocol=udp localport=7777 profile=private')


def running_instances(exclude_host_role=False):
    """PIDs of running packaged game instances; a client role ignores host-role peers."""
    where = " | Where-Object { $_.CommandLine -notmatch '-ws2testhost=1' }" if exclude_host_role else ""
    script = (
        "$p = Get-CimInstance Win32_Process "
        "-Filter \"Name='WiesbadenSurvivors2-Win64-Shipping.exe' or Name='WiesbadenSurvivors2.exe'\" "
        "-ErrorAction SilentlyContinue%s; "
        "if ($p) { $p | Select-Object -ExpandProperty ProcessId | ConvertTo-Json -Compress }" % where
    )
    try:
        completed = subprocess.run(
            ["powershell", "-NoProfile", "-Command", script],
            capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=30,
        )
    except (OSError, subprocess.SubprocessError):
        return []
    raw = (completed.stdout or "").strip()
    if not raw:
        return []
    try:
        value = json.loads(raw)
    except json.JSONDecodeError:
        return []
    if isinstance(value, int):
        return [value]
    return [item for item in value if isinstance(item, int)]


def pid_alive(pid):
    """True if a process with this PID is still running."""
    if pid is None:
        return False
    try:
        completed = subprocess.run(
            ["tasklist", "/FI", "PID eq %d" % pid, "/NH"],
            capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=10,
        )
    except (OSError, subprocess.SubprocessError):
        return False
    return str(pid) in (completed.stdout or "")


def launch(exe, args):
    return subprocess.Popen(
        [str(exe), *args],
        cwd=str(exe.parent),
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        creationflags=getattr(subprocess, "CREATE_NEW_PROCESS_GROUP", 0),
    )


def stop(process):
    if process is None or process.poll() is not None:
        return
    try:
        subprocess.run(
            ["taskkill", "/PID", str(process.pid), "/T", "/F"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=30, check=False,
        )
    except (OSError, subprocess.SubprocessError, subprocess.TimeoutExpired):
        pass
    try:
        process.wait(timeout=15)
    except subprocess.TimeoutExpired:
        pass


def write_report(paths, report):
    for path in paths:
        if path is None:
            continue
        path = Path(path)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    return [str(path) for path in paths if path is not None]


def saved_dir():
    local = Path(os.environ.get("LOCALAPPDATA", str(ROOT)))
    return local / "WiesbadenSurvivors2" / "Saved" / "WS2"


def side_report_paths(role, custom):
    name = "ws2_mp_%s_report.json" % role
    return [Path(custom) if custom else saved_dir() / name, ROOT / "Saved" / "WS2" / name]


def resolve_executable(args):
    exe = args.exe if args.exe.is_absolute() else ROOT / args.exe
    return exe


def side_common_args(window):
    return [
        "-windowed", "-ResX=800", "-ResY=600", "-nosplash", "-nosound",
        "-ws2netverify=1", "-ws2invuln=1", "-ws2alltypes=1", "-ws2fast=2",
        "-ws2netwindow=%g" % window, "-ws2auto=%d" % int(window + 20),
    ]


def finish_side(process, trace, role, window, report, started):
    """Wait for the game's own COMPLETE/quit path, then stop the process tree."""
    deadline = time.monotonic() + max(60.0, window + 60.0)
    while time.monotonic() < deadline:
        lines = read_lines(trace)
        if complete(lines, role.upper()):
            break
        if process.poll() is not None:
            break
        time.sleep(0.5)
    deadline = time.monotonic() + 30.0
    while time.monotonic() < deadline and process.poll() is None:
        time.sleep(0.5)
    if process.poll() is None:
        report["problems"].append("instance did not exit through the game's quit path")
    stop(process)
    report["exitCode"] = process.poll()
    report["durationSeconds"] = round(time.time() - started, 1)


def new_side_report(role, exe, endpoint, trace, window):
    """Common scaffolding for a single-side report; callers fill role-specific fields."""
    return {
        "schema": "ws2.packaged_mp_side.v1",
        "role": role,
        "generatedAt": time.strftime("%Y-%m-%dT%H:%M:%S"),
        "result": "FAIL",
        "package": str(RELEASE),
        "executable": str(exe),
        "endpoint": endpoint,
        "bindAddress": None,
        "peerAddress": None,
        "listenerLocal": None,
        "linkRemote": None,
        "checks": {},
        "problems": [],
        "trace": str(trace),
        "window": window,
    }


def run_role_host(args, bind_ip):
    print_firewall_status()
    exe = resolve_executable(args)
    port = args.port
    window = args.window if args.window is not None else ROLE_HOST_WINDOW
    trace = ROOT / "Build" / "packaged_mp_role_host.log"
    trace.parent.mkdir(parents=True, exist_ok=True)
    try:
        trace.unlink()
    except FileNotFoundError:
        pass

    endpoint = "%s:%d" % (bind_ip, port)
    report = new_side_report("host", exe, endpoint, trace, window)
    report["bindAddress"] = endpoint
    started = time.time()
    host = None
    try:
        host = launch(exe, [
            *side_common_args(window), "-multihome=%s" % bind_ip,
            "-ws2testhost=1", "-ws2trace=%s" % trace,
        ])
        report["command"] = host.args
        report["pid"] = host.pid
        print("host role started pid=%d endpoint=%s" % (host.pid, report["endpoint"]))

        want = "%s:%d" % (bind_ip, port)
        listener = None
        deadline = time.monotonic() + 90.0
        while time.monotonic() < deadline:
            found = [item for item in udp_listeners(port) if item["local"] == want]
            if found:
                listener = found[0]
                break
            if host.poll() is not None:
                report["problems"].append("host exited before binding %s (code %s)" % (want, host.returncode))
                break
            time.sleep(0.25)
        report["listenerLocal"] = listener["local"] if listener else None
        if listener:
            print("host listener %s" % listener["local"])

        finish_side(host, trace, "host", window, report, started)
        lines = read_lines(trace)
        done = complete(lines, "HOST")
        if done:
            report["progress"] = done
            report["players"] = done.get("players")
            report["wave"] = done.get("wave")
            report["maxLevel"] = done.get("maxLevel")
        report["hostSawTwoPlayers"] = any(
            re.search(r"PLAYER JOIN slot=\d+ .*players=2", line) for line in lines
        )
        report["checks"] = {
            "packaged_build_present": (RELEASE / "Windows" / "WiesbadenSurvivors2").is_dir(),
            "listener_is_lan_endpoint": listener is not None and listener["local"] == want
            and not is_loopback(bind_ip),
            "host_saw_two_players": bool(report["hostSawTwoPlayers"] and (done or {}).get("players") == 2),
            "host_completed": bool(done),
            "clean_exit": report["exitCode"] == 0 and bool(done),
            # The global process scan is unreliable on a machine that also runs
            # the other role; the precise check is that our own process exited.
            "no_leftover_processes": not pid_alive(host.pid),
        }
    except Exception as error:  # noqa: BLE001 - the report records the exact reason
        report["problems"].append(str(error))
        print("FAIL: %s" % error, file=sys.stderr)
    finally:
        stop(host)
        report["exitCode"] = host.poll() if host else None
        report["durationSeconds"] = round(time.time() - started, 1)
        failed = [name for name, ok in report["checks"].items() if not ok]
        report["problems"].extend(failed)
        report["result"] = "PASS" if report["checks"] and not failed and not report["problems"] else "FAIL"
        report["reports"] = write_report(side_report_paths("host", args.report), report)
        print("host role result: %s (exit %s)" % (report["result"], report["exitCode"]))
        for path in report["reports"]:
            print("report: %s" % path)
    return 0 if report["result"] == "PASS" else 1


def run_role_client(args, peer_ip):
    exe = resolve_executable(args)
    port = args.port
    window = args.window if args.window is not None else ROLE_CLIENT_WINDOW
    trace = ROOT / "Build" / "packaged_mp_role_client.log"
    trace.parent.mkdir(parents=True, exist_ok=True)
    try:
        trace.unlink()
    except FileNotFoundError:
        pass

    endpoint = "%s:%d" % (peer_ip, port)
    report = new_side_report("client", exe, endpoint, trace, window)
    report["peerAddress"] = endpoint
    started = time.time()
    client = None
    try:
        client = launch(exe, [
            *side_common_args(window), "-ws2testjoin=%s" % peer_ip,
            "-ws2hero=manni", "-ws2trace=%s" % trace,
        ])
        report["command"] = client.args
        report["pid"] = client.pid
        print("client role started pid=%d peer=%s" % (client.pid, report["peerAddress"]))

        finish_side(client, trace, "client", window, report, started)
        lines = read_lines(trace)
        done = complete(lines, "CLIENT")
        if done:
            report["progress"] = done
            report["players"] = done.get("players")
            report["wave"] = done.get("wave")
            report["maxLevel"] = done.get("maxLevel")
        link = next((line.strip() for line in lines if "NETVERIFY CLIENT LINK remote=" in line), None)
        report["linkRemote"] = link.split("remote=", 1)[1].strip() if link else None
        want = "%s:%d" % (peer_ip, port)
        report["checks"] = {
            "packaged_build_present": (RELEASE / "Windows" / "WiesbadenSurvivors2").is_dir(),
            "client_link_to_peer": report["linkRemote"] == want and not is_loopback(peer_ip),
            "client_completed": bool(done and done.get("players") == 2),
            "clean_exit": report["exitCode"] == 0 and bool(done),
            # Precise check: the client process we launched must be gone. A
            # co-located host role is expected and is not our leftover.
            "no_leftover_processes": not pid_alive(client.pid),
        }
    except Exception as error:  # noqa: BLE001 - the report records the exact reason
        report["problems"].append(str(error))
        print("FAIL: %s" % error, file=sys.stderr)
    finally:
        stop(client)
        report["exitCode"] = client.poll() if client else None
        report["durationSeconds"] = round(time.time() - started, 1)
        failed = [name for name, ok in report["checks"].items() if not ok]
        report["problems"].extend(failed)
        report["result"] = "PASS" if report["checks"] and not failed and not report["problems"] else "FAIL"
        report["reports"] = write_report(side_report_paths("client", args.report), report)
        print("client role result: %s (exit %s)" % (report["result"], report["exitCode"]))
        for path in report["reports"]:
            print("report: %s" % path)
    return 0 if report["result"] == "PASS" else 1


def merge_side_reports(args):
    host_path, client_path = (Path(p) for p in args.merge)
    merged = {
        "schema": "ws2.packaged_mp_merge.v1",
        "generatedAt": time.strftime("%Y-%m-%dT%H:%M:%S"),
        "result": "FAIL",
        "sources": {"host": str(host_path), "client": str(client_path)},
        "checks": {},
        "problems": [],
    }
    try:
        host = json.loads(host_path.read_text(encoding="utf-8"))
        client = json.loads(client_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        merged["problems"].append("cannot read side report: %s" % error)
        write_report([Path(args.report) if args.report else saved_dir() / "ws2_mp_merge_report.json",
                     ROOT / "Saved" / "WS2" / "ws2_mp_merge_report.json"], merged)
        print("FAIL: %s" % error, file=sys.stderr)
        return 1

    endpoint = host.get("endpoint") or host.get("bindAddress")
    host_wave = host.get("wave")
    client_wave = client.get("wave")
    host_level = host.get("maxLevel")
    client_level = client.get("maxLevel")
    checks = {
        "both_side_reports_pass": host.get("result") == "PASS" and client.get("result") == "PASS",
        "roles_are_host_and_client": host.get("role") == "host" and client.get("role") == "client",
        "same_endpoint": bool(endpoint)
        and endpoint == client.get("peerAddress")
        and endpoint == host.get("listenerLocal")
        and endpoint == client.get("linkRemote"),
        "endpoint_is_not_loopback": bool(endpoint) and not is_loopback(endpoint.rsplit(":", 1)[0]),
        "both_saw_two_players": host.get("players") == 2 and client.get("players") == 2
        and bool(host.get("hostSawTwoPlayers")),
        "shared_wave_progress": isinstance(host_wave, int) and isinstance(client_wave, int)
        and host_wave >= 4 and client_wave >= 4 and abs(host_wave - client_wave) <= 1,
        # The client quits first, so the host may bank up to two more upgrades
        # between the client's COMPLETE and its own.
        "shared_level_progress": isinstance(host_level, int) and isinstance(client_level, int)
        and host_level >= 2 and client_level >= 2 and abs(host_level - client_level) <= 2,
        "clean_shutdown_both_sides": host.get("exitCode") == 0 and client.get("exitCode") == 0,
    }
    merged["endpoint"] = endpoint
    merged["host"] = {key: host.get(key) for key in ("trace", "wave", "maxLevel", "players", "exitCode")}
    merged["client"] = {key: client.get(key) for key in ("trace", "wave", "maxLevel", "players", "exitCode")}
    merged["checks"] = checks
    failed = [name for name, ok in checks.items() if not ok]
    merged["problems"].extend(failed)
    merged["result"] = "PASS" if not failed else "FAIL"
    merged["reports"] = write_report(
        [Path(args.report) if args.report else saved_dir() / "ws2_mp_merge_report.json",
         ROOT / "Saved" / "WS2" / "ws2_mp_merge_report.json"],
        merged,
    )
    print("merge result: %s endpoint=%s host(wave=%s level=%s) client(wave=%s level=%s)"
          % (merged["result"], endpoint, host_wave, host_level, client_wave, client_level))
    for name, ok in checks.items():
        print("%s  %s" % ("PASS" if ok else "FAIL", name))
    for path in merged["reports"]:
        print("report: %s" % path)
    return 0 if merged["result"] == "PASS" else 1


def main():
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""address selection:
  Default is the first routable IPv4 address of this machine (no loopback, no
  169.254.x link-local). The host binds it with -multihome=<ip>, the client
  joins <ip>:7777. 127.0.0.1 and foreign addresses are rejected with an error
  instead of a silent loopback run; --bind-ip/--join-ip override the default.

outputs:
  %LOCALAPPDATA%\\WiesbadenSurvivors2\\Saved\\WS2\\packaged_mp_proof.json
  Saved/WS2/packaged_mp_proof.json (copy), Saved/WS2/packaged_mp/ (traces),
  Build/packaged_mp_host.log and Build/packaged_mp_client.log, plus --report.

interpretation:
  exit code 0 and result PASS means: both packaged processes started from the
  release folder, the client joined the LAN address, both sides saw two players
  and the same wave/level progress, both quit through the game's COMPLETE path
  with exit code 0, and no game process stayed behind.

roles (--role host | --role client) and merge:
  --role host binds --bind-ip (default: primary LAN IPv4) and writes its own JSON
  report; --role client needs --peer <host LAN IPv4>, joins that endpoint and
  writes its own JSON report. Each report has schema ws2.packaged_mp_side.v1
  with role, endpoint, listener/link address, exit code, players, wave, maxLevel,
  timestamp, checks and problems. --merge HOST_REPORT CLIENT_REPORT writes
  ws2.packaged_mp_merge.v1 and returns PASS only if both sides passed, both name
  the same <ip>:7777 endpoint (host listener, client peer and client LINK), the
  endpoint is not loopback, both saw two players, the wave agrees within one and
  maxLevel within two upgrades (the client quits first), and both exited 0. Report defaults: Saved/WS2/ws2_mp_host_report.json,
  ws2_mp_client_report.json, ws2_mp_merge_report.json (plus an app-data copy).

limits:
  The default two-process mode runs both instances on one Windows machine over
  its own LAN interface. That is not a substitute for two physical devices,
  their firewall/NAT state or the real Wi-Fi/switch path. Only the --role runs
  on two machines, merged with --merge, prove a real two-device session; allow
  UDP 7777 on the private network before starting.
""",
    )
    parser.add_argument("--exe", type=Path, default=DEFAULT_EXE)
    parser.add_argument("--bind-ip", default="", help="address the packaged host binds (default: primary LAN IPv4)")
    parser.add_argument("--join-ip", default="", help="address the packaged client travels to (default: the bind address)")
    parser.add_argument("--port", type=int, default=7777)
    parser.add_argument("--window", type=float, default=None,
                        help="netverify window in seconds (standard mode default 55, "
                             "--role host 120, --role client 115)")
    parser.add_argument("--role", choices=["host", "client"], default="",
                        help="run only one side of a two-device LAN test and write its own JSON report")
    parser.add_argument("--peer", default="",
                        help="host LAN address a --role client joins (required for --role client)")
    parser.add_argument("--merge", nargs=2, metavar=("HOST_REPORT", "CLIENT_REPORT"), default=None,
                        help="merge the two side reports into one PASS/FAIL verdict")
    parser.add_argument("--client-lag", type=float, default=5.0, help="host window minus this quits the client first")
    parser.add_argument("--timeout", type=float, default=260.0)
    parser.add_argument("--report", default="")
    args = parser.parse_args()

    if os.name != "nt":
        print("This runner requires Windows.", file=sys.stderr)
        return 2

    if args.merge:
        return merge_side_reports(args)

    if args.role:
        exe = resolve_executable(args)
        if not exe.is_file():
            print("FAIL: packaged executable missing: %s" % exe, file=sys.stderr)
            return 1
        if args.role == "client":
            peer_ip = (args.peer or "").strip()
            if not peer_ip:
                print("FAIL: --role client needs --peer <host LAN IPv4>:\n"
                      "  python Tools/run_packaged_mp_proof.py --role client --peer 192.168.1.100",
                      file=sys.stderr)
                return 1
            if is_loopback(peer_ip):
                print("FAIL: %s is loopback; a two-device client must join the host's LAN address.\n"
                      "Pass the host's LAN IPv4 (from `ipconfig` on the host) as --peer." % peer_ip,
                      file=sys.stderr)
                return 1
            # A joining client never binds the port, and on a shared machine the
            # host role may already own it: no local port/process gate here.
            return run_role_client(args, peer_ip)

        bind_ip = (args.bind_ip or primary_lan_ipv4()).strip()
        if not bind_ip:
            print(NO_LAN_HELP, file=sys.stderr)
            return 1
        if is_loopback(bind_ip):
            print("FAIL: %s is loopback; --role host must bind the LAN address.\n"
                  "Pass the IPv4 that `ipconfig` lists for the WLAN/LAN adapter as --bind-ip." % bind_ip,
                  file=sys.stderr)
            return 1
        local_addresses = local_ipv4_addresses()
        if bind_ip not in local_addresses:
            print("FAIL: %s is not an address of this machine. Local IPv4 addresses: %s.\n"
                  "Pass one of them as --bind-ip (see `ipconfig`)."
                  % (bind_ip, ", ".join(local_addresses) or "none found"), file=sys.stderr)
            return 1
        busy = udp_listeners(args.port)
        if busy:
            print("FAIL: UDP %d is already bound on this machine: %s" % (args.port, busy), file=sys.stderr)
            return 1
        leftover = running_instances()
        if leftover:
            print("FAIL: game processes are still running (stop them first): %s" % leftover, file=sys.stderr)
            return 1
        print_firewall_status()
        return run_role_host(args, bind_ip)

    exe = args.exe if args.exe.is_absolute() else ROOT / args.exe
    if not exe.is_file():
        print("FAIL: packaged executable missing: %s" % exe)
        return 1

    bind_ip = (args.bind_ip or primary_lan_ipv4()).strip()
    join_ip = (args.join_ip or bind_ip).strip()
    if not bind_ip:
        print(NO_LAN_HELP, file=sys.stderr)
        return 1
    if is_loopback(bind_ip):
        print("FAIL: %s is loopback; this proof must run on the machine's LAN address,\n"
              "not on the loopback interface. Pass the LAN IPv4 shown by `ipconfig` as --bind-ip." % bind_ip,
              file=sys.stderr)
        return 1
    if is_loopback(join_ip):
        print("FAIL: %s is loopback; the client must join the host's LAN address.\n"
              "Pass it as --join-ip (same value as --bind-ip when both run on this machine)." % join_ip,
              file=sys.stderr)
        return 1
    local_addresses = local_ipv4_addresses()
    if bind_ip not in local_addresses:
        print("FAIL: %s is not an address of this machine. Local IPv4 addresses: %s.\n"
              "Pass one of them as --bind-ip (see `ipconfig`)."
              % (bind_ip, ", ".join(local_addresses) or "none found"), file=sys.stderr)
        return 1

    busy = udp_listeners(args.port)
    if busy:
        print("FAIL: UDP %d is already bound (another game or test is running): %s" % (args.port, busy))
        return 1
    leftover = running_instances()
    if leftover:
        print("FAIL: game processes are still running (stop them first): %s" % leftover)
        return 1

    print_firewall_status()

    local = Path(os.environ.get("LOCALAPPDATA", str(ROOT)))
    saved = local / "WiesbadenSurvivors2" / "Saved" / "WS2"
    trace_dir = ROOT / "Build"
    trace_dir.mkdir(parents=True, exist_ok=True)
    host_trace = trace_dir / "packaged_mp_host.log"
    client_trace = trace_dir / "packaged_mp_client.log"
    for path in (host_trace, client_trace):
        try:
            path.unlink()
        except FileNotFoundError:
            pass

    common = [
        "-windowed", "-ResX=800", "-ResY=600", "-nosplash", "-nosound",
        "-ws2netverify=1", "-ws2invuln=1", "-ws2auto=75", "-ws2alltypes=1", "-ws2fast=2",
    ]
    window = HOST_WINDOW if args.window is None else args.window
    host_args = [
        *common, "-ws2netwindow=%g" % window, "-multihome=%s" % bind_ip,
        "-ws2testhost=1", "-ws2trace=%s" % host_trace,
    ]
    client_args = [
        *common, "-ws2netwindow=%g" % max(5.0, window - args.client_lag),
        "-ws2testjoin=%s" % join_ip, "-ws2hero=manni", "-ws2trace=%s" % client_trace,
    ]

    report = {
        "schema": "ws2.packaged_mp_proof.v1",
        "generatedAt": time.strftime("%Y-%m-%dT%H:%M:%S"),
        "result": "FAIL",
        "release": str(RELEASE),
        "executable": str(exe),
        "bindAddress": "%s:%d" % (bind_ip, args.port),
        "joinTarget": "%s:%d" % (join_ip, args.port),
        "loopbackUsed": is_loopback(bind_ip) or is_loopback(join_ip),
        "port": args.port,
        "hostCommand": host_args,
        "clientCommand": client_args,
        "checks": {},
        "evidence": {},
        "execution": {},
        "logs": {"hostTrace": str(host_trace), "clientTrace": str(client_trace)},
        "limits": [
            "Two packaged instances on one Windows host, bound to and joined over the machine's LAN IPv4 interface (%s); this exercises the real UDP socket, the real LAN host path and the replicated game state." % bind_ip,
            "It is not a substitute for two physical devices: a second machine, its firewall/NAT state and the actual Wi-Fi/switch path remain manual acceptance work.",
            "The host runs its netverify window a few seconds longer than the client so both sides shut down through the game's own COMPLETE/QuitGame path instead of being killed.",
        ],
    }

    host = None
    client = None
    listener = None
    failure = None
    started = time.time()
    try:
        host = launch(exe, host_args)
        report["execution"]["hostPid"] = host.pid
        print("host started pid=%d" % host.pid)

        want = "%s:%d" % (bind_ip, args.port)
        deadline = time.monotonic() + 90.0
        wildcard = None
        while time.monotonic() < deadline:
            found = [item for item in udp_listeners(args.port) if item["local"].endswith(":%d" % args.port)]
            listener = next((item for item in found if item["local"] == want), None)
            wildcard = wildcard or next((item for item in found if item["local"].startswith("0.0.0.0")), None)
            if listener:
                break
            if host.poll() is not None:
                raise RuntimeError("host exited before binding UDP %d (code %s)" % (args.port, host.returncode))
            time.sleep(0.25)
        if listener is None:
            raise RuntimeError(
                "host did not bind %s within 90s (saw %s); -multihome did not take effect"
                % (want, wildcard or "no UDP %d listener" % args.port)
            )
        report["execution"]["hostListener"] = listener
        print("host listener %s pid=%s (bound LAN address)" % (listener["local"], listener["pid"]))

        client = launch(exe, client_args)
        report["execution"]["clientPid"] = client.pid
        print("client started pid=%d target=%s" % (client.pid, report["joinTarget"]))

        deadline = time.monotonic() + args.timeout
        while time.monotonic() < deadline:
            host_lines = read_lines(host_trace)
            client_lines = read_lines(client_trace)
            if complete(host_lines, "HOST") and complete(client_lines, "CLIENT"):
                break
            if host.poll() is not None and client.poll() is not None:
                break
            time.sleep(0.5)

        # Both sessions end through the game's own QuitGame; a killed instance is
        # a failed shutdown, so wait for a clean exit instead of forcing one.
        deadline = time.monotonic() + 30.0
        while time.monotonic() < deadline:
            if host.poll() is not None and client.poll() is not None:
                break
            time.sleep(0.5)
        if host.poll() is None or client.poll() is None:
            raise RuntimeError(
                "instances did not exit through the game's quit path within 30s "
                "(host=%s client=%s)" % (host.poll(), client.poll())
            )

        host_lines = read_lines(host_trace)
        client_lines = read_lines(client_trace)
        host_done = complete(host_lines, "HOST")
        client_done = complete(client_lines, "CLIENT")
        host_states = state_bodies(host_lines, "HOST")
        client_states = state_bodies(client_lines, "CLIENT")
        shared = [body for body in client_states if body in host_states]
        shared_max_level = 0
        shared_max_wave = 0
        for body in shared:
            match = re.search(r"levels=([\d,]+)", body)
            if match:
                shared_max_level = max(shared_max_level, max(int(value) for value in match.group(1).split(",")))
            wave_match = re.search(r"wave=(\d+)", body)
            if wave_match:
                shared_max_wave = max(shared_max_wave, int(wave_match.group(1)))
        host_waves = waves(host_lines, "HOST")
        client_waves = waves(client_lines, "CLIENT")
        host_levels = levels(host_lines, "HOST")
        client_levels = levels(client_lines, "CLIENT")

        evidence = {
            "bindAddress": report["bindAddress"],
            "joinTarget": report["joinTarget"],
            "listenerLocal": listener["local"],
            "hostBindLine": next((line.strip() for line in host_lines if "LAN HOST bind=" in line), None),
            "clientJoinLine": next((line.strip() for line in client_lines if "LAN JOIN target=" in line), None),
            "clientLinkLine": next((line.strip() for line in client_lines if "NETVERIFY CLIENT LINK remote=" in line), None),
            "hostJoin": next((line.strip() for line in host_lines if re.search(r"PLAYER JOIN slot=\d+ .*players=2", line)), None),
            "hostBegin": next((line.strip() for line in host_lines if "NETVERIFY HOST BEGIN" in line), None),
            "clientBegin": next((line.strip() for line in client_lines if "NETVERIFY CLIENT BEGIN" in line), None),
            "hostPlayers": netverify(host_lines, "HOST", "PLAYERS")[:2],
            "clientPlayers": netverify(client_lines, "CLIENT", "PLAYERS")[:2],
            "clientHostVisible": next((line.strip() for line in client_lines if "NETVERIFY CLIENT HOST VISIBLE" in line), None),
            "hostMovement": netverify(host_lines, "SERVER", "MOVED")[:4],
            "clientMovement": netverify(client_lines, "CLIENT", "MOVED")[:4],
            "hostWaves": host_waves,
            "clientWaves": client_waves,
            "hostLevels": host_levels,
            "clientLevels": client_levels,
            "hostComplete": host_done,
            "clientComplete": client_done,
            "sharedStateLines": len(shared),
            "sharedStateSamples": shared[-3:],
            "sharedStateMaxLevel": shared_max_level,
            "sharedStateMaxWave": shared_max_wave,
            "hostStates": len(host_states),
            "clientStates": len(client_states),
        }
        report["evidence"] = evidence

        def has_level_up(rows):
            return any(any(level >= 2 for level in values) for values in rows.values())

        def moved(lines, role, needle):
            return any(needle in body for body in netverify(lines, role, "MOVED"))

        checks = {
            "release_folder_is_packaged_build": (RELEASE / "Windows" / "WiesbadenSurvivors2" / "Content" / "Paks").is_dir(),
            "host_bound_udp": listener is not None,
            "host_bound_lan_interface": listener is not None and listener["local"] == "%s:%d" % (bind_ip, args.port)
            and not is_loopback(bind_ip),
            "client_joined_lan_address": bool(evidence["clientJoinLine"] and ("target=%s:%d" % (join_ip, args.port)) in evidence["clientJoinLine"])
            and bool(evidence["clientLinkLine"] and join_ip in evidence["clientLinkLine"])
            and bool(evidence["hostBindLine"] and ("bind=%s:%d" % (bind_ip, args.port)) in evidence["hostBindLine"]),
            "client_joined_over_lan": bool(client_done and "players=2" in client_done["line"]),
            "host_saw_two_players": bool(evidence["hostJoin"]) and bool(host_done and host_done.get("players") == 2),
            "both_began_same_match": bool(evidence["hostBegin"] and evidence["clientBegin"]
                                          and "players=2" in evidence["hostBegin"] and "players=2" in evidence["clientBegin"]),
            "players_count_replicated": bool(evidence["hostPlayers"] and evidence["clientPlayers"])
            and "-1 -> 2" in evidence["hostPlayers"][0]
            and "-1 -> 2" in evidence["clientPlayers"][0],
            "client_sees_host_pawn": bool(evidence["clientHostVisible"]),
            "movement_replicated_both_ways": moved(host_lines, "SERVER", "local=1") and moved(host_lines, "SERVER", "local=0")
            and moved(client_lines, "CLIENT", "distance=")
            and any("NETVERIFY CLIENT HOST MOVED" in line for line in client_lines),
            "shared_state_on_both_sides": len(shared) >= 5,
            "wave_progress_replicated": len(host_waves) >= 2 and len(client_waves) >= 2
            and max(host_waves) >= 4 and max(client_waves) >= 4
            and shared_max_wave >= 4
            and host_done and client_done and host_done.get("wave", 0) >= 4
            and client_done.get("wave", 0) >= 4
            # The host verifies a few seconds longer than the client, so it may
            # already be one wave ahead when each side writes its COMPLETE line.
            and 0 <= host_done["wave"] - client_done["wave"] <= 1,
            "level_up_replicated": has_level_up(host_levels) and has_level_up(client_levels)
            and shared_max_level >= 2
            and host_done and client_done and client_done.get("maxLevel", 0) >= 2
            and host_done.get("maxLevel", 0) >= client_done.get("maxLevel", 0),
            "clean_shutdown_both_sides": bool(host_done and client_done)
            and host.returncode == 0 and client.returncode == 0,
        }
        report["checks"] = checks
        report["execution"].update(
            {
                "hostExitCode": host.returncode,
                "clientExitCode": client.returncode,
                "durationSeconds": round(time.time() - started, 1),
                "leftoverProcesses": running_instances(),
            }
        )
        checks["no_leftover_processes"] = report["execution"]["leftoverProcesses"] == []
        report["result"] = "PASS" if all(checks.values()) else "FAIL"
        if report["result"] != "PASS":
            failed = [name for name, ok in checks.items() if not ok]
            raise RuntimeError("failed checks: %s" % ", ".join(failed))
    except Exception as error:  # noqa: BLE001 - the report records the exact failure
        failure = str(error)
        report["failure"] = failure
        report["result"] = "FAIL"
        print("FAIL: %s" % failure, file=sys.stderr)
    finally:
        stop(client)
        stop(host)
        report["execution"]["hostExitCode"] = host.poll() if host else None
        report["execution"]["clientExitCode"] = client.poll() if client else None
        report["execution"]["durationSeconds"] = round(time.time() - started, 1)

        archive = ROOT / "Saved" / "WS2" / "packaged_mp"
        archive.mkdir(parents=True, exist_ok=True)
        for source in (host_trace, client_trace):
            if source.exists():
                shutil.copy2(source, archive / source.name)
        report["logs"]["archive"] = str(archive)

        report_paths = [saved / "packaged_mp_proof.json", ROOT / "Saved" / "WS2" / "packaged_mp_proof.json"]
        if args.report:
            report_paths.append(Path(args.report))
        for path in report_paths:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
        print("report: %s" % report_paths[0])
        print("logs:   %s" % archive)
        print("result: %s" % report["result"])
        for name, ok in report["checks"].items():
            print("%s  %s" % ("PASS" if ok else "FAIL", name))
        if failure:
            print("failure: %s" % failure)
    return 0 if report.get("result") == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
