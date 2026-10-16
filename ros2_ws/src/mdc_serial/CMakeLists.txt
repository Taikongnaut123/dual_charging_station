cmake_minimum_required(VERSION 3.8)
project(mdc_serial)

find_package(ament_cmake REQUIRED)
find_package(rosidl_default_generators REQUIRED)

rosidl_generate_interfaces(${PROJECT_NAME}
  "msg/MdcData.msg"
  "srv/FanCtrl.srv"
  "srv/PowerioCtrl.srv"
  "srv/RelayCtrl.srv"
  "srv/WirelessCharge.srv"
  "srv/LedCtrl.srv"
)

ament_export_dependencies(rosidl_default_runtime)
ament_package()
