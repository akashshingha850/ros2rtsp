import os
from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node

def generate_launch_description():
   config = os.path.join(
      get_package_share_directory('image2rtsp'),
      'config',
      'parameters.yaml'
      )

   log_level = LaunchConfiguration('log_level')

   return LaunchDescription([
      DeclareLaunchArgument(
         'log_level',
         default_value='warn',
         description='ROS logger level (debug, info, warn, error, fatal)'
      ),
      Node(
         package='image2rtsp',
         executable='image2rtsp',
         name='image2rtsp',
         parameters=[config],
         arguments=['--ros-args', '--log-level', log_level]
      )
   ])