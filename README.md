# Wiesbaden Survivors 2.0 (v0.2.0)

Top-down arena roguelite built in Unreal Engine 5.8. Seven Wiesbaden-inspired arenas, 28 enemy archetypes, eight three-phase bosses, 40 weapons with 160 tiers, 44 items and a wave/shop loop. A normal run is **20 waves** and ends with the arena's boss phase table.

## Windows build (v0.2.0)

The local release artifact is `Dist/WiesbadenSurvivors2-0.2.0-LevelUpProof.zip` (SHA-256 `9BAB987A5074F6E201C29880CC36AA03E6F1F13BC8E29E13B538275BE4C9A8B9`). Extract it and run `WiesbadenSurvivors2.exe` from the extracted folder. No editor or UE installation is required. This hash is for the final rebuilt package; rebuilds change it.

To rebuild the Shipping game with UE 5.8:

```text
"C:/Program Files/Epic Games/UE_5.8/Engine/Build/BatchFiles/Build.bat" WiesbadenSurvivors2 Win64 Shipping -Project="<absolute path>/WiesbadenSurvivors2.uproject"
```

For development, open `WiesbadenSurvivors2.uproject` in the UE 5.8 editor or build the `WiesbadenSurvivors2Editor` target. The package is produced from the Shipping build with `RunUAT BuildCookRun` (cook, stage, pak, archive). Do not package the Development editor build.

## LAN co-op

The start screen has three ways into the arena:

- **SOLO STARTEN** — starts a local run with the selected hero and arena.
- **LAN HOSTEN** — starts a listen server on **port 7777** with the selected hero and arena. The lobby shows the host's local IPv4 address; share that address with players on the same WLAN/LAN.
- **IP BEITRETEN** — enter the host address (`192.168.x.x` or `192.168.x.x:7777`) and connect. Each joiner picks a hero and can join a run that is already in progress.

On first launch Windows may ask for firewall access; allow it on private networks so other LAN players can reach port 7777. The host and joiners must be on the same network; this is direct-IP LAN co-op, not internet matchmaking.

The server owns the simulation: movement, enemies, projectiles, pickups, damage, shop purchases, wave state and boss phases. Clients send movement input and receive compact snapshots of the horde plus a replicated run state. Multiple players can share a run; each player has a hero slot assigned by the host/server, and a joiner's selected hero is applied over the network after connecting.

### Controls

- **WASD / arrow keys / left stick**: move
- Auto-aim and auto-fire handle combat while enemies are in range
- **Space**: fire manually (in addition to auto-fire)
- **1 / 2 / 3**: pick a shop item
- **R**: reroll the shop
- **F11**: toggle fullscreen
- **Esc**: pause (solo)

## Run systems

- **20-wave campaign** per arena, with the arena's boss encounters in the final waves. The short five-wave proof slice remains available for automated checks.
- **Six weapon slots**. Weapon offers fill an empty slot, upgrade a matching weapon, or replace the first slot when the loadout is full. All equipped weapons auto-fire independently on the server.
- **Persistent profile** (`Saved/SaveGames/WS2Profile.sav`): best wave, best kill count, runs and total kills are saved after a completed full run and shown in the HUD/lobby.
- **Procedural audio**: synthesized shot, hit, kill, pickup, level-up, wave, boss, down, shop and victory SFX. No imported audio assets are required.
- **LAN HUD**: party roster with per-player HP/down state, shared wave/shop/boss state and profile record.

## Source layout

- `Source/WS2Core`, `Source/WS2Gameplay`, `Source/WS2UI` — C++ game modules
- `Content/Data` — heroes, enemies, bosses, weapons, items, arenas, VAT metadata
- `Art/scripts` — headless Blender generators, LOD chains and turnarounds
- `Tools` — UE import, VAT/budget validation, LAN acceptance and firewall helpers

## Verification

### Builds

- `Build.bat WiesbadenSurvivors2Editor Win64 Development` succeeds.
- `Build.bat WiesbadenSurvivors2 Win64 Shipping` succeeds.
- Forced full-run smoke test completes waves 1–20 and logs `ARENA COMPLETE` / `SLICE RUN END`.

