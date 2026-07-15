#include "SolidSectionEditorDialog.hpp"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include <utility>

SolidSectionEditorDialog::SolidSectionEditorDialog(
    const std::vector<emilcae::core::Material>& materials,
    NameAvailability nameAvailability, int editingSectionId,
    QWidget* parent)
    : QDialog(parent),
      nameAvailability_(std::move(nameAvailability)),
      editingSectionId_(editingSectionId) {
    setWindowTitle(editingSectionId >= 0 ? tr("编辑实体截面")
                                         : tr("新建实体截面"));
    setModal(true);
    resize(420, 190);
    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    nameEdit_ = new QLineEdit(this);
    form->addRow(tr("截面名称："), nameEdit_);
    form->addRow(tr("截面类型："), new QLabel(tr("实体截面"), this));
    materialCombo_ = new QComboBox(this);
    for (const emilcae::core::Material& material : materials) {
        materialCombo_->addItem(
            QString::fromUtf8(material.name.data(),
                              static_cast<qsizetype>(material.name.size())),
            material.id);
    }
    form->addRow(tr("材料："), materialCombo_);
    layout->addLayout(form);
    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("确定"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    connect(buttons, &QDialogButtonBox::accepted,
            this, &SolidSectionEditorDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected,
            this, &SolidSectionEditorDialog::reject);
    layout->addWidget(buttons);
}

void SolidSectionEditorDialog::setValues(const QString& name,
                                         int materialId) {
    nameEdit_->setText(name);
    const int index = materialCombo_->findData(materialId);
    if (index >= 0) {
        materialCombo_->setCurrentIndex(index);
    }
}

QString SolidSectionEditorDialog::sectionName() const {
    return nameEdit_->text().trimmed();
}

int SolidSectionEditorDialog::materialId() const {
    bool valid = false;
    const int id = materialCombo_->currentData().toInt(&valid);
    return valid ? id : -1;
}

void SolidSectionEditorDialog::accept() {
    const QString name = sectionName();
    if (name.isEmpty()) {
        QMessageBox::warning(this, tr("截面校验失败"),
                             tr("截面名称不能为空。"));
        return;
    }
    if (nameAvailability_ &&
        !nameAvailability_(name, editingSectionId_)) {
        QMessageBox::warning(this, tr("截面校验失败"),
                             tr("实体截面名称“%1”已经存在。").arg(name));
        return;
    }
    if (materialId() <= 0) {
        QMessageBox::warning(this, tr("截面校验失败"),
                             tr("必须选择一个有效材料。"));
        return;
    }
    QDialog::accept();
}
