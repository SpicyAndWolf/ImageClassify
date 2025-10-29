#pragma once
#include <QDialog>

class QLineEdit;
class QPushButton;

class ContactDialog : public QDialog {
    Q_OBJECT
public:
    explicit ContactDialog(const QString& name,
                           const QString& email,
                           const QString& phone,
                           QWidget* parent = nullptr);
private:
    QLineEdit* m_nameEdit {nullptr};
    QLineEdit* m_emailEdit {nullptr};
    QLineEdit* m_phoneEdit {nullptr};
    QPushButton* m_closeBtn {nullptr};

    void applyModernStyle(); // QSS
};
