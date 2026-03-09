#!/usr/bin/env bash
set -e

# Minimal launcher: runs a package launch with optional args.
exec ros2 launch image2rtsp "${1:-image2rtsp.launch.py}" "${@:2}"
