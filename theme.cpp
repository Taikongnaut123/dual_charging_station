#include "theme.h"

#include <QRegularExpression>
#include <QStringList>
#include <QStyle>
#include <QWidget>

namespace Theme
{
    namespace
    {
        qreal g_geoScale = 1.0;
        qreal g_fontScale = 1.0;
    }

    void setUiScale(qreal geoScale, qreal fontScale)
    {
        g_geoScale = qBound(0.3, geoScale, 2.0);
        g_fontScale = qBound(0.3, fontScale, 2.0);
    }

    qreal geoScale()
    {
        return g_geoScale;
    }

    qreal fontScale()
    {
        return g_fontScale;
    }

    int px(int designPx)
    {
        return qMax(1, qRound(designPx * g_geoScale));
    }

    int fontPx(int designPx)
    {
        return qMax(1, qRound(designPx * g_fontScale));
    }

    void repolish(QWidget *widget)
    {
        if (!widget)
            return;
        widget->style()->unpolish(widget);
        widget->style()->polish(widget);
        widget->update();
    }

    QString styleSheet()
    {
        // 说明：下面的颜色占位符会在函数末尾被替换成上面的常量，
        //      避免同一套色值在样式表和代码里各写一遍。
        static const char *rawCss = R"CSS(
* {
    font-family: "Noto Sans CJK SC", "Source Han Sans SC", "Microsoft YaHei",
                 "WenQuanYi Micro Hei", "PingFang SC", "sans-serif";
}

QWidget#root {
    background: qlineargradient(x1:0, y1:0, x2:0.7, y2:1,
                                stop:0 @BG_TOP@, stop:1 @BG_BOTTOM@);
}
QMainWindow {
    background: @BG_BOTTOM@;
}

/* ---------------- 卡片 ---------------- */
QFrame#card {
    background: @CARD@;
    border: 1px solid @CARD_BORDER@;
    border-radius: 14px;
}
QLabel#cardTitle {
    color: @TEXT2@;
    font-size: 18px;
    font-weight: 600;
}
QLabel#badge {
    color: @TEXT3@;
    background: @PANEL@;
    border: 1px solid @CARD_BORDER@;
    border-radius: 9px;
    padding: 2px 9px;
    font-size: 14px;
    font-weight: 600;
}
QLabel#badge[level="ok"]   { color: @GREEN@;  border-color: rgba(34,197,94,90);   background: rgba(34,197,94,26); }
QLabel#badge[level="warn"] { color: @AMBER@;  border-color: rgba(245,165,36,90);  background: rgba(245,165,36,26); }
QLabel#badge[level="err"]  { color: @RED@;    border-color: rgba(240,75,75,90);   background: rgba(240,75,75,26); }
QLabel#badge[level="info"] { color: @ACCENT@; border-color: rgba(34,211,238,90);  background: rgba(34,211,238,26); }

/* 标题旁的 "?" 提示：平时很淡，悬停才高亮，不抢操作员的注意力 */
QLabel#helpMark {
    color: @TEXT3@;
    background: @PANEL@;
    border: 1px solid @CARD_BORDER@;
    border-radius: 8px;
    font-size: 13px;
    font-weight: 700;
    min-width: 16px;
    max-width: 16px;
    min-height: 16px;
    max-height: 16px;
}
QLabel#helpMark:hover {
    color: @ACCENT@;
    border-color: @ACCENT@;
}

/* 悬停说明本身也是界面的一部分，默认白底黑字的 tooltip 在暗色界面上非常突兀 */
QToolTip {
    color: @TEXT@;
    background: @CARD@;
    border: 1px solid @CARD_BORDER@;
    padding: 6px 9px;
    font-size: 13px;
}

/* ---------------- 未启用的充电口（link_status = 0） ----------------
 * 整卡降级：底色压暗 + 虚线边框，一眼就能看出"这个口没启用"。
 * 自绘的环形表不吃 QSS，那边由 CircularGauge::setDimmed() 单独处理。
 */
