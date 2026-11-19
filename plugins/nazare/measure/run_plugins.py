"""
Measures console plugins (Slate VCC, Waves NLS, or anything else) from the
outside: it loads them with Spotify's pedalboard, plays the test signal
through every console setting and drive step, and writes the measurements.

    pip install pedalboard numpy
    python run_plugins.py --scan                 # what is installed
    python run_plugins.py --params "<file>" [--name "<plugin in a shell>"]
    python run_plugins.py --all                  # measure everything it found
    python run_plugins.py --plugin "<file>" [--name ...] [--drives 0,0.25,0.5,0.75,1]

Results land in results/<plugin>/ as one JSON per setting plus summary.csv.
Nothing is decompiled or copied: only the audio that comes out is read.
"""
import argparse, csv, json, os, platform, re, sys, time
import numpy as np
import nzmeasure as nz

HERE = os.path.dirname(os.path.abspath(__file__))
WANT = re.compile(r"slate|vcc|virtual\s*(console|channel|mix|buss)|waveshell|waves|nls", re.I)
WANT_NAMES = re.compile(r"vcc|virtual|channel|buss|bus|nls|console", re.I)


def plugin_dirs():
    home = os.path.expanduser("~")
    if platform.system() == "Darwin":
        return ["/Library/Audio/Plug-Ins/VST3", f"{home}/Library/Audio/Plug-Ins/VST3",
                "/Library/Audio/Plug-Ins/Components", f"{home}/Library/Audio/Plug-Ins/Components"]
    if platform.system() == "Windows":
        return [r"C:\Program Files\Common Files\VST3", r"C:\Program Files (x86)\Common Files\VST3"]
    return [f"{home}/.vst3", "/usr/lib/vst3", "/usr/local/lib/vst3"]


def scan():
    from pedalboard import VST3Plugin
    try:
        from pedalboard import AudioUnitPlugin
    except ImportError:
        AudioUnitPlugin = None
    found = []
    for d in plugin_dirs():
        if not os.path.isdir(d): continue
        for root, dirs, files in os.walk(d):
            for n in list(dirs) + files:
                if not n.endswith((".vst3", ".component")) or not WANT.search(n): continue
                path = os.path.join(root, n)
                cls = VST3Plugin if n.endswith(".vst3") else AudioUnitPlugin
                if cls is None: continue
                try:
                    names = cls.get_plugin_names_for_file(path)
                except Exception as e:
                    names = [f"<cannot read: {e}>"]
                if "shell" in n.lower() or len(names) > 1:
                    names = [x for x in names if WANT_NAMES.search(x)]
                found.append({"file": path, "format": "VST3" if n.endswith(".vst3") else "AU", "plugins": names})
            dirs[:] = [x for x in dirs if not x.endswith((".vst3", ".component"))]
    return found


def load(path, name=None):
    from pedalboard import load_plugin
    return load_plugin(path, plugin_name=name) if name else load_plugin(path)


def describe(p):
    out = {}
    for key, par in p.parameters.items():
        d = {"name": getattr(par, "name", key), "units": getattr(par, "label", ""), "raw": float(par.raw_value)}
        for attr in ("min_value", "max_value", "default_raw_value", "num_steps"):
            if hasattr(par, attr):
                try: d[attr] = float(getattr(par, attr)) if attr != "num_steps" else int(getattr(par, attr))
                except Exception: pass
        vv = getattr(par, "valid_values", None)
        if vv: d["valid_values"] = [str(v) for v in vv][:64]
        try: d["value"] = str(getattr(p, key))
        except Exception: pass
        out[key] = d
    return out


def find(params, *patterns, exclude=None):
    for pat in patterns:
        for k, d in params.items():
            if re.search(pat, k, re.I) or re.search(pat, d["name"], re.I):
                if exclude and re.search(exclude, k, re.I): continue
                return k
    return None


def set_raw(p, key, raw):
    p.parameters[key].raw_value = float(min(1.0, max(0.0, raw)))


