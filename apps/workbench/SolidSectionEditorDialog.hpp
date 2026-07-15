#pragma once

#include "emilcae/core/Material.hpp"

#include <QDialog>

#include <functional>
#include <vector>

class QComboBox;
class QLineEdit;

class SolidSectionEditorDialog final : public QDialog {
    Q_OBJECT

public:
    using NameAvailability =
        std::function<bool(const QString&, int excludedSectionId)>;

    SolidSectionEditorDialog(
        const std::vector<emilcae::core::Material>& materials,
        NameAvailability nameAvailability, int editingSectionId = -1,
        QWidget* parent = nullptr);

    void setValues(const QString& name, int materialId);
    QString sectionName() const;
    int materialId() const;

protected:
    void accept() override;

private:
    NameAvailability nameAvailability_;
    int editingSectionId_{-1};
    QLineEdit* nameEdit_{nullptr};
    QComboBox* materialCombo_{nullptr};
};
