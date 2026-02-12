#!/bin/bash
# Test: Notify mode toggle, flash on change, dismiss on enter.

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
source "$SCRIPT_DIR/helpers.sh"

setup_tmpdir
start_helper
start_sniptotop -n

create_snippet

# Focus snippet, press 'n' to enable notify mode
press_key "n" "$SNIPPET_WID"
sleep 0.3

# Border should be green (00FF00)
color=$(get_pixel_color "$SNIPPET_WID" 0 0)
assert_eq "$color" "00FF00" "notify border is green" || fail "border not green: $color"

# Trigger content change via SIGUSR1 to helper
kill -USR1 "$HELPER_PID"
sleep 0.5

# Border should be flashing — sample and check for red (FF0000)
# Flash pattern is 200ms red, 800ms green — sample a few times
found_red=0
for i in $(seq 1 10); do
	color=$(get_pixel_color "$SNIPPET_WID" 0 0)
	if [ "$color" = "FF0000" ]; then
		found_red=1
		break
	fi
	sleep 0.15
done
assert_eq "$found_red" "1" "flash shows red border" || fail "never saw red flash"

# Move mouse into snippet to dismiss flash
# Get the window position and move mouse into its center
pos=$(get_window_pos "$SNIPPET_WID")
sx=$(echo "$pos" | awk '{print $1}')
sy=$(echo "$pos" | awk '{print $2}')
size=$(get_window_size "$SNIPPET_WID")
sw=$(echo "$size" | awk '{print $1}')
sh=$(echo "$size" | awk '{print $2}')
xdotool mousemove --sync $((sx + sw / 2)) $((sy + sh / 2))
sleep 1.0

# Border should be back to green (flash dismissed).
# Retry a few times in case of timing issues.
dismissed=0
for i in $(seq 1 5); do
	color=$(get_pixel_color "$SNIPPET_WID" 0 0)
	if [ "$color" = "00FF00" ]; then
		dismissed=1
		break
	fi
	sleep 0.3
done
assert_eq "$dismissed" "1" "flash dismissed, border green" || fail "border not green after enter: $color"

# Press 'n' again to disable notify
press_key "n" "$SNIPPET_WID"
sleep 0.3

# Border should be black (000000)
color=$(get_pixel_color "$SNIPPET_WID" 0 0)
assert_eq "$color" "000000" "notify disabled, border black" || fail "border not black: $color"

echo "test_notify: all assertions passed"
cleanup
