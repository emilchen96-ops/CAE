#pragma once

#include "emilcae/core/DisplacementConstraint.hpp"
#include "emilcae/core/NamedSelection.hpp"

#include <QDialog>

#include <array>
#include <functional>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;

class DisplacementConstraintDialog final : public QDialog {
    Q_OBJECT

public:
    using NameAvailability = std::function<bool(
        const QString& name, int excludedConstraintId)>;

    DisplacementConstraintDialog(
        emilcae::core::ConstraintType constraintType,
        const std::vector<emilcae::core::NamedSelection>& namedSelections,
        NameAvailability nameAvailability,
        int editingConstraintId = -1,
        QWidget* parent = nullptr);

    void setConstraint(
        const emilcae::core::DisplacementConstraint& constraint);
    void setName(const QString& name);
    void setNamedSelectionId(int namedSelectionId);
    emilcae::core::DisplacementConstraint constraint() const;

protected:
    void accept() override;

private:
    struct DirectionWidgets {
        QCheckBox* constrained{nullptr};
        QDoubleSpinBox* value{nullptr};
        QComboBox* unit{nullptr};
        emilcae::core::DisplacementUnit currentUnit{
            emilcae::core::DisplacementUnit::Millimeter};
    };

    void configureDirection(int index, const QString& direction);
    void updateDirectionEnabled(int index);
    void updateSelectionInformation();
    void changeUnit(int index,
                    emilcae::core::DisplacementUnit newUnit);

    emilcae::core::ConstraintType constraintType_;
    std::vector<emilcae::core::NamedSelection> namedSelections_;
    NameAvailability nameAvailability_;
    int editingConstraintId_{-1};
    QLineEdit* nameEdit_{nullptr};
    QComboBox* namedSelectionCombo_{nullptr};
    QLabel* selectionTypeValue_{nullptr};
    QLabel* selectionCountValue_{nullptr};
    QLabel* selectionWarning_{nullptr};
    std::array<DirectionWidgets, 3> directions_;
};
