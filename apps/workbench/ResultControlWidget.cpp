#include "ResultControlWidget.hpp"

#include "ResultFieldProcessor.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QSignalBlocker>

#include <algorithm>
#include <array>

namespace {

QString fromUtf8(const std::string& value) {
    return QString::fromUtf8(value.data(),
                             static_cast<qsizetype>(value.size()));
}

bool isPreferredDisplacementName(const std::string& name) {
    static const std::array<std::string, 3> names{
        "Displacement", "DISPLACEMENT", "U"};
    return std::find(names.begin(), names.end(), name) != names.end();
}

} // namespace

ResultControlWidget::ResultControlWidget(QWidget* parent)
    : QWidget(parent) {
    auto* layout = new QFormLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);

    auto* frameContainer = new QWidget(this);
    auto* frameLayout = new QHBoxLayout(frameContainer);
    frameLayout->setContentsMargins(0, 0, 0, 0);
    previousFrameButton_ = new QPushButton(tr("上一帧"), frameContainer);
    frameCombo_ = new QComboBox(frameContainer);
    nextFrameButton_ = new QPushButton(tr("下一帧"), frameContainer);
    frameLayout->addWidget(previousFrameButton_);
    frameLayout->addWidget(frameCombo_, 1);
    frameLayout->addWidget(nextFrameButton_);

    fieldCombo_ = new QComboBox(this);
    componentCombo_ = new QComboBox(this);
    scalarCheck_ = new QCheckBox(tr("显示"), this);
    displacementCombo_ = new QComboBox(this);
    deformationCheck_ = new QCheckBox(tr("显示"), this);
    deformationScaleSpin_ = new QDoubleSpinBox(this);
    deformationScaleSpin_->setRange(0.0, 1.0e12);
    deformationScaleSpin_->setDecimals(6);
    deformationScaleSpin_->setValue(1.0);
    deformationScaleSpin_->setSingleStep(1.0);
    legendCheck_ = new QCheckBox(tr("显示"), this);
    legendCheck_->setChecked(true);

    layout->addRow(tr("结果帧："), frameContainer);
    layout->addRow(tr("结果字段："), fieldCombo_);
    layout->addRow(tr("显示分量："), componentCombo_);
    layout->addRow(tr("显示云图："), scalarCheck_);
    layout->addRow(tr("位移字段："), displacementCombo_);
    layout->addRow(tr("显示变形："), deformationCheck_);
    layout->addRow(tr("变形倍率："), deformationScaleSpin_);
    layout->addRow(tr("显示图例："), legendCheck_);

    connect(frameCombo_, &QComboBox::currentIndexChanged,
            this, [this](int index) {
                if (index >= 0) {
                    emit frameChangeRequested(
                        frameCombo_->itemData(index).toInt());
                }
            });
    connect(previousFrameButton_, &QPushButton::clicked,
            this, [this] {
                const int index = frameCombo_->currentIndex();
                if (index > 0) {
                    emit frameChangeRequested(
                        frameCombo_->itemData(index - 1).toInt());
                }
            });
    connect(nextFrameButton_, &QPushButton::clicked,
            this, [this] {
                const int index = frameCombo_->currentIndex();
                if (index >= 0 && index + 1 < frameCombo_->count()) {
                    emit frameChangeRequested(
                        frameCombo_->itemData(index + 1).toInt());
                }
            });
    connect(fieldCombo_, &QComboBox::currentIndexChanged,
            this, [this] {
                updateComponents();
                emit scalarSelectionChanged();
            });
    connect(componentCombo_, &QComboBox::currentIndexChanged,
            this, &ResultControlWidget::scalarSelectionChanged);
    connect(scalarCheck_, &QCheckBox::toggled,
            this, &ResultControlWidget::scalarVisibilityChanged);
    connect(displacementCombo_, &QComboBox::currentIndexChanged,
            this, [this] {
                const bool hasDisplacement =
                    displacementCombo_->currentData().toInt() >= 0;
                deformationCheck_->setEnabled(hasDisplacement);
                if (!hasDisplacement) {
                    const QSignalBlocker blocker(deformationCheck_);
                    deformationCheck_->setChecked(false);
                }
                emit displacementFieldChanged();
            });
    connect(deformationCheck_, &QCheckBox::toggled,
            this, &ResultControlWidget::deformationVisibilityChanged);
    connect(deformationScaleSpin_, &QDoubleSpinBox::editingFinished,
            this, [this] {
                emit deformationScaleChanged(
                    deformationScaleSpin_->value());
            });
    connect(legendCheck_, &QCheckBox::toggled,
            this, &ResultControlWidget::legendVisibilityChanged);
    clearResult();
    clearSequence();
}

