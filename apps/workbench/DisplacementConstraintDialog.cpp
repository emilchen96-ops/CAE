#include "DisplacementConstraintDialog.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <utility>

namespace {

QString fromUtf8(const std::string& value) {
    return QString::fromUtf8(value.data(),
                             static_cast<qsizetype>(value.size()));
}

std::string toUtf8(const QString& value) {
    return value.toUtf8().toStdString();
}

QString entityTypeName(emilcae::core::NamedSelectionEntityType type) {
    using emilcae::core::NamedSelectionEntityType;
    switch (type) {
    case NamedSelectionEntityType::Object:
        return QObject::tr("对象");
    case NamedSelectionEntityType::Vertex:
        return QObject::tr("点");
    case NamedSelectionEntityType::Edge:
        return QObject::tr("边");
    case NamedSelectionEntityType::Face:
        return QObject::tr("面");
    case NamedSelectionEntityType::Solid:
        return QObject::tr("实体");
    }
    return {};
}

emilcae::core::DisplacementUnit unitFromCombo(const QComboBox* combo) {
    return combo->currentData().toInt() == 0
        ? emilcae::core::DisplacementUnit::Millimeter
        : emilcae::core::DisplacementUnit::Meter;
}

} // namespace

DisplacementConstraintDialog::DisplacementConstraintDialog(
    emilcae::core::ConstraintType constraintType,
    const std::vector<emilcae::core::NamedSelection>& namedSelections,
    NameAvailability nameAvailability, int editingConstraintId,
    QWidget* parent)
    : QDialog(parent),
      constraintType_(constraintType),
      namedSelections_(namedSelections),
      nameAvailability_(std::move(nameAvailability)),
      editingConstraintId_(editingConstraintId) {
    setWindowTitle(editingConstraintId >= 0
                       ? tr("编辑约束")
                       : constraintType == emilcae::core::ConstraintType::Fixed
                           ? tr("新建固定约束")
                           : tr("新建位移约束"));
    setModal(true);
    resize(560, 430);

    auto* mainLayout = new QVBoxLayout(this);
    auto* basicGroup = new QGroupBox(tr("基本信息"), this);
    auto* basicForm = new QFormLayout(basicGroup);
    nameEdit_ = new QLineEdit(basicGroup);
    basicForm->addRow(tr("约束名称："), nameEdit_);
    basicForm->addRow(
        tr("约束类型："),
        new QLabel(constraintType == emilcae::core::ConstraintType::Fixed
                       ? tr("固定约束")
                       : tr("位移约束"),
                   basicGroup));
    namedSelectionCombo_ = new QComboBox(basicGroup);
    for (const emilcae::core::NamedSelection& selection : namedSelections_) {
        namedSelectionCombo_->addItem(fromUtf8(selection.name), selection.id);
    }
    basicForm->addRow(tr("命名选择集："), namedSelectionCombo_);
    selectionTypeValue_ = new QLabel(basicGroup);
    selectionCountValue_ = new QLabel(basicGroup);
    basicForm->addRow(tr("选择集类型："), selectionTypeValue_);
    basicForm->addRow(tr("选择集成员数量："), selectionCountValue_);
    basicForm->addRow(tr("坐标系："), new QLabel(tr("全局坐标系"), basicGroup));
    selectionWarning_ = new QLabel(basicGroup);
    selectionWarning_->setWordWrap(true);
    selectionWarning_->setStyleSheet(QStringLiteral("color: #b45f06;"));
    basicForm->addRow(QString(), selectionWarning_);
    mainLayout->addWidget(basicGroup);

    auto* dofGroup = new QGroupBox(tr("平移自由度设置"), this);
    auto* dofGrid = new QGridLayout(dofGroup);
    dofGrid->addWidget(new QLabel(tr("方向"), dofGroup), 0, 0);
    dofGrid->addWidget(new QLabel(tr("是否约束"), dofGroup), 0, 1);
    dofGrid->addWidget(new QLabel(tr("位移值"), dofGroup), 0, 2);
    dofGrid->addWidget(new QLabel(tr("单位"), dofGroup), 0, 3);
    configureDirection(0, QStringLiteral("X"));
    configureDirection(1, QStringLiteral("Y"));
    configureDirection(2, QStringLiteral("Z"));
    for (int index = 0; index < 3; ++index) {
        dofGrid->addWidget(new QLabel(
                               index == 0 ? QStringLiteral("X")
                               : index == 1 ? QStringLiteral("Y")
                                            : QStringLiteral("Z"),
                               dofGroup),
                           index + 1, 0);
        dofGrid->addWidget(directions_[index].constrained, index + 1, 1);
        dofGrid->addWidget(directions_[index].value, index + 1, 2);
        dofGrid->addWidget(directions_[index].unit, index + 1, 3);
    }
    mainLayout->addWidget(dofGroup);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("确定"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    connect(buttons, &QDialogButtonBox::accepted,
            this, &DisplacementConstraintDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected,
            this, &QDialog::reject);
    mainLayout->addWidget(buttons);

    connect(namedSelectionCombo_,
            qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int) { updateSelectionInformation(); });
    updateSelectionInformation();
}

