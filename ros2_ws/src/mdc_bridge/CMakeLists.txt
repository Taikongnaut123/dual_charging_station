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
# MdcData 里含 std_msgs/Header，直接用到就得直接声明
find_package(std_msgs REQUIRED)

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
ament_target_dependencies(mdc_bridge rclcpp mdc_serial std_msgs)

# ---------------------------------------------------------------------------
# mdc_fake_pub：假设备（无真机时验证用）
# ---------------------------------------------------------------------------
add_executable(mdc_fake_pub src/mdc_fake_pub.cpp)
target_include_directories(mdc_fake_pub PRIVATE ${MDC_SHARED_DIR})
ament_target_dependencies(mdc_fake_pub rclcpp mdc_serial std_msgs)

install(TARGETS
  mdc_bridge
  mdc_fake_pub
  DESTINATION lib/${PROJECT_NAME}
)

# ---------------------------------------------------------------------------
# 让安装后的可执行文件自己找到 mdc_serial 的接口库。
#
# 打包后接口库放在 <包根>/lib/，可执行文件放在 <包根>/bin/，相对位置固定，
# 所以用 $ORIGIN（运行时二进制自身所在目录）定位即可：$ORIGIN/../lib。
# 整个包目录拷到哪、挪到哪都能直接跑，不需要重新配置。
#
# 不设这个的话，用户直接敲 ./bin/mdc_bridge 会报：
#     libmdc_serial__rosidl_typesupport_cpp.so: cannot open shared object file
# （未打包时由 colcon overlay 的 LD_LIBRARY_PATH 兜住，所以在开发机上察觉不到）
#
# --disable-new-dtags 是**必须**的，不能省：默认链出的标记是 DT_RUNPATH，
# 而 DT_RUNPATH 只对可执行文件自己的直接依赖生效，**对 dlopen 无效**。
# 那几个 typesupport 库（fastrtps / introspection / generator_c）恰恰是运行时
# dlopen 的，用 DT_RUNPATH 会漏掉，报错变成：
#     Could not load library libmdc_serial__rosidl_typesupport_fastrtps_cpp.so
# 换成传统的 DT_RPATH，它会被继承、且对 dlopen 生效，才能真正自包含。
# ---------------------------------------------------------------------------
set_target_properties(mdc_bridge mdc_fake_pub PROPERTIES
  INSTALL_RPATH "$ORIGIN/../lib"
  BUILD_WITH_INSTALL_RPATH TRUE
  LINK_FLAGS "-Wl,--disable-new-dtags"
)

ament_package()
