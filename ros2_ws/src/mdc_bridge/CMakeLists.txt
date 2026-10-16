cmake_minimum_required(VERSION 3.8)
project(mdc_bridge)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(mdc_serial REQUIRED)

# ---------------------------------------------------------------------------
# 与 Qt 界面程序共用的协议契约头文件（shared/mdc_protocol.h）。
# 这是两端的唯一契约，故意放在工程根目录、两个构建系统都能引用到，
# 避免手抄两份键名字符串对不上。
# ---------------------------------------------------------------------------
get_filename_component(MDC_SHARED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../shared" ABSOLUTE)
if(NOT EXISTS "${MDC_SHARED_DIR}/mdc_protocol.h")
  message(FATAL_ERROR "找不到 shared/mdc_protocol.h，期望位置：${MDC_SHARED_DIR}")
endif()
message(STATUS "共用协议头文件目录: ${MDC_SHARED_DIR}")

# ---------------------------------------------------------------------------
# mdc_bridge：桥节点
# ---------------------------------------------------------------------------
add_executable(mdc_bridge src/mdc_bridge_node.cpp)
target_include_directories(mdc_bridge PRIVATE ${MDC_SHARED_DIR})
ament_target_dependencies(mdc_bridge rclcpp mdc_serial)

# ---------------------------------------------------------------------------
# mdc_fake_pub：假设备（无真机时验证用）
# ---------------------------------------------------------------------------
add_executable(mdc_fake_pub src/mdc_fake_pub.cpp)
target_include_directories(mdc_fake_pub PRIVATE ${MDC_SHARED_DIR})
ament_target_dependencies(mdc_fake_pub rclcpp mdc_serial)

install(TARGETS
  mdc_bridge
  mdc_fake_pub
  DESTINATION lib/${PROJECT_NAME}
)

ament_package()
