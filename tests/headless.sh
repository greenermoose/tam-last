#!/bin/sh
# The whole path on a real compositor, without touching the desktop:
# tam-pane runs on wlroots' headless backend with a private
# XDG_RUNTIME_DIR, starts tam-last, and logs the window it maps. tam-last
# runs a command that prints and exits, so it must map its window, draw,
# and exit with the command's status.
#
#   tests/headless.sh TAM_LAST TAM_PANE
#
# Skipped (not failed) when the tam-pane build is missing.
set -u
last=$1
pane=$2

if [ ! -x "$pane" ]; then
	echo "headless: skipped (no tam-pane build at $pane)"
	exit 0
fi
case $last in /*) ;; *) last="$(pwd)/$last" ;; esac

dir=$(mktemp -d "${TMPDIR:-/tmp}/tam-last-headless.XXXXXX") || exit 1
chmod 700 "$dir"
cleanup() { rm -rf "$dir"; }
trap cleanup EXIT

# The status file proves tam-last ran the command and exited with its
# status, which tam-pane cannot report.
env -u WAYLAND_DISPLAY -u DISPLAY XDG_RUNTIME_DIR="$dir" \
	WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_HEADLESS_OUTPUTS=1 \
	"$pane" -d -s "$last -e sh -c 'printf hello; sleep 1; exit 3'; echo \$? > $dir/status" \
	>"$dir/log" 2>&1 &
pid=$!
i=0
while [ ! -s "$dir/status" ] && [ "$i" -lt 50 ]; do
	sleep 0.2
	i=$((i + 1))
done
kill -TERM "$pid" 2>/dev/null
wait "$pid"

fail=0
if grep -q 'window mapped: tam-last' "$dir/log"; then
	echo "headless: window mapped: ok"
else
	echo "headless: window mapped: FAILED"
	fail=1
fi
status=$(cat "$dir/status" 2>/dev/null)
if [ "$status" = 3 ]; then
	echo "headless: exit status of the command: ok"
else
	echo "headless: exit status of the command: FAILED (got '${status:-none}', want 3)"
	fail=1
fi
if [ "$fail" -ne 0 ]; then
	grep -E 'tam-last|ERROR|error' "$dir/log" | head -20 | sed 's/^/  | /'
fi
exit "$fail"
