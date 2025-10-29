#pragma once
#include <QObject>
#include <QPointer>
#include <QPropertyAnimation>
#include <QWidget>

class SizeTween : public QObject {
    Q_OBJECT
public:
    explicit SizeTween(QWidget* target, int durationMs = 220)
        : QObject(target), m_target(target)
    {
        m_ani = new QPropertyAnimation(target, "maximumHeight", this);
        m_ani->setDuration(durationMs);
    }

    void expandTo(int endHeight) {
        m_ani->stop();
        m_ani->setStartValue(m_target->maximumHeight());
        m_ani->setEndValue(endHeight);
        m_ani->start();
    }
    void collapseTo(int endHeight = 0) {
        expandTo(endHeight);
    }

private:
    QPointer<QWidget> m_target;
    QPropertyAnimation* m_ani = nullptr;
};
