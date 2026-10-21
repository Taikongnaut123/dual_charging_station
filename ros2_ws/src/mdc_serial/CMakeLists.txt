cmake_minimum_required(VERSION 3.8)
project(mdc_serial)

find_package(ament_cmake REQUIRED)
find_package(rosidl_default_generators REQUIRED)
# MdcData.msg 里用了 std_msgs/Header，必须显式声明，否则生成代码找不到头文件
find_package(std_msgs REQUIRED)

# 协议**只收不发**（充电桩主机自主完成充电控制），所以只有 msg，没有 srv。
rosidl_generate_interfaces(${PROJECT_NAME}
  "msg/MdcData.msg"
  DEPENDENCIES std_msgs
)

ament_export_dependencies(rosidl_default_runtime std_msgs)
ament_package()
