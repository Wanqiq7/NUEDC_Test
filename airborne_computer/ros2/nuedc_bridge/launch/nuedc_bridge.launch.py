from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description():
    return LaunchDescription([
        DeclareLaunchArgument("telemetry_endpoint", default_value="tcp://0.0.0.0:5557"),
        DeclareLaunchArgument("command_endpoint", default_value="tcp://0.0.0.0:5558"),
        DeclareLaunchArgument("mission_plan_path", default_value="runtime/active_mission_plan.json"),
        DeclareLaunchArgument("task_id", default_value="ros2-bridge"),
        DeclareLaunchArgument("odometry_topic", default_value="Odometry"),
        DeclareLaunchArgument("detections_topic", default_value="detections"),
        Node(
            package="nuedc_bridge",
            executable="nuedc_bridge_node",
            name="nuedc_bridge_node",
            output="screen",
            parameters=[{
                "telemetry_endpoint": LaunchConfiguration("telemetry_endpoint"),
                "command_endpoint": LaunchConfiguration("command_endpoint"),
                "mission_plan_path": LaunchConfiguration("mission_plan_path"),
                "task_id": LaunchConfiguration("task_id"),
                "odometry_topic": LaunchConfiguration("odometry_topic"),
                "detections_topic": LaunchConfiguration("detections_topic"),
            }],
        ),
    ])
