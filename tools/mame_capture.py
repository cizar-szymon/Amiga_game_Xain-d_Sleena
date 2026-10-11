#!/usr/bin/env python3
"""Reference captures of Xain'd Sleena from MAME (screen PNG + memory/register state).

  python tools/mame_capture.py --out build/ref/demo --shots 100,600,2300 [--last 2400]
      [--script "1200:+coin1,1206:-coin1,1300:+start1,1306:-start1"]
      [--scenario play1] [--print-script] [--wav build/ref/demo.wav] [--trace maincpu:1000:1001 ...] [--set xsleena] [--show]

Frame N is the state after N complete frames (57.44 Hz). Each shot gives frame_NNNNN.png and
frame_NNNNN.bin (see read_snapshot). Script inputs: "+name" press, "-name" release at the end
of that frame; names: coin1 coin2 start1 start2 up down left right fire jump (player 1),
p2up p2down p2left p2right p2fire p2jump.
A trace writes every instruction of a CPU (maincpu, sub, audiocpu, mcu:mcu) between two frames
with its registers and total cycle count (T=) to trace_<cpu>.txt (several windows of one CPU:
trace_<cpu>_<from>.txt), and every data access to trace_<cpu>[_<from>]_mem.txt (needs the MAME
debugger: slower).
"""
import argparse
import os
import struct
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
MAME = os.environ.get("MAME", "C:/amiga-dev/mame/mame.exe")

INPUTS = {
    "coin1": (":P2", "Coin 1"), "coin2": (":P2", "Coin 2"),
    "start1": (":P1", "1 Player Start"), "start2": (":P1", "2 Players Start"),
    "up": (":P1", "P1 Up"), "down": (":P1", "P1 Down"),
    "left": (":P1", "P1 Left"), "right": (":P1", "P1 Right"),
    "fire": (":P1", "P1 Button 1"), "jump": (":P1", "P1 Button 2"),
    "p2up": (":P2", "P2 Up"), "p2down": (":P2", "P2 Down"),
    "p2left": (":P2", "P2 Left"), "p2right": (":P2", "P2 Right"),
    "p2fire": (":P2", "P2 Button 1"), "p2jump": (":P2", "P2 Button 2"),
}


def scenario_play1():
    """Coin, start, planet select (fire), then run right through the jungle firing and jumping."""
    ev = ["1200:+coin1", "1206:-coin1", "1300:+start1", "1306:-start1", "1700:+fire", "1706:-fire",
          "1800:+right"]
    ev += ["%d:+fire,%d:-fire" % (f, f + 4) for f in range(2000, 4401, 40)]
    ev += ["%d:+jump,%d:-jump" % (f, f + 6) for f in range(2020, 4401, 120)]
    ev.append("4500:-right")
    return ",".join(ev)


# standard reference runs: the PC port replays the same inputs (-script)
SCENARIOS = {
    "play1": scenario_play1,
}


def parse_frames(text):
    """"100,200,1000-1100/10" -> sorted list of frames."""
    frames = set()
    for part in filter(None, text.split(",")):
        if "-" in part:
            rng, _, step = part.partition("/")
            a, b = rng.split("-")
            frames.update(range(int(a), int(b) + 1, int(step or 1)))
        else:
            frames.add(int(part))
    return sorted(frames)


def parse_script(text):
    events = {}
    for part in filter(None, text.split(",")):
        frame, action = part.split(":")
        sign, name = action[0], action[1:]
        if sign not in "+-" or name not in INPUTS:
            sys.exit(f"bad script entry: {part}")
        port, field = INPUTS[name]
        events.setdefault(int(frame), []).append((port, field, 1 if sign == "+" else 0))
    return events


def lua_str(s):
    return '"' + s.replace("\\", "/").replace('"', '\\"') + '"'


def read_snapshot(path):
    """frame_NNNNN.bin -> {chunk name: bytes}."""
    data = open(path, "rb").read()
    if data[:4] != b"XSNP":
        raise ValueError(f"{path}: not a snapshot")
    chunks, pos = {}, 4
    while pos < len(data):
        name = data[pos:pos + 16].rstrip(b"\0").decode()
        size, = struct.unpack(">I", data[pos + 16:pos + 20])
        chunks[name] = data[pos + 20:pos + 20 + size]
        pos += 20 + size
    return chunks


