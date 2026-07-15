#pragma once

#include "emilcae/core/Material.hpp"

#include <QDialog>

#include <functional>

class QComboBox;
class QLineEdit;

class MaterialEditorDialog final : public QDialog {
    Q_OBJECT

public:
    using NameAvailability =
        std::function<bool(const QString& name, int editingMaterialId)>;

    explicit MaterialEditorDialog(NameAvailability nameAvailability,
                                  int editingMaterialId = -1,
                                  QWidget* parent = nullptr);

    void setMaterial(const emilcae::core::Material& material);
    emilcae::core::Material material() const;

protected:
    void accept() override;

private:
    bool readFinitePositive(QLineEdit* editor, double& value,
                            const QString& fieldName);

    NameAvailability nameAvailability_;
    int editingMaterialId_{-1};
    emilcae::core::Material material_;
    QLineEdit* nameEdit_{nullptr};
    QLineEdit* densityEdit_{nullptr};
    QLineEdit* youngsModulusEdit_{nullptr};
    QComboBox* modulusUnitCombo_{nullptr};
    QLineEdit* poissonRatioEdit_{nullptr};
};