void ResultControlWidget::setResultFields(
    const std::vector<ResultFieldInfo>& fields) {
    fields_ = fields;
    scalarOptions_.clear();
    scalarOptions_.reserve(fields_.size());
    const ResultFieldProcessor processor;
    for (const ResultFieldInfo& field : fields_) {
        scalarOptions_.push_back(processor.scalarOptions(field));
    }

    const QSignalBlocker fieldBlocker(fieldCombo_);
    const QSignalBlocker componentBlocker(componentCombo_);
    const QSignalBlocker scalarBlocker(scalarCheck_);
    const QSignalBlocker displacementBlocker(displacementCombo_);
    const QSignalBlocker deformationBlocker(deformationCheck_);
    const QSignalBlocker legendBlocker(legendCheck_);

    fieldCombo_->clear();
    int defaultField = -1;
    for (int index = 0; index < static_cast<int>(fields_.size());
         ++index) {
        const ResultFieldInfo& field =
            fields_[static_cast<std::size_t>(index)];
        const QString association =
            field.association == ResultFieldAssociation::Point
            ? tr("节点")
            : tr("单元");
        fieldCombo_->addItem(
            tr("%1（%2）").arg(fromUtf8(field.name), association),
            index);
        if (defaultField < 0 && field.componentCount == 1 &&
            field.association == ResultFieldAssociation::Point) {
            defaultField = index;
        }
    }
    if (defaultField < 0) {
        for (int index = 0; index < static_cast<int>(fields_.size());
             ++index) {
            if (fields_[static_cast<std::size_t>(index)]
                    .componentCount == 1) {
                defaultField = index;
                break;
            }
        }
    }
    if (defaultField < 0 && !fields_.empty()) {
        defaultField = 0;
    }
    fieldCombo_->setCurrentIndex(defaultField);
    updateComponents();

    displacementCombo_->clear();
    displacementCombo_->addItem(tr("无"), -1);
    int preferredDisplacementComboIndex = 0;
    for (int index = 0; index < static_cast<int>(fields_.size());
         ++index) {
        const ResultFieldInfo& field =
            fields_[static_cast<std::size_t>(index)];
        if (field.association != ResultFieldAssociation::Point ||
            field.componentCount != 3) {
            continue;
        }
        displacementCombo_->addItem(fromUtf8(field.name), index);
        if (isPreferredDisplacementName(field.name)) {
            preferredDisplacementComboIndex =
                displacementCombo_->count() - 1;
        }
    }
    displacementCombo_->setCurrentIndex(
        preferredDisplacementComboIndex);
    scalarCheck_->setChecked(defaultField >= 0);
    deformationCheck_->setChecked(false);
    deformationScaleSpin_->setValue(1.0);
    legendCheck_->setChecked(true);
    setEnabled(!fields_.empty());
    deformationCheck_->setEnabled(
        displacementCombo_->currentData().toInt() >= 0);
}

void ResultControlWidget::clearResult() {
    const QSignalBlocker fieldBlocker(fieldCombo_);
    const QSignalBlocker componentBlocker(componentCombo_);
    const QSignalBlocker scalarBlocker(scalarCheck_);
    const QSignalBlocker displacementBlocker(displacementCombo_);
    const QSignalBlocker deformationBlocker(deformationCheck_);
    const QSignalBlocker legendBlocker(legendCheck_);
    fieldCombo_->clear();
    componentCombo_->clear();
    displacementCombo_->clear();
    fields_.clear();
    scalarOptions_.clear();
    scalarCheck_->setChecked(false);
    deformationCheck_->setChecked(false);
    deformationScaleSpin_->setValue(1.0);
    legendCheck_->setChecked(true);
    setEnabled(false);
}

void ResultControlWidget::setSequenceFrames(
    const std::vector<int>& frameNumbers, int currentFrame) {
    setEnabled(true);
    const QSignalBlocker blocker(frameCombo_);
    frameCombo_->clear();
    for (const int frameNumber : frameNumbers) {
        frameCombo_->addItem(tr("帧 %1").arg(frameNumber), frameNumber);
    }
    const bool hasMultipleFrames = frameCombo_->count() > 1;
    frameCombo_->setEnabled(!frameNumbers.empty());
    previousFrameButton_->setEnabled(hasMultipleFrames);
    nextFrameButton_->setEnabled(hasMultipleFrames);
    selectFrame(currentFrame);
}

void ResultControlWidget::clearSequence() {
    const QSignalBlocker blocker(frameCombo_);
    frameCombo_->clear();
    frameCombo_->setEnabled(false);
    previousFrameButton_->setEnabled(false);
    nextFrameButton_->setEnabled(false);
}

std::optional<ResultScalarOption>
ResultControlWidget::selectedScalarOption() const {
    const int fieldIndex = fieldCombo_->currentData().toInt();
    const int optionIndex = componentCombo_->currentData().toInt();
    if (fieldIndex < 0 ||
        fieldIndex >= static_cast<int>(scalarOptions_.size())) {
        return std::nullopt;
    }
    const auto& options =
        scalarOptions_[static_cast<std::size_t>(fieldIndex)];
    if (optionIndex < 0 ||
        optionIndex >= static_cast<int>(options.size())) {
        return std::nullopt;
    }
    return options[static_cast<std::size_t>(optionIndex)];
}

