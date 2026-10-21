#ifndef SIMPLE_JSON_H
#define SIMPLE_JSON_H

/**
 * 极简 JSON **序列化**工具（只写，不读）。
 *
 * 为什么不用 nlohmann/json 之类的库：
 *   协议是扁平的（一层键值，没有嵌套对象、没有转义字符串），序列化只需要
 *   几十行；引进一个第三方依赖会让桥的编译依赖变重，而它将来要部署到
 *   机器人上，越少依赖越好。
 *
 * 注意：
 *   · 只做字符串 / 整数 / 浮点三种值，够本协议用
 *   · 字符串值不做转义处理 —— 本协议里出现的字符串全是固定的短标识
 *     （包类型 "s"/"hb"、键名等），不含引号与反斜杠，前提成立
 *   · 输出是**单行紧凑** JSON（UDP 一包一条，不能有换行）
 */

#include <cstdio>
#include <string>

namespace sjson
{

    class Builder
    {
    public:
        Builder() { m_out = "{"; }

        /** 追加一个整数字段 */
        Builder &num(const char *key, long long v)
        {
            sep();
            m_out += '"';
            m_out += key;
            m_out += "\":";
            m_out += std::to_string(v);
            return *this;
        }

        /** 追加一个小数（时间戳这类） */
        Builder &dbl(const char *key, double v)
        {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.6f", v);
            sep();
            m_out += '"';
            m_out += key;
            m_out += "\":";
            m_out += buf;
            return *this;
        }

        /** 追加一个字符串字段（不做转义，见文件头说明） */
        Builder &str(const char *key, const std::string &v)
        {
            sep();
            m_out += '"';
            m_out += key;
            m_out += "\":\"";
            m_out += v;
            m_out += '"';
            return *this;
        }

        /** 收尾，返回完整 JSON 文本 */
        std::string done()
        {
            m_out += '}';
            return m_out;
        }

    private:
        void sep()
        {
            if (m_out.size() > 1)
                m_out += ',';
        }

        std::string m_out;
    };

} // namespace sjson

#endif // SIMPLE_JSON_H
