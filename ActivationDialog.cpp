#include "ActivationDialog.h"
#include "LicenseManager.h"
#include <QVBoxLayout>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>

ActivationDialog::ActivationDialog(LicenseManager* lic, QWidget* parent)
  : QDialog(parent), m_lic(lic) {
  setWindowTitle(tr("激活"));
  setObjectName("ActivationDialog");
  setModal(true);
  auto* v = new QVBoxLayout(this);
  m_mid = new QLabel(tr("设备指纹：%1").arg(lic->machineId()), this);
  m_cdk = new QLineEdit(this); m_cdk->setPlaceholderText(tr("请输入激活码（CDK）"));
  m_btn = new QPushButton(tr("激活"), this);
  m_tip = new QLabel(this);
  m_tip->setObjectName("activateTip");
  v->addWidget(m_mid);
  v->addWidget(m_cdk);
  v->addWidget(m_btn);
  v->addWidget(m_tip);

  connect(m_btn, &QPushButton::clicked, this, &ActivationDialog::onActivate);
  connect(m_lic, &LicenseManager::activated, this, &ActivationDialog::onActivated);
  connect(m_lic, &LicenseManager::activationError, this, &ActivationDialog::onActivationError);
}

void ActivationDialog::onActivate() {
  m_btn->setEnabled(false);
  m_tip->clear();
  m_lic->activate(m_cdk->text().trimmed());
}

void ActivationDialog::onActivated() { accept(); }

void ActivationDialog::onActivationError(const QString& reason) {
  m_tip->setText(reason);
  m_btn->setEnabled(true);
}
