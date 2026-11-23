#!/bin/sh
# The whole path on a real compositor, without touching the desktop:
# tam-pane runs on wlroots' headless backend with a private
# XDG_RUNTIME_DIR, starts tam-last (both standalone and --server/--client),
# and logs the windows it maps.
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

mkdir -p "$dir/sub"

# Run standalone + server/client inside one headless tam-pane session.
cat >"$dir/run.sh" <<EOF
#!/bin/sh
"$last" -e sh -c 'printf hello; sleep 1; exit 3'
echo \$? > "$dir/status_standalone"

"$last" --server --socket "$dir/srv.sock" &
srv_pid=\$!
i=0
while [ ! -S "$dir/srv.sock" ] && [ "\$i" -lt 50 ]; do
	sleep 0.05
	i=\$((i + 1))
done
if [ ! -S "$dir/srv.sock" ]; then
	echo "missing" > "$dir/status_server"
	kill -TERM "\$srv_pid" 2>/dev/null || true
	exit 1
fi

# Open two concurrent client windows on the single server process.
"$last" --client --socket "$dir/srv.sock" -e sh -c 'sleep 0.5; exit 0' &
c1=\$!
(cd "$dir/sub" && "$last" --client --socket "$dir/srv.sock" -e sh -c 'pwd > "$dir/client_pwd"; sleep 0.5; exit 7')
echo \$? > "$dir/status_client"
wait "\$c1" || true

kill -TERM "\$srv_pid"
wait "\$srv_pid"
echo \$? > "$dir/status_server"
EOF
chmod +x "$dir/run.sh"

env -u WAYLAND_DISPLAY -u DISPLAY XDG_RUNTIME_DIR="$dir" \
	WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_HEADLESS_OUTPUTS=1 \
	"$pane" -d -s "$dir/run.sh" \
	>"$dir/log" 2>&1 &
pid=$!
i=0
while [ ! -s "$dir/status_server" ] && [ "$i" -lt 80 ]; do
	sleep 0.2
	i=$((i + 1))
done
kill -TERM "$pid" 2>/dev/null
wait "$pid"

fail=0
mapped=$(grep -c 'window mapped: tam-last' "$dir/log" 2>/dev/null || echo 0)
if [ "$mapped" -ge 3 ]; then
	echo "headless: windows mapped ($mapped): ok"
else
	echo "headless: windows mapped: FAILED (got $mapped, want >= 3)"
	fail=1
fi
status=$(cat "$dir/status_standalone" 2>/dev/null)
if [ "$status" = 3 ]; then
	echo "headless: standalone exit status: ok"
else
	echo "headless: standalone exit status: FAILED (got '${status:-none}', want 3)"
	fail=1
fi
cstatus=$(cat "$dir/status_client" 2>/dev/null)
if [ "$cstatus" = 7 ]; then
	echo "headless: client exit status: ok"
else
	echo "headless: client exit status: FAILED (got '${cstatus:-none}', want 7)"
	fail=1
fi
cpwd=$(cat "$dir/client_pwd" 2>/dev/null)
if [ "$cpwd" = "$dir/sub" ]; then
	echo "headless: client cwd forwarding: ok"
else
	echo "headless: client cwd forwarding: FAILED (got '${cpwd:-none}', want '$dir/sub')"
	fail=1
fi
sstatus=$(cat "$dir/status_server" 2>/dev/null)
if [ "$sstatus" = 0 ] && [ ! -e "$dir/srv.sock" ]; then
	echo "headless: server clean shutdown & socket cleanup: ok"
else
	echo "headless: server clean shutdown: FAILED (status='${sstatus:-none}', sock=$(ls "$dir/srv.sock" 2>/dev/null || echo gone))"
	fail=1
fi
if [ "$fail" -ne 0 ]; then
	grep -E 'tam-last|ERROR|error' "$dir/log" | head -20 | sed 's/^/  | /'
fi
exit "$fail"
