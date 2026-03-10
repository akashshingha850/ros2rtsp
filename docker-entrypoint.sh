#!/usr/bin/env bash
set -e

# Source ROS and workspace overlays then exec ros2 launch with provided args
if [ -n "${ROS_DISTRO:-}" ]; then
  source "/opt/ros/${ROS_DISTRO}/setup.bash" || true
else
  source /opt/ros/humble/setup.bash || true
fi

if [ -f /ws/install/setup.bash ]; then
  source /ws/install/setup.bash
fi

exec ros2 launch image2rtsp "$@"