void DisplacementConstraintDialog::configureDirection(
    int index, const QString& direction) {
    Q_UNUSED(direction)
    DirectionWidgets& widgets = directions_[index];
    widgets.constrained = new QCheckBox(this);
    widgets.value = new QDoubleSpinBox(this);
    widgets.value->setDecimals(9);
    widgets.value->setRange(-1.0e12, 1.0e12);
    widgets.value->setSingleStep(1.0);
    widgets.unit = new QComboBox(this);
    widgets.unit->addItem(tr("mm"), 0);
    widgets.unit->addItem(tr("m"), 1);
    widgets.currentUnit = emilcae::core::DisplacementUnit::Millimeter;
    if (constraintType_ == emilcae::core::ConstraintType::Fixed) {
        widgets.constrained->setChecked(true);
        widgets.constrained->setEnabled(false);
        widgets.value->setValue(0.0);
        widgets.value->setReadOnly(true);
        widgets.unit->setEnabled(false);
    } else {
        widgets.constrained->setChecked(false);
        widgets.value->setEnabled(false);
        widgets.unit->setEnabled(false);
        connect(widgets.constrained, &QCheckBox::toggled,
                this, [this, index](bool) {
                    updateDirectionEnabled(index);
                });
        connect(widgets.unit,
                qOverload<int>(&QComboBox::currentIndexChanged),
                this, [this, index](int) {
                    changeUnit(index, unitFromCombo(directions_[index].unit));
                });
    }
}

void DisplacementConstraintDialog::setConstraint(
    const emilcae::core::DisplacementConstraint& constraint) {
    setName(fromUtf8(constraint.name));
    setNamedSelectionId(constraint.namedSelectionId);
    const std::array<emilcae::core::TranslationalDofConstraint, 3> dofs = {
        constraint.ux, constraint.uy, constraint.uz};
    for (int index = 0; index < 3; ++index) {
        DirectionWidgets& widgets = directions_[index];
        widgets.currentUnit = emilcae::core::DisplacementUnit::Millimeter;
        widgets.unit->setCurrentIndex(0);
        widgets.constrained->setChecked(dofs[index].constrained);
        widgets.value->setValue(
            emilcae::core::displacementFromMeters(
                dofs[index].value,
                emilcae::core::DisplacementUnit::Millimeter));
        updateDirectionEnabled(index);
    }
}

void DisplacementConstraintDialog::setName(const QString& name) {
    nameEdit_->setText(name);
}

void DisplacementConstraintDialog::setNamedSelectionId(
    int namedSelectionId) {
    const int index = namedSelectionCombo_->findData(namedSelectionId);
    if (index >= 0) {
        namedSelectionCombo_->setCurrentIndex(index);
    }
}

