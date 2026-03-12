FROM ros:humble-ros-base AS build
SHELL ["/bin/bash", "-lc"]
WORKDIR /ws

# Install build dependencies
COPY apt-requirements.txt /tmp/apt-build.txt
RUN apt-get update \
  && grep -Ev '^[[:space:]]*($|#)' /tmp/apt-build.txt | xargs apt-get install -y --no-install-recommends \
  && rm -rf /var/lib/apt/lists/* /tmp/apt-build.txt

# Copy package manifest and install rosdeps
COPY package.xml ./package.xml
RUN add-apt-repository multiverse || true
RUN source /opt/ros/humble/setup.bash \
  && rosdep update || true \
  && rosdep install -i --from-paths . --rosdistro humble -y || true

# Copy sources and build
COPY . .
RUN source /opt/ros/humble/setup.bash \
  && CCACHE_DIR=/ccache mkdir -p /ccache \
  && chmod 777 /ccache \
  && colcon build --parallel-workers $(nproc) \
    --cmake-args -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache

FROM ros:humble-ros-base
WORKDIR /ws
## Install minimal runtime packages required by the built binaries
COPY apt-runtime.txt /tmp/apt-runtime.txt
RUN apt-get update \
  && grep -Ev '^[[:space:]]*($|#)' /tmp/apt-runtime.txt | xargs apt-get install -y --no-install-recommends \
  && rm -rf /var/lib/apt/lists/* /tmp/apt-runtime.txt

COPY --from=build /ws/install /ws/install
COPY docker-entrypoint.sh /ros_entrypoint.sh
RUN chmod +x /ros_entrypoint.sh
# Bake FastDDS UDP-only profile into the image so the shared-memory transport
# fix applies regardless of how the container is started (compose or docker run).
COPY fastdds_no_shm.xml /ws/fastdds_no_shm.xml
ENV ROS_DISTRO=humble
ENV FASTRTPS_DEFAULT_PROFILES_FILE=/ws/fastdds_no_shm.xml

## Make ROS and workspace overlays available for interactive shells
RUN echo "source /opt/ros/${ROS_DISTRO}/setup.bash || true" > /etc/profile.d/ros2.sh \
 && echo "[ -f /ws/install/setup.bash ] && source /ws/install/setup.bash" >> /etc/profile.d/ros2.sh \
 && chmod +x /etc/profile.d/ros2.sh

# Also source in non-login interactive bash shells
RUN echo "source /opt/ros/${ROS_DISTRO}/setup.bash || true" >> /etc/bash.bashrc \
 && echo "[ -f /ws/install/setup.bash ] && source /ws/install/setup.bash" >> /etc/bash.bashrc

ENTRYPOINT ["/ros_entrypoint.sh"]
CMD ["image2rtsp.launch.py"]
