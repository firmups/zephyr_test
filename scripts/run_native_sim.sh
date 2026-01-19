#!/usr/bin/env bash

REPO_ROOT=$(git rev-parse --show-toplevel)
EXECUTABLE="$REPO_ROOT/firmware/build_native/zephyr/zephyr.exe"

PIPE_NAME=$(mktemp)
rm -f "$PIPE_NAME"
mkfifo "$PIPE_NAME"
nohup "$EXECUTABLE" -attach_uart -attach_uart_cmd="echo %s > $PIPE_NAME" >/dev/null 2>&1 &
DEVICE=$(cat "$PIPE_NAME")
minicom --device "$DEVICE" -c on
kill $(pgrep -f zephyr.exe)
rm -f "$PIPE_NAME"
reset