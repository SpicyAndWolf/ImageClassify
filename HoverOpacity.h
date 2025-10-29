#pragma once
#include <QObject>
#include <QPointer>
#include <QPropertyAnimation>
#include <QGraphicsOpacityEffect>
#include <QEvent>
#include <QWidget>

class HoverOpacity : public QObject {
    Q_OBJECT
public:
    explicit HoverOpacity(QWidget* target,
                          qreal hoverOpacity = 1.0,   // 悬停目标（一般 <=1.0）
                          int durationMs = 160)
        : QObject(target), m_target(target), m_hover(hoverOpacity)
    {
        m_eff = qobject_cast<QGraphicsOpacityEffect*>(target->graphicsEffect());
        if (!m_eff) {
            m_eff = new QGraphicsOpacityEffect(target);
            target->setGraphicsEffect(m_eff);
        }
        m_base = 1.0;                   // 常态基线一律用 1.0
        m_eff->setOpacity(m_base);      // 初始化保持不变

        m_ani = new QPropertyAnimation(m_eff, "opacity", this);
        m_ani->setDuration(durationMs);
        m_ani->setEasingCurve(QEasingCurve::OutCubic);

        target->setAttribute(Qt::WA_Hover, true); // 更稳定的 Hover 事件
        target->installEventFilter(this);
    }

protected:
    bool eventFilter(QObject* obj, QEvent* ev) override {
        if (obj == m_target) {
            if (ev->type() == QEvent::Enter || ev->type() == QEvent::HoverEnter) {
                animateTo(m_hover);
            } else if (ev->type() == QEvent::Leave || ev->type() == QEvent::HoverLeave) {
                animateTo(m_base);      // 鼠标离开回到 1.0
            } else if (ev->type() == QEvent::Hide || ev->type() == QEvent::EnabledChange) {
                // 防止异常状态残留
                m_ani->stop();
                m_eff->setOpacity(m_base);
            }
        }
        return QObject::eventFilter(obj, ev);
    }

private:
    void animateTo(qreal end) {
        m_ani->stop();
        m_ani->setStartValue(m_eff->opacity());
        m_ani->setEndValue(end);
        m_ani->start();
    }

    QPointer<QWidget> m_target;
    QGraphicsOpacityEffect* m_eff = nullptr;
    QPropertyAnimation* m_ani = nullptr;
    qreal m_base = 1.0;      // 常态=1.0
    qreal m_hover = 1.0;     // 悬停目标
};
