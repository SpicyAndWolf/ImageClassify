#pragma once
#include <QDialog>
class LicenseManager;

class ActivationDialog : public QDialog {
  Q_OBJECT
public:
  explicit ActivationDialog(LicenseManager* lic, QWidget* parent=nullptr);
private slots:
  void onActivate();
  void onActivated();
  void onActivationError(const QString& reason);
private:
  LicenseManager* m_lic;
  class QLineEdit*   m_cdk;
  class QLabel*      m_mid;
  class QPushButton* m_btn;
  class QLabel*      m_tip;
};