QFrame#card[link="off"] {
    background: @BG_BOTTOM@;
    border: 1px dashed @CARD_BORDER@;
}
QFrame#card[link="off"] QLabel#cardTitle,
QFrame#card[link="off"] QLabel#rowKey,
QFrame#card[link="off"] QLabel#rowVal { color: @TEXT3@; }

/* 未启用提示：用琥珀色，和"故障红"区分开——没接电池不是故障 */
QLabel#portHint { color: @AMBER@; font-size: 13px; }

/* ---------------- 数据小格子 ---------------- */
QFrame#tile {
    background: @PANEL@;
    border: 1px solid @CARD_BORDER@;
    border-radius: 10px;
}
QLabel#tileCaption { color: @TEXT3@; font-size: 13px; }
QLabel#tileValue   { color: @TEXT@;  font-size: 22px; font-weight: 700; }
QLabel#tileUnit    { color: @TEXT2@; font-size: 13px; }
QLabel#tileSub     { color: @TEXT3@; font-size: 13px; }

/* ---------------- 标题 / 时钟 ---------------- */
QLabel#appTitle  { color: @TEXT@; font-size: 24px; font-weight: 700; }
QLabel#appSub    { color: @TEXT3@; font-size: 13px; }
QLabel#clockTime { color: @TEXT@; font-size: 26px; font-weight: 700; }
QLabel#clockDate { color: @TEXT3@; font-size: 13px; }

/* ---------------- 正文 ---------------- */
QLabel#heroState  { font-size: 30px; font-weight: 700; }
QLabel#heroSub    { color: @TEXT2@; font-size: 15px; }
QLabel#sectionLabel { color: @TEXT3@; font-size: 13px; font-weight: 600; }
QLabel#rowKey     { color: @TEXT2@; font-size: 15px; }
QLabel#rowVal     { color: @TEXT@;  font-size: 19px; font-weight: 600; }
QLabel#rowValMono { color: @TEXT@;  font-size: 19px; font-weight: 600; font-family: "DejaVu Sans Mono", monospace; }

/* ---------------- 顶部状态胶囊 ---------------- */
QLabel#pill {
    color: @TEXT2@;
    background: @PANEL@;
    border: 1px solid @CARD_BORDER@;
    border-radius: 13px;
    padding: 4px 13px;
    font-size: 15px;
    font-weight: 600;
}
QLabel#pill[level="ok"]   { color: @GREEN@;  border-color: rgba(34,197,94,80);   background: rgba(34,197,94,24); }
QLabel#pill[level="warn"] { color: @AMBER@;  border-color: rgba(245,165,36,80);  background: rgba(245,165,36,24); }
QLabel#pill[level="err"]  { color: @RED@;    border-color: rgba(240,75,75,80);   background: rgba(240,75,75,24); }
QLabel#pill[level="info"] { color: @ACCENT@; border-color: rgba(34,211,238,80);  background: rgba(34,211,238,24); }

/* ---------------- 充电进度条 ---------------- */
QProgressBar#chargeBar {
    background: @TRACK@;
    border: none;
    border-radius: 11px;
}
QProgressBar#chargeBar::chunk {
    border-radius: 11px;
    background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                                stop:0 #16A34A, stop:1 #22D3EE);
}
QProgressBar#chargeBar[level="warn"]::chunk {
    background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #D97706, stop:1 #F5A524);
}
QProgressBar#chargeBar[level="err"]::chunk {
    background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #B91C1C, stop:1 #F04B4B);
}

