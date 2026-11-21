"""
The same measurements without pedalboard, for a DAW.

    python daw_route.py make                 # writes nazare_testsignal.wav (48 kHz, 32-bit float)
    python daw_route.py analyse export.wav   # reads a render of it and prints the summary

Put the test file on a stereo track at 48 kHz, insert the console plugin,
set the console and drive you want, and bounce the whole length with nothing
else on the track or master. Name each export after its setting, e.g.
vcc-channel_brit-n_drive50.wav, and put them in results/exports/.
"""
import json, os, sys
import numpy as np
import nzmeasure as nz

HERE = os.path.dirname(os.path.abspath(__file__))

if __name__ == "__main__":
    cmd = sys.argv[1] if len(sys.argv) > 1 else "make"
    sig, man = nz.build_signal()
    if cmd == "make":
        nz.write_wav(os.path.join(HERE, "nazare_testsignal.wav"), sig)
        json.dump(man, open(os.path.join(HERE, "nazare_testsignal.json"), "w"))
        print(f"nazare_testsignal.wav  {sig.shape[1] / nz.FS:.1f} s")
    else:
        for path in sys.argv[2:]:
            out, fs = nz.read_wav(path)
            assert fs == nz.FS, f"{path}: export at 48 kHz, not {fs}"
            n = min(out.shape[1], sig.shape[1] + 8192)
            pad = np.zeros((2, sig.shape[1] + 8192)); pad[:, :n] = out[:2, :n]
            R = nz.analyse(pad, np.pad(sig.astype(np.float64), ((0, 0), (0, 8192))), man)
            json.dump(R, open(os.path.splitext(path)[0] + ".json", "w"))
            print(os.path.basename(path), json.dumps(nz.summary(R)))
