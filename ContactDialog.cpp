#include "ContactDialog.h"
#include <QFormLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QApplication>
#include <QClipboard>

ContactDialog::ContactDialog(const QString& name,
                             const QString& email,
                             const QString& phone,
                             QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("联系管理员"));
    setObjectName("ContactDialog");
    setModal(true);
    setMinimumWidth(420);

    m_nameEdit  = new QLineEdit(name, this);
    m_emailEdit = new QLineEdit(email, this);
    m_phoneEdit = new QLineEdit(phone, this);

    // 只读 + 鼠标可选中复制（SelectOnMousePress 让单击即全选，复制更顺手）
    for (auto* e : {m_nameEdit, m_emailEdit, m_phoneEdit}) {
        e->setReadOnly(true);
        e->setCursorPosition(0);
        e->setFocusPolicy(Qt::ClickFocus);
        e->setProperty("selectOnMousePress", true); // 用于样式或扩展
    }

    // 可选：旁边放一个“复制”按钮，提升可用性（鼠标选中复制本就可用）
    auto makeRow = [&](const QString& label, QLineEdit* edit) {
        auto* row = new QWidget(this);
        auto* hl  = new QHBoxLayout(row);
        hl->setContentsMargins(0,0,0,0);
        hl->addWidget(edit, /*stretch*/1);

        auto* copyBtn = new QPushButton(tr("复制"), row);
        copyBtn->setToolTip(tr("复制到剪贴板"));
        QObject::connect(copyBtn, &QPushButton::clicked, this, [edit]{
            edit->selectAll();
            QApplication::clipboard()->setText(edit->text());
        });
        hl->addWidget(copyBtn, 0);
        row->setLayout(hl);
        return std::pair<QString, QWidget*>{label, row};
    };

    auto [nLbl, nRow] = makeRow(tr("姓名"),  m_nameEdit);
    auto [eLbl, eRow] = makeRow(tr("邮箱"),  m_emailEdit);
    auto [pLbl, pRow] = makeRow(tr("电话"),  m_phoneEdit);

    auto* form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignRight);
    form->addRow(nLbl, nRow);
    form->addRow(eLbl, eRow);
    form->addRow(pLbl, pRow);

    m_closeBtn = new QPushButton(tr("关闭"), this);
    connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::accept);

    auto* root = new QVBoxLayout(this);
    root->addLayout(form);
    root->addSpacing(8);
    root->addWidget(m_closeBtn, 0, Qt::AlignRight);
    setLayout(root);

}

