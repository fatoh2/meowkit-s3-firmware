#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
temp_dir=$(mktemp -d /tmp/meowgotchi-tests.XXXXXX)
trap 'rm -f "$temp_dir/frames" "$temp_dir/radio"; rmdir "$temp_dir"' EXIT
flags=(-std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer)
g++ "${flags[@]}" tools/tests/meowgotchi_frames.cpp -o "$temp_dir/frames"
"$temp_dir/frames"
g++ "${flags[@]}" -Itools/tests/fakes tools/tests/meowgotchi_radio.cpp \
    src/app/app_11/wifi_hunter.cpp -o "$temp_dir/radio"
"$temp_dir/radio"
