#!/bin/bash
# After every Amiga build: install the game under the desktop icon on the A1200 and check that it
# starts from the icon (WBStart = a double click: Workbench start-up message, the icon's tool
# types). Does nothing while the game runs (the user may be playing).
#
#   tools/amiga_install.sh            (from the repository root, after make -f Makefile.amiga)
#
# The desktop game is Work:XainSleena/XainSleena_Fast (left out in Work:.backdrop, its icon with
# the user's tool types stays); NAMES below are all names under which it is installed.
cd "$(dirname "$0")/.." || exit 1
R="python tools/amiga_remote.py"
NAMES="XainSleena_Fast ${XAIN_EXTRA_NAMES:-}"

if $R titles | grep -q "Xain'd Sleena"; then
    echo "amiga_install: the game is running on the Amiga - nothing installed"
    exit 2
fi
$R put build/amiga/WBStart Work:XainSleena_dev/WBStart >/dev/null || exit 1
for n in $NAMES; do
    $R put build/amiga/XainSleena "Work:XainSleena/$n" >/dev/null || exit 1
    if ! $R run "List Work:XainSleena/$n.info QUICK" >/dev/null 2>&1; then
        echo "amiga_install: Work:XainSleena/$n.info missing (icon)"; exit 1
    fi
    $R run "Version Work:XainSleena/$n" | head -1
done
first=${NAMES%% *}
$R run "Work:XainSleena_dev/WBStart Work:XainSleena/$first" | head -1
ok=0
for i in 1 2 3 4 5 6; do
    sleep 2
    if $R titles | grep -q "Xain'd Sleena"; then ok=1; break; fi
done
if [ $ok = 1 ]; then
    sleep 4
    $R titles | grep "Xain'd Sleena"
    $R key esc --window "Xain" >/dev/null
    sleep 3
    echo "amiga_install: started from the icon OK (closed again)"
else
    echo "amiga_install: NOT started from the icon - no game screen after 12 s"
    exit 3
fi
