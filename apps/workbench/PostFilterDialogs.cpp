#include "PostFilterDialogs.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QMessageBox>
#include <QVBoxLayout>

#include <algorithm>

namespace {
QDoubleSpinBox* makeValueSpin(QWidget* parent) {
    auto* spin = new QDoubleSpinBox(parent);
    spin->setDecimals(10);
    spin->setRange(-1.0e300, 1.0e300);
    spin->setKeyboardTracking(false);
    return spin;
}
}

PlaneFilterDialog::PlaneFilterDialog(bool clipping, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(clipping ? tr("创建轴向剖切") : tr("创建平面切片"));
    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    axisCombo_ = new QComboBox(this);
    axisCombo_->addItems({tr("X 平面"), tr("Y 平面"), tr("Z 平面")});
    positionSpin_ = makeValueSpin(this);
    keepPositiveCheck_ = new QCheckBox(tr("保留法向正侧"), this);
    keepPositiveCheck_->setChecked(true);
    form->addRow(tr("方向"), axisCombo_);
    form->addRow(tr("位置"), positionSpin_);
    if (clipping) form->addRow(tr("保留方向"), keepPositiveCheck_);
    else keepPositiveCheck_->hide();
    layout->addLayout(form);
    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

PostFilterAxis PlaneFilterDialog::axis() const {
    return static_cast<PostFilterAxis>(axisCombo_->currentIndex());
}
double PlaneFilterDialog::position() const { return positionSpin_->value(); }
bool PlaneFilterDialog::keepPositive() const { return keepPositiveCheck_->isChecked(); }

ThresholdFilterDialog::ThresholdFilterDialog(
    const QStringList& fields, const QVector<QPair<double, double>>& ranges,
    int currentIndex, QWidget* parent)
    : QDialog(parent), ranges_(ranges) {
    setWindowTitle(tr("创建阈值过滤"));
    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    fieldCombo_ = new QComboBox(this);
    fieldCombo_->addItems(fields);
    minimumSpin_ = makeValueSpin(this);
    maximumSpin_ = makeValueSpin(this);
    form->addRow(tr("字段"), fieldCombo_);
    form->addRow(tr("最小值"), minimumSpin_);
    form->addRow(tr("最大值"), maximumSpin_);
    layout->addLayout(form);
    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this,
            &ThresholdFilterDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    connect(fieldCombo_, &QComboBox::currentIndexChanged,
            this, &ThresholdFilterDialog::updateRange);
    fieldCombo_->setCurrentIndex(std::clamp(
        currentIndex, 0, static_cast<int>(fields.size()) - 1));
    updateRange(fieldCombo_->currentIndex());
}

int ThresholdFilterDialog::fieldIndex() const { return fieldCombo_->currentIndex(); }
double ThresholdFilterDialog::minimum() const { return minimumSpin_->value(); }
double ThresholdFilterDialog::maximum() const { return maximumSpin_->value(); }

void ThresholdFilterDialog::updateRange(int index) {
    if (index < 0 || index >= ranges_.size()) return;
    minimumSpin_->setValue(ranges_[index].first);
    maximumSpin_->setValue(ranges_[index].second);
}

void ThresholdFilterDialog::accept() {
    if (minimum() > maximum()) {
        QMessageBox::warning(this, tr("阈值范围无效"),
                             tr("阈值最小值不能大于最大值。"));
        return;
    }
    QDialog::accept();
}