### LAN co-op: real input, not just replication

The deterministic two-process test mode uses the real lobby host/join paths, injects key events through the player controller, and writes a package-safe trace with `-ws2trace=`. The final ZIP was first verified against its SHA-256, then extracted into the **new empty folder** `Dist/Acceptance-AudioFinal`. Both processes were started only from that extracted copy, with the audio device enabled. The host was already running before the client joined, which also exercised late joining. Every trace line carries the wall-clock timestamp, process ID and package path; the header records the exact extracted executable path.

The host trace (`Build/audiofinal_host.log`, PID 36680) and client trace (`Build/audiofinal_client.log`, PID 31856) contain:

- `POSTLOGIN ... local=0 ... players=1`, `PLAYER JOIN slot=1 ... players=2` and `NETVERIFY SERVER START players=2`: the late joiner is added to the authoritative player list. This was a real bug in the first package run and is now fixed.
- `NETVERIFY HOST KEY A pressed move=1`, `NETVERIFY SERVER INPUT pawn=WS2PlayerPawn_2147481928 ... right=1.00`, and `NETVERIFY SERVER MOVED` for both the host pawn (`local=1`) and the client pawn (`local=0`). The client logs `NETVERIFY CLIENT MOVED`, `NETVERIFY CLIENT HOST VISIBLE`, `NETVERIFY CLIENT HOST MOVED`, and `NETVERIFY CLIENT LOCAL PROBE ... local=1` for its locally controlled pawn.
- Both `NETVERIFY HOST FIRE KEY pressed` and `NETVERIFY CLIENT FIRE KEY pressed`; the server logs two `NETVERIFY SERVER FIRE ... manual=1 ... hits=1` lines, one for `WS2PlayerPawn_2147481990` (host) and one for `WS2PlayerPawn_2147481928` (client). The server also logs `NETVERIFY SERVER DAMAGE` and `NETVERIFY SERVER KILL` for hits from those shots.
- A server-spawned test pickup is collected by the **client** pawn: `NETVERIFY SERVER PICKUP collected by=WS2PlayerPawn_2147481928 amount=7 materials=95`. The client material counter and pickup counter change as well.
- `NETVERIFY HOST WAVE 1 -> 2` and `NETVERIFY CLIENT WAVE 1 -> 2`; the host finishes with `NETVERIFY HOST COMPLETE wave=2`. No Error/Fatal line appeared in either package trace.

The exact package commands were (run from the extracted folder; a second copy of the extracted folder is not required):

```text
WiesbadenSurvivors2.exe -windowed -ws2netverify=1 -ws2invuln=1 -ws2testhost=1 -ws2trace=<absolute-host-trace.log>
WiesbadenSurvivors2.exe -windowed -ws2netverify=1 -ws2invuln=1 -ws2testjoin=127.0.0.1 -ws2hero=manni -ws2trace=<absolute-client-trace.log>
```

The menu itself is started without any arguments; the package screenshot with `VERSION 0.2.0` and **SOLO STARTEN / LAN HOSTEN / IP BEITRETEN / VERBINDEN** is `Build/audiofinal_menu2.png`.

For comparison, the editor-level deterministic test remains available with `UnrealEditor-Cmd.exe ... -game -ws2netverify=1`; the table above is specifically the packaged-artifact result.

### Automated LAN acceptance on this machine

The acceptance above used loopback. The reproducible same-machine LAN test is `python Tools/run_lan_acceptance.py`. It detects the active non-loopback IPv4 interface (currently `WLAN`, `192.168.1.100`), refuses loopback and an already-occupied UDP port, checks for blocking firewall rules, then starts two independent processes from the accepted Shipping package over the real interface. The host binds `0.0.0.0:7777`; the client is started only after the UDP listener exists and joins `192.168.1.100:7777` through the real `LAN HOSTEN` / `IP BEITRETEN` path.

The runner requires session join, both players' visibility, client movement on the host and host movement on the client, then writes `Saved/WS2/lan_acceptance_report.json` plus timestamped host/client traces under `Saved/WS2/lan_acceptance/`. It exits nonzero if any check fails. If Windows has explicit inbound block rules for the package executable, run `powershell -ExecutionPolicy Bypass -File Tools/fix_lan_firewall.ps1` from an elevated shell once; the runner itself does not require elevation.