emilcae::core::DisplacementConstraint
DisplacementConstraintDialog::constraint() const {
    emilcae::core::DisplacementConstraint result;
    result.name = toUtf8(nameEdit_->text().trimmed());
    result.type = constraintType_;
    result.namedSelectionId = namedSelectionCombo_->currentData().toInt();
    std::array<emilcae::core::TranslationalDofConstraint*, 3> dofs = {
        &result.ux, &result.uy, &result.uz};
    for (int index = 0; index < 3; ++index) {
        const DirectionWidgets& widgets = directions_[index];
        dofs[index]->constrained = widgets.constrained->isChecked();
        dofs[index]->value = emilcae::core::displacementToMeters(
            widgets.value->value(), unitFromCombo(widgets.unit));
    }
    if (constraintType_ == emilcae::core::ConstraintType::Fixed) {
        result.ux = {true, 0.0};
        result.uy = {true, 0.0};
        result.uz = {true, 0.0};
    }
    return result;
}

void DisplacementConstraintDialog::accept() {
    const QString name = nameEdit_->text().trimmed();
    if (name.isEmpty()) {
        QMessageBox::warning(this, tr("约束校验失败"),
                             tr("约束名称不能为空。"));
        return;
    }
    if (nameAvailability_ &&
        !nameAvailability_(name, editingConstraintId_)) {
        QMessageBox::warning(this, tr("约束校验失败"),
                             tr("约束名称“%1”已经存在。").arg(name));
        return;
    }
    if (namedSelectionCombo_->currentIndex() < 0) {
        QMessageBox::warning(this, tr("约束校验失败"),
                             tr("必须选择一个有效命名选择集。"));
        return;
    }
    if (constraintType_ == emilcae::core::ConstraintType::Displacement &&
        !directions_[0].constrained->isChecked() &&
        !directions_[1].constrained->isChecked() &&
        !directions_[2].constrained->isChecked()) {
        QMessageBox::warning(this, tr("约束校验失败"),
                             tr("至少需要约束一个平移方向。"));
        return;
    }
    const auto value = constraint();
    if (!std::isfinite(value.ux.value) ||
        !std::isfinite(value.uy.value) ||
        !std::isfinite(value.uz.value)) {
        QMessageBox::warning(this, tr("约束校验失败"),
                             tr("位移值必须是有限数值。"));
        return;
    }
    QDialog::accept();
}

void DisplacementConstraintDialog::updateDirectionEnabled(int index) {
    DirectionWidgets& widgets = directions_[index];
    if (constraintType_ == emilcae::core::ConstraintType::Fixed) {
        return;
    }
    const bool enabled = widgets.constrained->isChecked();
    widgets.value->setEnabled(enabled);
    widgets.unit->setEnabled(enabled);
}

void DisplacementConstraintDialog::updateSelectionInformation() {
    const int id = namedSelectionCombo_->currentData().toInt();
    const auto iterator = std::find_if(
        namedSelections_.cbegin(), namedSelections_.cend(),
        [id](const emilcae::core::NamedSelection& selection) {
            return selection.id == id;
        });
    if (iterator == namedSelections_.cend()) {
        selectionTypeValue_->setText(tr("无"));
        selectionCountValue_->setText(QStringLiteral("0"));
        selectionWarning_->clear();
        return;
    }
    selectionTypeValue_->setText(entityTypeName(iterator->entityType));
    selectionCountValue_->setText(QString::number(iterator->items.size()));
    using emilcae::core::NamedSelectionEntityType;
    if (iterator->entityType == NamedSelectionEntityType::Object ||
        iterator->entityType == NamedSelectionEntityType::Solid) {
        selectionWarning_->setText(
            tr("该约束将作用于整个对象或实体，可能导致模型被完全约束。"));
    } else {
        selectionWarning_->clear();
    }
}

void DisplacementConstraintDialog::changeUnit(
    int index, emilcae::core::DisplacementUnit newUnit) {
    DirectionWidgets& widgets = directions_[index];
    if (newUnit == widgets.currentUnit) {
        return;
    }
    const double meters = emilcae::core::displacementToMeters(
        widgets.value->value(), widgets.currentUnit);
    widgets.currentUnit = newUnit;
    widgets.value->setValue(
        emilcae::core::displacementFromMeters(meters, newUnit));
}
