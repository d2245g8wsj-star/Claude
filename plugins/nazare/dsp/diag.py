"""Target against model, per metric. python diag.py <key>"""
import sys, json, fit
P = json.load(open("models.json"))[sys.argv[1]]; T = fit.targets(sys.argv[1])
print(json.dumps({k: P[k] for k in ("shaper", "pre", "gain_db", "bump")}))
for d in fit.DRIVES:
    row = []
    for f in fit.FREQS:
        for L in fit.LEVELS:
            m = fit.metrics(P, f, L, d); row.append(f"{int(f)}@{L}:{m['thd']:.0f}/{T[d][f'thd_{int(f)}_{L}']:.0f}")
    g = fit.metrics(P, 1000, -18, d)["gain"]
    print(f"d{int(d):2d} gain {g:+.2f}/{T[d]['gain_1k_m18']:+.2f}  " + "  ".join(row))
m = fit.metrics(P, 1000, -6, 6.0)
print("H2-5 @-6:", [round(x, 1) for x in m["h"][:4]], "target", [T[6.0][f"H{i}_1k_m6"] for i in (2, 3, 4, 5)])
print("+3: thd", round(fit.metrics(P, 1000, 3, 6.0)["thd"], 1), "/", T["thd_p3"], " gain0", round(fit.metrics(P, 1000, 0, 6.0)["gain"], 2), "/", T[6.0]["gain_1k_0"], " gainp3", round(fit.metrics(P, 1000, 3, 6.0)["gain"], 2), "/", T[6.0]["gain_1k_p3"])