/* ---------------- 按钮 ---------------- */
QPushButton {
    color: @TEXT@;
    background: @PANEL@;
    border: 1px solid @CARD_BORDER@;
    border-radius: 12px;
    padding: 0 20px;
    min-height: 46px;
    font-size: 16px;
    font-weight: 600;
}
QPushButton:hover   { background: #16233A; border-color: #33507A; }
QPushButton:pressed { background: #0B1526; }
QPushButton:disabled { color: @TEXT3@; background: #0C1524; border-color: #172236; }

QPushButton#primaryBtn {
    color: #05230F;
    border: none;
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #22C55E, stop:1 #16A34A);
}
QPushButton#primaryBtn:hover   { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #34D77A, stop:1 #1CB054); }
QPushButton#primaryBtn:pressed { background: #15803D; }
QPushButton#primaryBtn:disabled { color: @TEXT3@; background: #16233A; }

QPushButton#dangerBtn {
    color: @RED@;
    border-color: rgba(240,75,75,110);
    background: rgba(240,75,75,20);
}
QPushButton#dangerBtn:hover { background: rgba(240,75,75,46); }
QPushButton#dangerBtn:disabled { color: @TEXT3@; background: #0C1524; border-color: #172236; }

QPushButton#ghostBtn {
    color: @TEXT2@;
    background: transparent;
}
QPushButton#ghostBtn:hover { color: @TEXT@; border-color: #33507A; }
QPushButton#ghostBtn:disabled { color: @TEXT3@; background: transparent; }

/* 底部工具条上的小按钮：需要覆盖上面的 min-height */
QPushButton#miniBtn {
    min-height: 26px;
    max-height: 26px;
    padding: 0 14px;
    border-radius: 8px;
    font-size: 13px;
    color: @TEXT2@;
    background: transparent;
}
QPushButton#miniBtn:hover { color: @TEXT@; border-color: #33507A; background: #16233A; }

/* ---------------- 日志列表 ---------------- */
QListWidget#logList {
    background: transparent;
    border: none;
    outline: none;
    color: @TEXT2@;
    font-size: 16px;
}
QListWidget#logList::item {
    padding: 5px 2px;
    border-bottom: 1px solid rgba(30,44,70,110);
}
QListWidget#logList::item:selected {
    background: rgba(34,211,238,22);
    color: @TEXT@;
}

