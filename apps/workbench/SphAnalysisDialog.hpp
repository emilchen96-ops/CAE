#pragma once

#include "SphJobDefinition.hpp"

#include <QDialog>

class QCheckBox;
class QDoubleSpinBox;
class QLineEdit;
class QSpinBox;

class SphAnalysisDialog final : public QDialog {
    Q_OBJECT

public:
    SphAnalysisDialog(const QString& meshName, bool sourceIsHmAscii,
                      const SphDynamicMaterialDefinition& initialMaterial,
                      QWidget* parent = nullptr);

    SphJobSettings settings() const;
    static QString discoverSolverExecutable();

protected:
    void accept() override;

private:
    static std::vector<SphNodeSetConstraintDefinition>
    defaultNodeSetConstraints();

    QLineEdit* jobNameEdit_{nullptr};
    QLineEdit* jobDirectoryEdit_{nullptr};
    QLineEdit* solverExecutableEdit_{nullptr};
    QDoubleSpinBox* simulationTimeSpin_{nullptr};
    QDoubleSpinBox* timeStepRatioSpin_{nullptr};
    QSpinBox* outputIntervalSpin_{nullptr};
    QDoubleSpinBox* velocityXSpin_{nullptr};
    QDoubleSpinBox* velocityYSpin_{nullptr};
    QDoubleSpinBox* velocityZSpin_{nullptr};

    QLineEdit* materialNameEdit_{nullptr};
    QDoubleSpinBox* densitySpin_{nullptr};
    QDoubleSpinBox* youngsModulusSpin_{nullptr};
    QDoubleSpinBox* poissonRatioSpin_{nullptr};
    QDoubleSpinBox* yieldStressSpin_{nullptr};
    QDoubleSpinBox* referencePlasticStrainSpin_{nullptr};
    QDoubleSpinBox* hardeningCoefficientSpin_{nullptr};
    QDoubleSpinBox* hardeningExponentSpin_{nullptr};
    QDoubleSpinBox* strainRateCoefficientSpin_{nullptr};
    QDoubleSpinBox* referenceStrainRateSpin_{nullptr};
    QDoubleSpinBox* referenceTemperatureSpin_{nullptr};
    QDoubleSpinBox* meltingTemperatureSpin_{nullptr};
    QDoubleSpinBox* specificHeatSpin_{nullptr};
    QDoubleSpinBox* thermalExponentSpin_{nullptr};
    QDoubleSpinBox* taylorQuinneySpin_{nullptr};
    QDoubleSpinBox* eosBulkModulusSpin_{nullptr};
    QDoubleSpinBox* eosShockSlopeSpin_{nullptr};
    QDoubleSpinBox* eosGammaSpin_{nullptr};

    QCheckBox* preserveNodeSetsCheck_{nullptr};
    QCheckBox* zPlaneCheck_{nullptr};
    QCheckBox* xPlaneCheck_{nullptr};
    QCheckBox* yPlaneCheck_{nullptr};
    QDoubleSpinBox* zPlaneStiffnessSpin_{nullptr};
    QDoubleSpinBox* xPlaneStiffnessSpin_{nullptr};
    QDoubleSpinBox* yPlaneStiffnessSpin_{nullptr};
    QDoubleSpinBox* artificialAlphaSpin_{nullptr};
    QDoubleSpinBox* artificialBetaSpin_{nullptr};
    QDoubleSpinBox* hourglassSpin_{nullptr};
};
