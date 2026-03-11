FROM ros:humble-ros-base AS build
SHELL ["/bin/bash", "-lc"]
WORKDIR /ws

# Install OS packages first (cached layer when requirements.txt unchanged)
COPY apt-requirements.txt /tmp/requirements.txt
RUN apt-get update \
  && xargs -a /tmp/requirements.txt apt-get install -y --no-install-recommends \
  && rm -rf /var/lib/apt/lists/* /tmp/requirements.txt

# Copy package manifest(s) so rosdep can install system deps and this layer caches
COPY package.xml ./package.xml
RUN source /opt/ros/humble/setup.bash \
  && rosdep update || true \
  && rosdep install -i --from-paths . --rosdistro humble -y || true

# Copy rest of the sources after deps to avoid busting the deps layer on code changes
COPY . .

# Build with Ninja + ccache for faster incremental builds inside the container
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
  && xargs -a /tmp/apt-runtime.txt apt-get install -y --no-install-recommends \
  && rm -rf /var/lib/apt/lists/* /tmp/apt-runtime.txt

COPY --from=build /ws/install /ws/install
COPY docker-entrypoint.sh /ros_entrypoint.sh
RUN chmod +x /ros_entrypoint.sh
ENV ROS_DISTRO=humble

## Make ROS and workspace overlays available for interactive shells
# This ensures `docker exec -it <container> bash` has `ros2` on PATH
RUN echo "source /opt/ros/${ROS_DISTRO}/setup.bash || true" > /etc/profile.d/ros2.sh \
 && echo "[ -f /ws/install/setup.bash ] && source /ws/install/setup.bash" >> /etc/profile.d/ros2.sh \
 && chmod +x /etc/profile.d/ros2.sh

# Also source in non-login interactive bash shells
RUN echo "source /opt/ros/${ROS_DISTRO}/setup.bash || true" >> /etc/bash.bashrc \
 && echo "[ -f /ws/install/setup.bash ] && source /ws/install/setup.bash" >> /etc/bash.bashrc

ENTRYPOINT ["/ros_entrypoint.sh"]
CMD ["image2rtsp.launch.py"]
