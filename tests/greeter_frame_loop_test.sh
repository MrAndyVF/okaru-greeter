#!/bin/sh
set -eu

compositor=$1
client=$2
unset WAYLAND_DISPLAY WAYLAND_SOCKET GREETD_SOCK
test_dir=$(mktemp -d "${TMPDIR:-/tmp}/noctalia-frame-loop.XXXXXX")
trap 'rm -rf "$test_dir"' EXIT INT TERM
mkdir "$test_dir/runtime" "$test_dir/state"
chmod 700 "$test_dir/runtime"

# The compositor does not propagate its child's exit code, so also require the
# client's success marker. Keep the combined log available on any failure.
if ! env \
  XDG_RUNTIME_DIR="$test_dir/runtime" \
  OKARU_GREETER_STATE_DIR="$test_dir/state" \
  WLR_BACKENDS=headless \
  WLR_HEADLESS_OUTPUTS=1 \
  WLR_RENDERER=pixman \
  LIBGL_ALWAYS_SOFTWARE=1 \
  GREETER_BIN="$client" \
  OKARU_GREETER_LOG=stderr \
  "$compositor" >"$test_dir/log" 2>&1; then
  cat "$test_dir/log"
  exit 1
fi
if ! grep -Fx 'frame loop: startup, animation, redraw/layout, idle, and restart passed' "$test_dir/log" >/dev/null; then
  cat "$test_dir/log"
  exit 1
fi
