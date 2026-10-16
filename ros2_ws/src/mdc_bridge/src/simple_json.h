#ifndef SIMPLE_JSON_H
#define SIMPLE_JSON_H

/**
 * 极简 JSON 工具（只用于 mdc_bridge 内部）
 *
 * 为什么不用 nlohmann/json：ROS2 Humble 环境不保证装了该包，而这个协议是
 * 我们自己定的、结构极简（扁平 + 整数 + 整数数组），手写几十行足够，
 * 还能保证零第三方依赖。
 *
 * 注意：这不是通用 JSON 解析器，不处理嵌套对象 / 转义序列 / null。
 */

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace sjson
{

    /**
     * 定位 `"key":`，返回冒号后第一个字符的下标；找不到返回 npos
     *
     * 注意：必须循环往后找。因为键名可能先以**值的身份**出现，例如
     *   {"t":"cmd","cmd":"relay"}
     * 搜 "cmd" 时第一次命中的是 t 的值，那里后面跟的是 ',' 而不是 ':'，
     * 此时必须继续往后搜，否则会误判成"字段不存在"。
     */
    inline size_t findValuePos(const std::string &js, const char *key)
    {
        std::string needle = "\"";
        needle += key;
        needle += "\"";

        size_t searchFrom = 0;
        while (searchFrom < js.size())
        {
            const size_t hit = js.find(needle, searchFrom);
            if (hit == std::string::npos)
                return std::string::npos;

            size_t p = hit + needle.size();
            while (p < js.size() && (js[p] == ' ' || js[p] == '\t'))
                ++p;

            if (p < js.size() && js[p] == ':')
            {
                ++p;
                while (p < js.size() && (js[p] == ' ' || js[p] == '\t'))
                    ++p;
                return p;
            }

            searchFrom = hit + 1; // 这次命中的不是键，从下一个字符继续找
        }
        return std::string::npos;
    }

    /** 读取整数（允许负号） */
    inline bool getInt(const std::string &js, const char *key, long &out)
    {
        const size_t p = findValuePos(js, key);
        if (p == std::string::npos || p >= js.size())
            return false;

        const char *start = js.c_str() + p;
        char *end = nullptr;
        const long v = std::strtol(start, &end, 10);
        if (end == start)
            return false;

        out = v;
        return true;
    }

    /** 读取整数，带默认值 */
    inline long getIntOr(const std::string &js, const char *key, long fallback)
    {
        long v = fallback;
        getInt(js, key, v);
        return v;
    }

    /** 读取字符串（假设内部无转义字符，本项目自带协议满足该前提） */
    inline bool getString(const std::string &js, const char *key, std::string &out)
    {
        const size_t p = findValuePos(js, key);
        if (p == std::string::npos || p >= js.size() || js[p] != '"')
            return false;

        const size_t end = js.find('"', p + 1);
        if (end == std::string::npos)
            return false;

        out = js.substr(p + 1, end - p - 1);
        return true;
    }

    /** 读取整数数组，元素个数不足 expectN 时返回 false */
    inline bool getIntArray(const std::string &js, const char *key,
                            std::vector<long> &out, size_t expectN)
    {
        const size_t p = findValuePos(js, key);
        if (p == std::string::npos || p >= js.size() || js[p] != '[')
            return false;

        const size_t end = js.find(']', p);
        if (end == std::string::npos)
            return false;

        out.clear();
        size_t cur = p + 1;
        while (cur < end)
        {
            while (cur < end && (js[cur] == ' ' || js[cur] == ','))
                ++cur;
            if (cur >= end)
                break;
            char *numEnd = nullptr;
            const long v = std::strtol(js.c_str() + cur, &numEnd, 10);
            if (numEnd == js.c_str() + cur)
                break;
            out.push_back(v);
            cur = static_cast<size_t>(numEnd - js.c_str());
        }
        return out.size() >= expectN;
    }

    /**
     * 流式 JSON 对象构造器。
     *   JsonBuilder b;
     *   b.num("a", 1).str("t", "s").arr("led", data, 8);
     *   std::string s = b.done();
     */
    class Builder
    {
    public:
        Builder()
        {
            m_s.reserve(1024);
            m_s += '{';
        }

        Builder &num(const char *key, long long v)
        {
            sep();
            m_s += '"';
            m_s += key;
            m_s += "\":";
            m_s += std::to_string(v);
            return *this;
        }

        Builder &dbl(const char *key, double v)
        {
            sep();
            m_s += '"';
            m_s += key;
            m_s += "\":";
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.3f", v);
            m_s += buf;
            return *this;
        }

        Builder &str(const char *key, const std::string &v)
        {
            sep();
            m_s += '"';
            m_s += key;
            m_s += "\":\"";
            // 只做最小转义：协议里只有命令名/提示信息，可能含双引号或反斜杠
            for (char c : v)
            {
                if (c == '"' || c == '\\')
                {
                    m_s += '\\';
                    m_s += c;
                }
                else if (c == '\n' || c == '\r')
                {
                    m_s += ' ';
                }
                else
                {
                    m_s += c;
                }
            }
            m_s += '"';
            return *this;
        }

        Builder &boolean(const char *key, bool v)
        {
            sep();
            m_s += '"';
            m_s += key;
            m_s += "\":";
            m_s += (v ? "true" : "false");
            return *this;
        }

        Builder &arr(const char *key, const uint8_t *data, size_t n)
        {
            sep();
            m_s += '"';
            m_s += key;
            m_s += "\":[";
            for (size_t i = 0; i < n; ++i)
            {
                if (i)
                    m_s += ',';
                m_s += std::to_string(static_cast<unsigned>(data[i]));
            }
            m_s += ']';
            return *this;
        }

        std::string done()
        {
            m_s += '}';
            return m_s;
        }

    private:
        void sep()
        {
            if (!m_first)
                m_s += ',';
            m_first = false;
        }

        std::string m_s;
        bool m_first = true;
    };

} // namespace sjson

#endif // SIMPLE_JSON_H
