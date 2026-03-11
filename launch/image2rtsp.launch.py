import os
from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch_ros.actions import Node

def generate_launch_description():
   config = os.path.join(
      get_package_share_directory('image2rtsp'),
      'config',
      'parameters.yaml'
      )

   return LaunchDescription([
      Node(
         package='image2rtsp',
         executable='image2rtsp',
         name='image2rtsp',
         parameters=[config],
         # Reduce runtime logging verbosity to WARN to avoid info spam in container logs
         arguments=['--ros-args', '--log-level', 'warn']
      )
   ])