### Solo pause

`-ws2pausetest=1` was run from the **same SHA-verified extracted package** and starts a solo run, measures the world and the player position independently of the paused simulation, then resumes. `Build/audiofinal_pause.log` contains `NETVERIFY PAUSE HOLD paused=1 locDelta=0.000 worldDelta=0.000`, `NETVERIFY PAUSE RESULT hold=PASS`, a resumed world advance and `NETVERIFY PAUSE RESULT resume=PASS`. The same run also contains real audio triggers while the audio device was open.

### Audio in a real run

The **SHA-verified extracted package** was run with the audio device enabled (no `-nosound`; windowed) in the two-process co-op test. `WS2Audio::Play` now writes an `AUDIO TRIGGER` line **after** handing the decoded procedural wave to the real audio device, with the event name, numeric type, engine frame, source pawn or world position, volume, device state and active-source count. The host trace `Build/audiofinal_host.log` contains e.g.:

```text
AUDIO TRIGGER sfx=shot type=0 frame=5995 source=WS2PlayerPawn_2147481990 ... channelOpen=1 audioDevice=1 soundEnabled=1 outputDevices=2 audioTime=14.805 activeSources=32 muted=0
AUDIO TRIGGER sfx=hit type=1 frame=7062 source=world ... channelOpen=1 audioDevice=1 ...
AUDIO TRIGGER sfx=kill type=2 frame=6739 source=world ... channelOpen=1 audioDevice=1 ...
AUDIO TRIGGER sfx=pickup type=3 frame=6309 source=world ... channelOpen=1 audioDevice=1 ...
AUDIO TRIGGER sfx=wave type=5 frame=7571 source=world ... channelOpen=1 audioDevice=1 ...
AUDIO TRIGGER sfx=levelup type=4 frame=3583 source=world ... channelOpen=1 audioDevice=1 ...
```

The client trace `Build/audiofinal_client.log` contains its own triggered `shot`, `hit`, `kill`, `pickup`, `wave` and `levelup` lines; a `shop` trigger also appears in the client's subsequent continuation. In both traces `channelOpen=1` is backed by a non-null audio device, `soundEnabled=1`, two enumerated output devices, a running audio clock (`audioTime > 0`) and active sources; the separate pause run also shows the same open-channel state. Boss, down and victory were not reached in this short co-op run; they use the same `Play` path but are not claimed as exercised here.

### Arena result proof: level-ups and the full shop pool

