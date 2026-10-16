#ifndef THEME_H
#define THEME_H

#include <QColor>
#include <QString>

class QWidget;

/**
 * 双轮足机器人充电桩 —— 全局视觉主题
 *
 * 所有配色、圆角、字号集中定义在这里：
 *  1. 方便统一调整外观；
 *  2. 后续想加"夜间 / 白天"主题时只需要换一组常量。
 */
namespace Theme
{

    // ---------------- 背景 ----------------
    const QColor BgTop("#0B1424");
    const QColor BgBottom("#060A13");

    // ---------------- 卡片 / 控件层 ----------------
    const QColor Card("#111C30");
    const QColor CardBorder("#1E2C46");
    const QColor Panel("#0D1729");
    const QColor Track("#1A2740");

    // ---------------- 文字 ----------------
    const QColor TextPrimary("#E9F0FA");
    const QColor TextSecondary("#8A9CBB");
    const QColor TextMuted("#5D6F8E");

    // ---------------- 语义色 ----------------
    const QColor Accent("#22D3EE"); // 主强调（青）
    const QColor Green("#22C55E");  // 正常 / 充电中
    const QColor Amber("#F5A524");  // 警告
    const QColor Red("#F04B4B");    // 故障 / 急停
    const QColor Violet("#8B5CF6");

    const int RadiusCard = 14;

    /** 修改动态属性后重新应用样式（配合 QLabel[level="ok"] 之类的选择器） */
    void repolish(QWidget *widget);

    /** 全局样式表（在 main.cpp 中应用到 QApplication） */
    QString styleSheet();

} // namespace Theme

#endif // THEME_H
