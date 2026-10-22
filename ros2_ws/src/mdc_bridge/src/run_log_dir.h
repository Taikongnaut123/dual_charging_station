#ifndef RUN_LOG_DIR_H
#define RUN_LOG_DIR_H

/**
 * 把 ROS2 的日志文件默认写到"程序所在目录/log/"。
 *
 * rclcpp 默认写 $HOME/.ros/log/<节点名>_<pid>_<时间戳>.log。一体屏上程序通常由
 * 开机脚本或 systemd 拉起，$HOME 可能是 /root、也可能根本不一样，日志跑哪去了
 * 现场很难找。指到可执行文件旁边，翻日志时直接进程序目录就行。
 *
 * 三条规矩：
 *   1. 必须在 rclcpp::init() **之前**调用 —— 日志目录是 init 时定下来的；
 *   2. 外部已经设了 ROS_LOG_DIR 就完全不动（保留用环境变量改的余地）；
 *   3. 目录建不出来或不可写（比如程序装在只读的 /opt 下）就放弃，
 *      让 rclcpp 走它自己的默认路径，不能因为日志把程序拖挂。
 */

#include <cerrno>
#include <cstdlib>
#include <string>

#include <sys/stat.h>
#include <unistd.h>

namespace runlog
{

    /** 可执行文件所在目录；取不到返回空串（用 /proc/self/exe，不受 argv[0] 影响） */
    inline std::string exeDir()
    {
        char buf[4096];
        const ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
        if (n <= 0)
            return std::string();
        buf[n] = '\0';

        std::string path(buf);
        const std::string::size_type pos = path.find_last_of('/');
        return (pos == std::string::npos) ? std::string() : path.substr(0, pos);
    }

    /** 设置 ROS_LOG_DIR 默认值；返回最终生效的目录（可能为空，表示没改） */
    inline std::string useRunDirLogs()
    {
        if (const char *env = std::getenv("ROS_LOG_DIR"))
        {
            if (*env != '\0')
                return std::string(); // 外部指定了，尊重它
        }

        const std::string dir = exeDir();
        if (dir.empty())
            return std::string();

        const std::string logDir = dir + "/log";
        if (::mkdir(logDir.c_str(), 0755) != 0 && errno != EEXIST)
            return std::string();
        if (::access(logDir.c_str(), W_OK) != 0)
            return std::string();

        ::setenv("ROS_LOG_DIR", logDir.c_str(), 1);
        return logDir;
    }

} // namespace runlog

#endif // RUN_LOG_DIR_H