/* ---------------- 滚动条 ---------------- */
QScrollBar:vertical { background: transparent; width: 8px; margin: 0; }
QScrollBar::handle:vertical { background: #24354F; border-radius: 4px; min-height: 26px; }
QScrollBar::handle:vertical:hover { background: #33507A; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }

QLabel#footer { color: @TEXT3@; font-size: 13px; }

/* ---------------- 页签 ---------------- */
QTabWidget#mainTabs::pane {
    border: 1px solid @CARD_BORDER@;
    border-radius: 12px;
    background: transparent;
    top: -1px;
}
QTabBar::tab {
    background: @PANEL@;
    color: @TEXT2@;
    border: 1px solid @CARD_BORDER@;
    border-bottom: none;
    border-top-left-radius: 10px;
    border-top-right-radius: 10px;
    padding: 8px 22px;
    margin-right: 6px;
    /* 注意：这里**不能**写 font-size。QTabBar 的尺寸和文字绘制区域是按
     * 控件字体算的，QSS 只改绘制字体、不改尺寸，字号一大文字就被裁，
     * 加 padding 也没用（实测加到 120px 依然裁）。
     * 页签字号在 mainwindow.cpp 里用 setFont 设，见 Theme::fontPx()。
     */
}
QTabBar::tab:selected {
    color: @TEXT@;
    background: @CARD@;
    border-color: #33507A;
}
QTabBar::tab:hover:!selected { color: @TEXT@; }

/* ---------------- 滚动区 ---------------- */
QScrollArea { background: transparent; border: none; }
QScrollArea > QWidget > QWidget { background: transparent; }

/* ---------------- 滑块 ---------------- */
QSlider::groove:horizontal {
    height: 6px;
    background: @TRACK@;
    border-radius: 3px;
}
QSlider::sub-page:horizontal { background: @ACCENT@; border-radius: 3px; }
QSlider::handle:horizontal {
    background: #E9F0FA;
    width: 14px;
    margin: -5px 0;
    border-radius: 7px;
}
QSlider::handle:horizontal:hover { background: #FFFFFF; }

/* ---------------- 数值输入 ---------------- */
QSpinBox {
    background: @PANEL@;
    color: @TEXT@;
    border: 1px solid @CARD_BORDER@;
    border-radius: 8px;
    padding: 3px 6px;
    min-height: 24px;
    font-size: 13px;
}
QSpinBox:focus { border-color: #33507A; }
QSpinBox::up-button, QSpinBox::down-button {
    width: 14px;
    border: none;
    background: transparent;
}

/* ---------------- 下拉框 ---------------- */
QComboBox {
    background: @PANEL@;
    color: @TEXT@;
    border: 1px solid @CARD_BORDER@;
    border-radius: 8px;
    padding: 3px 8px;
    min-height: 24px;
    font-size: 13px;
}
QComboBox:hover { border-color: #33507A; }
QComboBox::drop-down { border: none; width: 18px; }
QComboBox QAbstractItemView {
    background: @CARD@;
    color: @TEXT@;
    border: 1px solid @CARD_BORDER@;
    selection-background-color: rgba(34,211,238,45);
    outline: none;
}

/* ---------------- 复选框 ---------------- */
QCheckBox { color: @TEXT@; font-size: 13px; spacing: 8px; }
QCheckBox::indicator {
    width: 16px;
    height: 16px;
    border-radius: 4px;
    border: 1px solid @CARD_BORDER@;
    background: @PANEL@;
}
QCheckBox::indicator:hover { border-color: #33507A; }
QCheckBox::indicator:checked { background: @GREEN@; border-color: @GREEN@; }
)CSS";

        QString css = QString::fromUtf8(rawCss);
        css.replace(QLatin1String("@BG_TOP@"), BgTop.name());
        css.replace(QLatin1String("@BG_BOTTOM@"), BgBottom.name());
        css.replace(QLatin1String("@CARD@"), Card.name());
        css.replace(QLatin1String("@CARD_BORDER@"), CardBorder.name());
        css.replace(QLatin1String("@PANEL@"), Panel.name());
        css.replace(QLatin1String("@TRACK@"), Track.name());
        css.replace(QLatin1String("@TEXT@"), TextPrimary.name());
        css.replace(QLatin1String("@TEXT2@"), TextSecondary.name());
        css.replace(QLatin1String("@TEXT3@"), TextMuted.name());
        css.replace(QLatin1String("@ACCENT@"), Accent.name());
        css.replace(QLatin1String("@GREEN@"), Green.name());
        css.replace(QLatin1String("@AMBER@"), Amber.name());
        css.replace(QLatin1String("@RED@"), Red.name());
        css.replace(QLatin1String("@VIOLET@"), Violet.name());

        // ---- 按屏幕缩放：样式表里的 px 全部按设计稿像素处理 ----
        // font-size 用字号系数（缩得少），其余（padding/radius/border/宽高）
        // 用几何系数。逐行处理，因为每条 font-size 声明都独占一行。
        if (!qFuzzyCompare(g_geoScale, 1.0) || !qFuzzyCompare(g_fontScale, 1.0))
        {
            static const QRegularExpression pxRe(QStringLiteral("(\\d+)px"));
            QStringList lines = css.split(QLatin1Char('\n'));
            for (QString &line : lines)
            {
                if (!line.contains(QLatin1String("px")))
                    continue;
                const qreal k = line.contains(QLatin1String("font-size")) ? g_fontScale
                                                                          : g_geoScale;
                if (qFuzzyCompare(k, 1.0))
                    continue;

                QString out;
                int last = 0;
                auto it = pxRe.globalMatch(line);
                while (it.hasNext())
                {
                    const auto m = it.next();
                    out += line.mid(last, int(m.capturedStart()) - last);
                    out += QString::number(qMax(1, qRound(m.captured(1).toInt() * k)));
                    last = int(m.capturedEnd()) - 2; // 跳过 "px"，它要保留
                }
                out += line.mid(last);
                line = out;
            }
            css = lines.join(QLatin1Char('\n'));
        }
        return css;
    }

} // namespace Theme