def run_mame(args, out, shots, inputs, traces, last):
    lua = ["CFG = {}", "CFG.out = " + lua_str(out), "CFG.last = %d" % last,
           "CFG.shots = {" + ",".join("[%d]=true" % f for f in shots) + "}",
           "CFG.inputs = {"]
    for frame in sorted(inputs):
        lua.append("  [%d] = {%s}," % (frame, ",".join(
            "{%s,%s,%d}" % (lua_str(port), lua_str(field), v) for port, field, v in inputs[frame])))
    lua.append("}")
    lua.append("CFG.watch = {" + ",".join("{cpu=%s, addr=0x%s}" % (lua_str(":" + w.split(":")[0]), w.split(":")[1])
                                           for w in args.watch) + "}")
    lua.append("CFG.traces = {" + ",".join("{cpu=%s, from=%d, to=%d, file=%s}" % (
        lua_str(t[0]), t[1], t[2], lua_str(t[3])) for t in traces) + "}")
    # inlined, not dofile(): MAME runs the autoboot script in its own environment
    lua.append(open(os.path.join(HERE, "mame_capture.lua"), encoding="utf-8").read())
    script = out + "capture_run.lua"
    open(script, "w", encoding="utf-8").write("\n".join(lua) + "\n")

    cmd = [MAME, args.set, "-rompath", args.rompath, "-skip_gameinfo", "-nomouse",
           "-autoboot_script", script, "-snapshot_directory", out, "-nvram_directory", out + "nvram",
           "-cfg_directory", out + "cfg"]
    if not args.show:
        cmd += ["-video", "none", "-nothrottle", "-seconds_to_run", str(last // 50 + 30)]
        if not args.wav:
            cmd += ["-sound", "none"]
    else:
        cmd += ["-window"]
    if args.wav:
        cmd += ["-wavwrite", os.path.abspath(args.wav)]
    if traces:
        cmd += ["-debug", "-debugger", "none"]
    t0 = time.time()
    r = subprocess.run(cmd, cwd=os.path.dirname(MAME), capture_output=True, text=True)
    log = (r.stdout + r.stderr).strip()
    if log:
        print(log)
    print(f"MAME rc {r.returncode}, {time.time() - t0:.1f} s, {len(shots)} shots in {out}")
    return r.returncode


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--out", help="output directory")
    p.add_argument("--shots", default="", help="frames to dump, e.g. 100,200,1000-1100/10")
    p.add_argument("--last", type=int, help="last frame (default: last shot / trace end)")
    p.add_argument("--script", default="", help='inputs, e.g. "1200:+coin1,1206:-coin1"')
    p.add_argument("--scenario", choices=sorted(SCENARIOS), help="standard input script (added to --script)")
    p.add_argument("--print-script", action="store_true", help="print the input script and exit")
    p.add_argument("--wav", help="record the sound to this WAV file (MAME -wavwrite, 48 kHz)")
    p.add_argument("--trace", action="append", default=[],
                   help="cpu:from:to, e.g. maincpu:1000:1001 (repeatable; cpu: maincpu sub audiocpu mcu:mcu)")
    p.add_argument("--watch", action="append", default=[], help="cpu:ADDR (hex): log writes to watch.txt")
    p.add_argument("--set", default="xsleena", help="MAME set name")
    p.add_argument("--rompath", default=os.path.join(os.path.dirname(MAME), "roms"))
    p.add_argument("--show", action="store_true", help="show the MAME window (normal speed)")
    args = p.parse_args()

    if args.scenario:
        args.script = ",".join(filter(None, [SCENARIOS[args.scenario](), args.script]))
    if args.print_script:
        print(args.script)
        return 0
    out = os.path.abspath(args.out).replace("\\", "/") + "/"
    os.makedirs(out, exist_ok=True)
    shots = parse_frames(args.shots)
    inputs = parse_script(args.script)
    traces = []
    for spec in args.trace:
        cpu, a, b = spec.rsplit(":", 2)
        traces.append((":" + cpu, int(a), int(b)))
    multi = {t[0] for t in traces if sum(1 for u in traces if u[0] == t[0]) > 1}
    traces = [(cpu, a, b, out + "trace_" + cpu[1:].replace(":", "_") + ("_%05d" % a if cpu in multi else "") + ".txt")
              for cpu, a, b in traces]
    last = args.last or max(shots + [t[2] for t in traces] + list(inputs) + [1])

    # one traced CPU per MAME run (several windows of the same CPU share a run)
    cpus = []
    for t in traces:
        if t[0] not in cpus:
            cpus.append(t[0])
    runs = [(shots if i == 0 else [], [t for t in traces if t[0] == cpu]) for i, cpu in enumerate(cpus)] or [(shots, [])]
    rc = 0
    for run_shots, run_traces in runs:
        rc |= run_mame(args, out, run_shots, inputs, run_traces, last)
    missing = [f for f in shots if not os.path.exists(out + "frame_%05d.bin" % f)]
    if missing:
        sys.exit(f"missing shots: {missing[:10]}")
    return rc


if __name__ == "__main__":
    sys.exit(main())