def plan(p, drives):
    """Every console the plugin offers, at each drive step. Noise off where it can be."""
    params = describe(p)
    console = find(params, r"^console$", r"console|desk|model|type|mixer", exclude=r"bypass|drive|group")
    drive = find(params, r"^drive$", r"drive|input|gain", exclude=r"output|trim|bypass")
    noise = find(params, r"noise")
    steps = []
    choices = params[console].get("valid_values") if console else None
    if console and choices:
        n = len(choices); cons = [(c, i / max(1, n - 1)) for i, c in enumerate(choices)]
    else:
        cons = [("default", None)]
    for cname, craw in cons:
        for dv in drives:
            steps.append({"console": cname, "console_raw": craw, "drive_raw": dv})
    return {"console_param": console, "drive_param": drive, "noise_param": noise, "steps": steps, "params": params}


def slug(s): return re.sub(r"[^A-Za-z0-9]+", "-", s).strip("-").lower() or "x"


def measure(path, name, drives, sig, man):
    p = load(path, name)
    label = name or os.path.splitext(os.path.basename(path))[0]
    outdir = os.path.join(HERE, "results", slug(label)); os.makedirs(outdir, exist_ok=True)
    pl = plan(p, drives)
    json.dump({"file": path, "name": label, "console_param": pl["console_param"], "drive_param": pl["drive_param"],
               "noise_param": pl["noise_param"], "parameters": pl["params"]}, open(os.path.join(outdir, "params.json"), "w"), indent=1)
    print(f"\n{label}: console={pl['console_param']} drive={pl['drive_param']} noise={pl['noise_param']} runs={len(pl['steps'])}", flush=True)
    rows = []
    for st in pl["steps"]:
        p = load(path, name)  # fresh instance per run, so no state leaks between settings
        if pl["console_param"] and st["console_raw"] is not None: set_raw(p, pl["console_param"], st["console_raw"])
        if pl["drive_param"] is not None: set_raw(p, pl["drive_param"], st["drive_raw"])
        settings = {k: d.get("value") for k, d in describe(p).items()}
        t = time.time()
        out = p.process(sig, nz.FS, buffer_size=512, reset=True)
        R = nz.analyse(np.asarray(out, np.float64), sig.astype(np.float64), man)
        R["settings"] = settings; R["step"] = st
        tag = f"{slug(st['console'])}__drive{int(round(st['drive_raw'] * 100)):03d}"
        json.dump(R, open(os.path.join(outdir, tag + ".json"), "w"))
        s = nz.summary(R); s.update(console=st["console"], drive_raw=st["drive_raw"],
                                   drive_value=settings.get(pl["drive_param"]) if pl["drive_param"] else None)
        rows.append(s)
        print(f"  {tag:40s} THD@-18 {s['thd_1k_nominal_pct']} %  @0dBFS {s['thd_1k_0dbfs_pct']} %  "
              f"noise {s['noise_dbfs']} dBFS  xtalk {s['xtalk_1k_db']} dB  ({time.time() - t:.1f}s)", flush=True)
    with open(os.path.join(outdir, "summary.csv"), "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys())); w.writeheader(); w.writerows(rows)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--scan", action="store_true")
    ap.add_argument("--params"); ap.add_argument("--plugin"); ap.add_argument("--name")
    ap.add_argument("--all", action="store_true")
    ap.add_argument("--drives", default="0,0.25,0.5,0.75,1")
    a = ap.parse_args()
    drives = [float(x) for x in a.drives.split(",")]
    if a.scan or a.all:
        found = scan()
        json.dump(found, open(os.path.join(HERE, "results", "scan.json") if os.path.isdir(os.path.join(HERE, "results")) else os.path.join(HERE, "scan.json"), "w"), indent=1)
        for f in found: print(f["format"], f["file"], "->", f["plugins"])
        if not a.all: return
    if a.params:
        print(json.dumps(describe(load(a.params, a.name)), indent=1)); return
    sig, man = nz.build_signal()
    os.makedirs(os.path.join(HERE, "results"), exist_ok=True)
    json.dump({"platform": platform.platform(), "python": sys.version, "manifest": man}, open(os.path.join(HERE, "results", "run.json"), "w"))
    targets = [(a.plugin, a.name)] if a.plugin else [(f["file"], n) for f in scan() for n in (f["plugins"] or [None])]
    for path, name in targets:
        try:
            measure(path, name if name and not name.startswith("<") else None, drives, sig, man)
        except Exception as e:
            print(f"!! {path} {name}: {e}", flush=True)


if __name__ == "__main__":
    main()
