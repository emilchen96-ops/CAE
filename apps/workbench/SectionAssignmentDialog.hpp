#pragma once

#include "emilcae/core/Material.hpp"
#include "emilcae/core/SolidSection.hpp"

#include <QDialog>

#include <vector>

class QComboBox;
class QLabel;

class SectionAssignmentDialog final : public QDialog {
    Q_OBJECT

public:
    SectionAssignmentDialog(
        const QString& targetName, const QString& targetType,
        const QString& currentAssignment,
        const std::vector<emilcae::core::SolidSection>& sections,
        const std::vector<emilcae::core::Material>& materials,
        int selectedSectionId = -1, QWidget* parent = nullptr);

    int sectionId() const;

private:
    QComboBox* sectionCombo_{nullptr};
    QLabel* materialValue_{nullptr};
};
