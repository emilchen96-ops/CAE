#include "MaterialEditorDialog.hpp"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleValidator>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include <cmath>
#include <utility>

using emilcae::core::ElasticModulusUnit;
using emilcae::core::Material;

namespace {

ElasticModulusUnit selectedUnit(const QComboBox* combo) {
    switch (combo->currentIndex()) {
    case 0:
        return ElasticModulusUnit::Pascal;
    case 1:
        return ElasticModulusUnit::Megapascal;
    case 2:
        return ElasticModulusUnit::Gigapascal;
    default:
        return ElasticModulusUnit::Pascal;
    }
}

QDoubleValidator* numberValidator(QObject* parent) {
    auto* validator = new QDoubleValidator(parent);
    validator->setNotation(QDoubleValidator::ScientificNotation);
    validator->setLocale(QLocale::c());
    return validator;
}

} // namespace

MaterialEditorDialog::MaterialEditorDialog(
    NameAvailability nameAvailability, int editingMaterialId,
    QWidget* parent)
    : QDialog(parent),
      nameAvailability_(std::move(nameAvailability)),
      editingMaterialId_(editingMaterialId) {
    setWindowTitle(editingMaterialId_ >= 0 ? tr("编辑材料")
                                           : tr("新建材料"));
    setModal(true);
    resize(440, 280);

    auto* mainLayout = new QVBoxLayout(this);

    auto* basicGroup = new QGroupBox(tr("基本信息"), this);
    auto* basicForm = new QFormLayout(basicGroup);
    nameEdit_ = new QLineEdit(basicGroup);
    basicForm->addRow(tr("材料名称："), nameEdit_);
    mainLayout->addWidget(basicGroup);

    auto* propertiesGroup = new QGroupBox(tr("材料属性"), this);
    auto* propertiesForm = new QFormLayout(propertiesGroup);

    densityEdit_ = new QLineEdit(propertiesGroup);
    densityEdit_->setValidator(numberValidator(densityEdit_));
    auto* densityRow = new QWidget(propertiesGroup);
    auto* densityLayout = new QHBoxLayout(densityRow);
    densityLayout->setContentsMargins(0, 0, 0, 0);
    densityLayout->addWidget(densityEdit_);
    densityLayout->addWidget(new QLabel(tr("kg/m³"), densityRow));
    propertiesForm->addRow(tr("密度："), densityRow);

    youngsModulusEdit_ = new QLineEdit(propertiesGroup);
    youngsModulusEdit_->setValidator(numberValidator(youngsModulusEdit_));
    modulusUnitCombo_ = new QComboBox(propertiesGroup);
    modulusUnitCombo_->addItems({tr("Pa"), tr("MPa"), tr("GPa")});
    modulusUnitCombo_->setCurrentIndex(2);
    auto* modulusRow = new QWidget(propertiesGroup);
    auto* modulusLayout = new QHBoxLayout(modulusRow);
    modulusLayout->setContentsMargins(0, 0, 0, 0);
    modulusLayout->addWidget(youngsModulusEdit_);
    modulusLayout->addWidget(modulusUnitCombo_);
    propertiesForm->addRow(tr("弹性模量："), modulusRow);

    poissonRatioEdit_ = new QLineEdit(propertiesGroup);
    poissonRatioEdit_->setValidator(numberValidator(poissonRatioEdit_));
    propertiesForm->addRow(tr("泊松比："), poissonRatioEdit_);
    mainLayout->addWidget(propertiesGroup);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("保存"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    connect(buttons, &QDialogButtonBox::accepted,
            this, &MaterialEditorDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected,
            this, &MaterialEditorDialog::reject);
    mainLayout->addWidget(buttons);
}

void MaterialEditorDialog::setMaterial(const Material& material) {
    material_ = material;
    nameEdit_->setText(QString::fromUtf8(
        material.name.data(), static_cast<qsizetype>(material.name.size())));
    densityEdit_->setText(QString::number(material.density, 'g', 15));
    modulusUnitCombo_->setCurrentIndex(2);
    youngsModulusEdit_->setText(QString::number(
        emilcae::core::elasticModulusFromPascals(
            material.elasticity.youngsModulus,
            ElasticModulusUnit::Gigapascal),
        'g', 15));
    poissonRatioEdit_->setText(
        QString::number(material.elasticity.poissonRatio, 'g', 15));
}

Material MaterialEditorDialog::material() const {
    return material_;
}

void MaterialEditorDialog::accept() {
    const QString name = nameEdit_->text().trimmed();
    if (name.isEmpty()) {
        QMessageBox::warning(this, tr("材料校验失败"),
                             tr("材料名称不能为空。"));
        nameEdit_->setFocus();
        return;
    }
    if (nameAvailability_ &&
        !nameAvailability_(name, editingMaterialId_)) {
        QMessageBox::warning(
            this, tr("材料校验失败"),
            tr("材料名称“%1”已经存在，请使用其他名称。").arg(name));
        nameEdit_->setFocus();
        return;
    }

    double density = 0.0;
    if (!readFinitePositive(densityEdit_, density, tr("密度"))) {
        return;
    }
    double modulusValue = 0.0;
    if (!readFinitePositive(youngsModulusEdit_, modulusValue,
                            tr("弹性模量"))) {
        return;
    }
    if (modulusUnitCombo_->currentIndex() < 0 ||
        modulusUnitCombo_->currentIndex() > 2) {
        QMessageBox::warning(this, tr("材料校验失败"),
                             tr("弹性模量单位无效。"));
        return;
    }
    const double youngsModulus =
        emilcae::core::elasticModulusToPascals(
            modulusValue, selectedUnit(modulusUnitCombo_));
    if (!std::isfinite(youngsModulus) || youngsModulus <= 0.0) {
        QMessageBox::warning(this, tr("材料校验失败"),
                             tr("弹性模量换算结果无效。"));
        return;
    }

    bool poissonValid = false;
    const double poissonRatio = QLocale::c().toDouble(
        poissonRatioEdit_->text().trimmed(), &poissonValid);
    if (!poissonValid || !std::isfinite(poissonRatio)) {
        QMessageBox::warning(this, tr("材料校验失败"),
                             tr("泊松比必须是有效有限数值。"));
        poissonRatioEdit_->setFocus();
        return;
    }
    if (poissonRatio <= -1.0 || poissonRatio >= 0.5) {
        QMessageBox::warning(this, tr("材料校验失败"),
                             tr("泊松比必须满足 -1.0 < ν < 0.5。"));
        poissonRatioEdit_->setFocus();
        return;
    }
    if (emilcae::core::isNearlyIncompressible(poissonRatio) &&
        QMessageBox::warning(
            this, tr("泊松比警告"),
            tr("泊松比接近 0.5，后续位移型有限元计算可能出现近不可压缩问题。\n"
               "是否仍要保存？"),
            QMessageBox::Save | QMessageBox::Cancel,
            QMessageBox::Cancel) != QMessageBox::Save) {
        return;
    }

    material_.name = name.toUtf8().toStdString();
    material_.density = density;
    material_.elasticity.youngsModulus = youngsModulus;
    material_.elasticity.poissonRatio = poissonRatio;
    QDialog::accept();
}

bool MaterialEditorDialog::readFinitePositive(
    QLineEdit* editor, double& value, const QString& fieldName) {
    bool valid = false;
    value = QLocale::c().toDouble(editor->text().trimmed(), &valid);
    if (!valid || !std::isfinite(value)) {
        QMessageBox::warning(
            this, tr("材料校验失败"),
            tr("%1必须是有效有限数值。").arg(fieldName));
        editor->setFocus();
        return false;
    }
    if (value <= 0.0) {
        QMessageBox::warning(
            this, tr("材料校验失败"),
            tr("%1必须大于零。").arg(fieldName));
        editor->setFocus();
        return false;
    }
    return true;
}