std::string ResultControlWidget::selectedDisplacementField() const {
    const int fieldIndex = displacementCombo_->currentData().toInt();
    if (fieldIndex < 0 ||
        fieldIndex >= static_cast<int>(fields_.size())) {
        return {};
    }
    return fields_[static_cast<std::size_t>(fieldIndex)].name;
}

bool ResultControlWidget::scalarVisible() const {
    return scalarCheck_->isChecked();
}

bool ResultControlWidget::deformationVisible() const {
    return deformationCheck_->isChecked();
}

bool ResultControlWidget::legendVisible() const {
    return legendCheck_->isChecked();
}

double ResultControlWidget::deformationScale() const {
    return deformationScaleSpin_->value();
}

bool ResultControlWidget::selectScalarOption(
    const ResultScalarOption& option) {
    for (int fieldIndex = 0;
         fieldIndex < static_cast<int>(scalarOptions_.size());
         ++fieldIndex) {
        const auto& options =
            scalarOptions_[static_cast<std::size_t>(fieldIndex)];
        for (int optionIndex = 0;
             optionIndex < static_cast<int>(options.size());
             ++optionIndex) {
            const ResultScalarOption& candidate =
                options[static_cast<std::size_t>(optionIndex)];
            if (candidate.sourceArrayName != option.sourceArrayName ||
                candidate.association != option.association ||
                candidate.operation != option.operation ||
                candidate.component != option.component) {
                continue;
            }
            const QSignalBlocker fieldBlocker(fieldCombo_);
            fieldCombo_->setCurrentIndex(fieldIndex);
            updateComponents();
            const QSignalBlocker componentBlocker(componentCombo_);
            componentCombo_->setCurrentIndex(optionIndex);
            const QSignalBlocker scalarBlocker(scalarCheck_);
            scalarCheck_->setChecked(true);
            return true;
        }
    }
    return false;
}

bool ResultControlWidget::selectDisplacementField(
    const std::string& name) {
    if (name.empty()) {
        const QSignalBlocker blocker(displacementCombo_);
        displacementCombo_->setCurrentIndex(
            displacementCombo_->count() > 0 ? 0 : -1);
        deformationCheck_->setEnabled(false);
        return true;
    }
    for (int index = 0; index < displacementCombo_->count(); ++index) {
        if (fromUtf8(name) != displacementCombo_->itemText(index)) {
            continue;
        }
        const QSignalBlocker blocker(displacementCombo_);
        displacementCombo_->setCurrentIndex(index);
        deformationCheck_->setEnabled(true);
        return true;
    }
    return false;
}

void ResultControlWidget::setScalarVisible(bool visible) {
    const QSignalBlocker blocker(scalarCheck_);
    scalarCheck_->setChecked(visible);
}

void ResultControlWidget::setDeformationChecked(bool checked) {
    const QSignalBlocker blocker(deformationCheck_);
    deformationCheck_->setChecked(checked);
}

void ResultControlWidget::setDeformationScaleValue(double scale) {
    const QSignalBlocker blocker(deformationScaleSpin_);
    deformationScaleSpin_->setValue(scale);
}

void ResultControlWidget::setLegendChecked(bool checked) {
    const QSignalBlocker blocker(legendCheck_);
    legendCheck_->setChecked(checked);
}

bool ResultControlWidget::selectFrame(int frameNumber) {
    for (int index = 0; index < frameCombo_->count(); ++index) {
        if (frameCombo_->itemData(index).toInt() != frameNumber) {
            continue;
        }
        const QSignalBlocker blocker(frameCombo_);
        frameCombo_->setCurrentIndex(index);
        previousFrameButton_->setEnabled(index > 0);
        nextFrameButton_->setEnabled(index + 1 < frameCombo_->count());
        return true;
    }
    return false;
}

void ResultControlWidget::updateComponents() {
    const QSignalBlocker blocker(componentCombo_);
    componentCombo_->clear();
    const int fieldIndex = fieldCombo_->currentData().toInt();
    if (fieldIndex < 0 ||
        fieldIndex >= static_cast<int>(scalarOptions_.size())) {
        return;
    }
    const auto& options =
        scalarOptions_[static_cast<std::size_t>(fieldIndex)];
    for (int optionIndex = 0;
         optionIndex < static_cast<int>(options.size());
         ++optionIndex) {
        componentCombo_->addItem(
            fromUtf8(options[static_cast<std::size_t>(optionIndex)]
                         .displayName),
            optionIndex);
    }
    componentCombo_->setCurrentIndex(options.empty() ? -1 : 0);
}