Every completed arena writes `arena_<arena>.json`: the editor writes it to `<project>/Saved/WS2/`, a packaged build to `%LOCALAPPDATA%/WiesbadenSurvivors2/Saved/WS2/` (a staged build maps its saved folder to the user's app data). Besides waves, kills, bosses and frame stats it carries two proof blocks:

- `levelUps` — `count`, `finalLevel`, `levelUpShops`, `shopOpens`, `pendingAtEnd` and one row per shop session with `wave`, `level`, `levelUp`, the presented `offers` (`weapon:<id>@T<n>`, `item:<id>`, `stat:<id>`) and the chosen `pick`.
- `shopPool` — the pool sizes (`weapons`, `tiers`, `items`, `stats`, `elements`), the expected sizes (`40/160/44/24/5`), `complete`, the counts that the offer generator can actually reach (`offerable`), `fullPoolOfferable` and what the run really offered (`offeredInRun`). `offerable` is measured by sweeping the same deterministic seed used by `BuildShopOffers()` (wave 1-32, level 0-48, reroll 0-12), not by trusting the data files.

A level-up reward spends its third slot on a stat, so weapons, tiers, items, stats and elements are all genuinely offered; previously the stat branch was unreachable because both item slots always filled the list. `-ws2noboss=1` now really skips the arena bosses (the flag was parsed but never used), which lets the 20-wave proof runs finish on the wave clock.

Reproduce (per arena, no shipping package needed):

```text
UnrealEditor.exe WiesbadenSurvivors2.uproject "L_Arena?autostart=1?arena=kurpark" -game -nullrhi -unattended -nosound -nosplash -ws2full=1 -ws2fast=100 -ws2auto=60 -ws2quit=120 -ws2noboss=1 -ws2shot=-1 -ws2invuln=1
```

All seven arenas were replayed this way; each report has `complete: true`, `fullPoolOfferable: true`, 7-19 level-ups and 7-9 level-up shops whose offers include a stat.

#### Reproducing the proof from the release package

The current release artifact is the archived build `Dist/Release-0.2.0-LevelUpProof` (built from this source tree with `RunUAT BuildCookRun -clientconfig=Shipping -cook -stage -pak -archive`; `SHA256SUMS.txt` in that folder lists **every** delivered payload file (all 30, including the engine DLLs, manifests and the `.pdb`; only the manifest itself cannot list itself), so `sha256sum -c SHA256SUMS.txt` covers the whole package) and the matching archive `Dist/WiesbadenSurvivors2-0.2.0-LevelUpProof.zip` (31 entries: the same 30 files and `SHA256SUMS.txt`, integrity tested). `Saved/StagedBuilds/Windows` and `Saved/Cooked/Windows` are build output, not evidence: `-stage` and `-cook` recreate them, so clearing them to free disk space is safe and the next `BuildCookRun` rebuilds the exact staged tree the archive is copied from. Start the *packaged* game, never the editor, from `Dist/Release-0.2.0-LevelUpProof/Windows`:

```text
python Tools/run_packaged_proof.py --arena kurpark
```

The runner starts `WiesbadenSurvivors2.exe "L_Arena?autostart=1?arena=kurpark" -ws2arena=kurpark -game -windowed -ResX=960 -ResY=540 -nosound -nosplash -ws2trace=<Build>/packaged_release_<arena>.log -ws2full=1 -ws2fast=100 -ws2auto=60 -ws2quit=120 -ws2noboss=1 -ws2shot=-1 -ws2invuln=1`, waits for the process, then locates and validates the result file. Three details differ from an editor run:

- The Shipping build has no NullRHI, so `-nullrhi` makes it exit immediately; the packaged run renders into a small window instead.
- The Shipping build writes no engine log file, so `-ws2trace=<path>` mirrors the WS2 log lines into `Build/packaged_release_<arena>.log`.
- The launcher drops the map URL's `arena` option (a request for `innenstadt` started `kurpark`), so `-ws2arena=<arena>` is passed explicitly.

The packaged run writes its result to `%LOCALAPPDATA%\WiesbadenSurvivors2\Saved\WS2\arena_<arena>.json`; `Build/packaged_release_<arena>.log` contains the `RUN START ... full=1 arena=<arena>` line of that same PID. The runner prints one PASS/FAIL line per required field, stores the checks in `Saved/WS2/packaged_proof_<arena>.json` and keeps a copy of the validated JSON in `Saved/WS2/packaged_release/arena_<arena>.json`. Packaged results so far: all seven arenas verified against `Dist/Release-0.2.0-LevelUpProof`, each `PASS` with 20/20 waves, `bossCount: 0` (so `-ws2noboss=1` is honoured in the package), `shopPool` 40/160/44/24/5, `complete: true`, `fullPoolOfferable: true`. Across the seven arenas: 8-9 level-ups, 7-9 level-up shops and 7-9 stat offers per run, 19 shop sessions each.

#### Multiplayer proof from the release package

The co-op proof above runs two independent processes out of `Dist/Release-0.2.0-LevelUpProof`: one packaged host that opens the real listen server and one packaged client that joins over a real UDP socket. Both use the machine's **primary LAN IPv4 address** by default (not loopback): the host binds that interface with `-multihome=<ip>` and the client travels to `<ip>:7777`. No editor, no `-server`, no special listen-only mode: both sides use `HostLan()` / `JoinLan()`, the same code the **LAN HOSTEN** and **IP BEITRETEN** buttons call.

One-liner (auto-detects the LAN IPv4, here `192.168.1.100`):

```text
python Tools/run_packaged_mp_proof.py
```

Explicit LAN invocation, including the addresses actually used:

```text
python Tools/run_packaged_mp_proof.py --bind-ip 192.168.1.100 --join-ip 192.168.1.100 --window 55
```

`--bind-ip` is the address the packaged host binds (default: first routable IPv4 of this machine), `--join-ip` is the address the packaged client travels to (default: the bind address). The runner refuses `127.0.0.1`/`localhost` for either value and refuses an address that is not present on this machine, so a loopback run cannot be mistaken for a LAN proof. If no routable IPv4 exists at all it exits with code 1 and prints the exact remedy (connect WLAN/LAN, read `IPv4-Adresse` from `ipconfig`, pass `--bind-ip <LAN-IPv4> --join-ip <LAN-IPv4>`, allow UDP 7777 in the private-network firewall) instead of silently falling back to loopback. `python Tools/run_packaged_mp_proof.py --help` repeats the default, the one-liner, the full command line, the output paths, the meaning of a PASS and the one-machine limit.

Host and client are started with the netverify flags instead of a script driving menus (the runner passes the same flags; the LAN host line shows the bind address it passed):

```text
WiesbadenSurvivors2.exe -windowed -ResX=800 -ResY=600 -nosplash -nosound -ws2netverify=1 -ws2invuln=1 -ws2auto=75 -ws2alltypes=1 -ws2fast=2 -ws2netwindow=55 -multihome=192.168.1.100 -ws2testhost=1 -ws2trace=<repo>/Build/packaged_mp_host.log
WiesbadenSurvivors2.exe -windowed -ResX=800 -ResY=600 -nosplash -nosound -ws2netverify=1 -ws2invuln=1 -ws2auto=75 -ws2alltypes=1 -ws2fast=2 -ws2netwindow=50 -ws2testjoin=192.168.1.100 -ws2hero=manni -ws2trace=<repo>/Build/packaged_mp_client.log
```

`-multihome=<ip>` makes the packaged listen server bind exactly that interface (verified with `netstat -ano -p UDP`: `192.168.1.100:7777`, not `0.0.0.0`), `-ws2testhost` hosts and joins the arena through the LAN path, `-ws2testjoin=<ip>` connects a second packaged process, `-ws2netwindow=<s>` is the verification window in seconds (host 55 s, client 50 s, so both leave through the game's own `COMPLETE` + `QuitGame` instead of being killed), and `-ws2trace=<file>` mirrors the WS2 log lines into a file because a Shipping build writes no engine log. `-ws2auto=75` only arms the proof slice; it is deliberately longer than both windows so the netverify teardown wins the race.

What the run writes:

- `%LOCALAPPDATA%\WiesbadenSurvivors2\Saved\WS2\packaged_mp_proof.json` — machine-readable report with `result`, the addresses actually used (`bindAddress`, `joinTarget`, `execution.hostListener`), one `PASS`/`FAIL` per check, the exact host/client command lines, exit codes, duration and the log paths. A copy is kept in the repository at `Saved/WS2/packaged_mp_proof.json`, the two traces are archived to `Saved/WS2/packaged_mp/`. Use `--report <file>` to keep a second copy per run (for example `Saved/WS2/lan_run1.json` and `lan_run2.json` for the two green repeat runs).
- `Build/packaged_mp_host.log` / `Build/packaged_mp_client.log` — the raw trace of both packaged instances. They name the address: `LAN HOST bind=192.168.1.100:7777 local=192.168.1.100:7777`, `LAN JOIN target=192.168.1.100:7777` and `NETVERIFY CLIENT LINK remote=192.168.1.100:7777`; a loopback run would show `127.0.0.1` there instead.

How to read the checks: `host_bound_udp` (a UDP listener exists), `host_bound_lan_interface` (`netstat` reports exactly `<bind-ip>:7777`, so `-multihome` took effect), `client_joined_lan_address` (host, client and the client's own `LINK remote=` all name the LAN address), `client_joined_over_lan` (the client's own `COMPLETE players=2`), `host_saw_two_players` (the host's `PLAYER JOIN ... players=2` and `COMPLETE players=2`), `both_began_same_match` (both `NETVERIFY <ROLE> BEGIN players=2 wave=1`), `players_count_replicated` (both sides log `PLAYERS -1 -> 2`), `client_sees_host_pawn`, `movement_replicated_both_ways` (the host reports `SERVER MOVED local=1` and `local=0`, the client reports `CLIENT MOVED distance=...` and `CLIENT HOST MOVED`), `shared_state_on_both_sides` (identical `STATE players=... wave=... levels=...` lines on both sides), `wave_progress_replicated`, `level_up_replicated` (level rises replicated per slot on both sides), `clean_shutdown_both_sides` (both processes exit 0 through `COMPLETE`) and `no_leftover_processes`. The lobby's own address detection (`LocalLanAddress()`) now walks the network adapters instead of trusting `GetLocalHostAddr()`, which reported the loopback adapter on this machine; the host line and the lobby log therefore show the real LAN address too.

The run also leaves `LOGOUT <controller> pawn=<0|1> players=<n>` lines: a client that quits can lose its pawn before the authority's logout callback, so the host now prunes that stale roster entry instead of walking a destroyed pawn (that crash used to end the host with exit code 3 right after the client left).

Packaged LAN results so far (both runs from the current package): two consecutive runs (`Saved/WS2/lan_run1.json`, `Saved/WS2/lan_run2.json`) both `PASS` with all 15 checks, exit code 0/0, `bindAddress` and `joinTarget` `192.168.1.100:7777`, host listener exactly `192.168.1.100:7777`, host `COMPLETE players=2 playersNow=1 wave=5 maxLevel=6` and client `COMPLETE players=2 playersNow=2 wave=5 maxLevel=6`, 22 identical `STATE players=... wave=... levels=...` lines, 61.8-61.9 s per run, `leftoverProcesses: []` and no crash when the client leaves. The arena proof from the same package reports `complete: true` (20/20 waves), `shopPool.complete`/`fullPoolOfferable` true, 9 level-up shops and 9 stat offers.

The limit is unchanged and important: this proves the packaged build, the real UDP socket and the replicated state on **one** Windows machine. It is not a substitute for two physical devices — the second machine, its firewall/NAT state and the actual Wi-Fi/switch path stay manual acceptance work (see *Remaining manual acceptance*).

#### Two devices over WLAN/LAN (split roles)

The runner also runs **one side only**, so each physical device needs a single command and writes its own machine-readable report. Both machines need this repository folder (or at least `Tools/run_packaged_mp_proof.py` plus the release folder) and Python 3.

```text
# machine A (host), packaged release
python Tools/run_packaged_mp_proof.py --role host --bind-ip 192.168.1.100

# machine B (client), packaged release
python Tools/run_packaged_mp_proof.py --role client --peer 192.168.1.100

# afterwards, on either machine (copy the other report over)
python Tools/run_packaged_mp_proof.py --merge host_report.json client_report.json
```

Full form: `python Tools/run_packaged_mp_proof.py --role host --bind-ip <LAN-IPv4> --port 7777 --window 120 --exe Dist/Release-0.2.0-LevelUpProof/Windows/WiesbadenSurvivors2-Win64-Shipping.exe --report <path>` (client: `--role client --peer <host-LAN-IPv4> --window 115`). Defaults: `--bind-ip` = primary LAN IPv4, `--port` 7777, host window 120 s, client window 115 s (the client leaves first so the host sees the logout), `--report` = `Saved/WS2/ws2_mp_host_report.json` / `ws2_mp_client_report.json`, each also copied to `%LOCALAPPDATA%\WiesbadenSurvivors2\Saved\WS2\`. Traces: `Build/packaged_mp_role_host.log`, `Build/packaged_mp_role_client.log`. Loopback and addresses that do not exist on the local machine are still rejected, with no silent fallback.

Each side report (`schema: ws2.packaged_mp_side.v1`) records `role`, `endpoint`, `bindAddress`/`peerAddress`, `listenerLocal` (host, exact `<ip>:7777` from `netstat`), `linkRemote` (client, its own `LINK remote=`), `exitCode`, `players`, `wave`, `maxLevel`, `durationSeconds`, `generatedAt`, per-check booleans and `problems`. The host report additionally carries `hostSawTwoPlayers`.

`--merge` writes `schema: ws2.packaged_mp_merge.v1` (default `Saved/WS2/ws2_mp_merge_report.json`) and returns exit code 0 / `PASS` only when **all** of these hold: `both_side_reports_pass`, `roles_are_host_and_client`, `same_endpoint` (host `endpoint` = host `listenerLocal` = client `peerAddress` = client `linkRemote`), `endpoint_is_not_loopback`, `both_saw_two_players` (`players=2` on both sides, host saw the join), `shared_wave_progress` (both ≥ wave 4, at most one wave apart), `shared_level_progress` (both ≥ level 2, at most two upgrades apart because the client quits first) and `clean_shutdown_both_sides` (exit 0/0). Anything else exits 1 and lists the failed check names.

Longer netverify windows used to be cut short by the proof slice: when the slice reaches its last wave it quits the game, which pre-empted `COMPLETE` for windows beyond ~75 s. The game now skips that slice-end quit while a netverify run is active, so the run always ends through its own `NETVERIFY <ROLE> COMPLETE` line; the two-process default mode (55 s window) is unchanged in behaviour.

The role split is verified on one machine by starting the host role and the client role one after the other (two commands, two reports) and merging those two real reports: the merge passes. The artifacts are `Saved/WS2/two_device_host_report.json`, `Saved/WS2/two_device_client_report.json` and `Saved/WS2/two_device_merge_report.json` — both sides `PASS`, endpoint `192.168.1.100:7777`, host `wave=5 maxLevel=6`, client `wave=5 maxLevel=6`, merge 8/8 checks `PASS`. Those two runs still happened on **one** machine (host role in the background, client role joining over the LAN IPv4), so they prove the role/merge tooling, not a second device. Negative merges were tested by changing the client report to `wave: 1`, to `peerAddress: 10.0.0.5:7777` and to `result: FAIL` — each exits 1 with the matching failed check. **A real two-device session has not been run yet**: until someone runs the two commands above on two machines, this release does not claim a two-device PASS. Allow the packaged game on the *private* network (UDP 7777) on the host, and expect a real Router/NAT and Wi-Fi path that the single-machine runs cannot model.

### Packaged artifact

This earlier acceptance package predates the release folder above. The final ZIP was built from the Shipping target, checked against SHA-256 `BC80957F3D4BE15BACFB0A4A5E13CA5647977B9C7B8C177FFD2B020E1160C103`, then extracted into the new empty folder `Dist/Acceptance-AudioFinal`. Its root `WiesbadenSurvivors2.exe` was started without arguments. `Build/audiofinal_menu2.png` shows `WIESBADEN SURVIVORS 2.0`, `VERSION 0.2.0`, the loaded hero/arena data, the LAN address field and **SOLO STARTEN**, **LAN HOSTEN**, **IP BEITRETEN** with **VERBINDEN**. The two-process co-op, pause and audio checks above were run from that same extracted package, not from the editor build.

### Remaining manual acceptance

The only unverified item is a real two-device WLAN/LAN session. The same-machine runner above exercises the real WLAN socket, but it cannot replace a second physical Windows device, its firewall/NAT state or the actual Wi-Fi/switch path; no such two-device session is claimed here. To perform it, either play it by hand — start a packaged game on device A, choose **LAN HOSTEN**, allow Windows Firewall access on the private network, then on device B choose **IP BEITRETEN**, enter A's IPv4 address (and `:7777` if needed), and verify both players can move, shoot, collect drops and see the wave change — or run the split-role commands above and merge the two reports, which produces a machine-readable PASS/FAIL for exactly that session. Internet matchmaking, NAT traversal and cross-platform play are not part of this release.

## Build requirements

- Unreal Engine 5.8
- Visual Studio 2022 with the Windows C++ toolchain
- Windows 10/11, DirectX 12 capable GPU

Generated `Binaries/`, `Intermediate/`, `Saved/`, `Build/` and `Dist/` directories are intentionally not committed.
