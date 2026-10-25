"""Summary of ScrollLog CSV files (port/tests/scrolllog.c): per replay, the frames where the
bottom layer scrolled and the ones where it did not, with the mean block counts of each layer
above it and the step of the other background.

    python tools/scrolllog_summary.py build/scrolllog/*.csv
"""
import csv
import sys


def mean(rows, key):
    return sum(float(r[key]) for r in rows) / len(rows) if rows else 0.0


def main():
    print("%-38s %6s %6s %7s %6s %6s %6s %6s %6s %8s" % (
        "replay / frames", "count", "ovl", "edge", "txt", "bg1", "spr", "same", "recol", "pal_fr") + "  moved  unmoved")
    for path in sys.argv[1:]:
        rows = list(csv.DictReader(open(path)))
        name = path.replace("\\", "/").split("/")[-1].replace("build_lag_", "").replace(".bin.csv", "")
        for label, sel in (("scrolled", lambda r: r["dx"] != "0" or r["dy"] != "0"),
                           ("still", lambda r: r["dx"] == "0" and r["dy"] == "0")):
            part = [r for r in rows if sel(r)]
            if not part:
                continue
            same = sum(1 for r in part if r["other_dx"] == r["dx"] and r["other_dy"] == r["dy"])
            pal = sum(1 for r in part if r["pal_changed"] != "0")
            print("%-38s %6d %6.0f %7.1f %6.0f %6.0f %6.0f %5.0f%% %6.1f %7.0f%%" % (
                name + " " + label, len(part), mean(part, "overlay_blocks"), mean(part, "edge_blocks"),
                mean(part, "txt_blocks"), mean(part, "otherbg_blocks"), mean(part, "spr_blocks"),
                100.0 * same / len(part), mean(part, "pal_bottom_blocks"), 100.0 * pal / len(part))
                  + "  %5.0f  %7.0f" % (mean(part, "shift_blocks"), mean(part, "still_blocks")))


main()
