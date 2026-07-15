#include "SectionAssignmentDialog.hpp"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include <unordered_map>

namespace {

QString fromUtf8(const std::string& text) {
    return QString::fromUtf8(text.data(),
                             static_cast<qsizetype>(text.size()));
}

} // namespace

SectionAssignmentDialog::SectionAssignmentDialog(
    const QString& targetName, const QString& targetType,
    const QString& currentAssignment,
    const std::vector<emilcae::core::SolidSection>& sections,
    const std::vector<emilcae::core::Material>& materials,
    int selectedSectionId, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(tr("指派实体截面"));
    setModal(true);
    resize(440, 220);
    std::unordered_map<int, QString> materialNames;
    for (const emilcae::core::Material& material : materials) {
        materialNames.emplace(material.id, fromUtf8(material.name));
    }
    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    form->addRow(tr("目标对象："), new QLabel(targetName, this));
    form->addRow(tr("目标类型："), new QLabel(targetType, this));
    form->addRow(tr("当前指派："), new QLabel(currentAssignment, this));
    sectionCombo_ = new QComboBox(this);
    for (const emilcae::core::SolidSection& section : sections) {
        sectionCombo_->addItem(fromUtf8(section.name), section.id);
        sectionCombo_->setItemData(
            sectionCombo_->count() - 1,
            materialNames.contains(section.materialId)
                ? materialNames.at(section.materialId)
                : tr("未知材料"),
            Qt::UserRole + 1);
    }
    const int selectedIndex = sectionCombo_->findData(selectedSectionId);
    if (selectedIndex >= 0) {
        sectionCombo_->setCurrentIndex(selectedIndex);
    }
    form->addRow(tr("实体截面："), sectionCombo_);
    materialValue_ = new QLabel(this);
    form->addRow(tr("对应材料："), materialValue_);
    auto updateMaterial = [this] {
        materialValue_->setText(
            sectionCombo_->currentData(Qt::UserRole + 1).toString());
    };
    connect(sectionCombo_, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [updateMaterial](int) { updateMaterial(); });
    updateMaterial();
    layout->addLayout(form);
    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("确定"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    connect(buttons, &QDialogButtonBox::accepted,
            this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected,
            this, &QDialog::reject);
    layout->addWidget(buttons);
}

int SectionAssignmentDialog::sectionId() const {
    bool valid = false;
    const int id = sectionCombo_->currentData().toInt(&valid);
    return valid ? id : -1;
}
