#pragma once

#include "MeshData.hpp"

#include <QString>
#include <QStringList>

#include <vector>

struct SphRigidPlaneDefinition {
    bool enabled{false};
    double originX{0.0};
    double originY{0.0};
    double originZ{0.0};
    double normalX{0.0};
    double normalY{0.0};
    double normalZ{1.0};
    double stiffness{0.0};
};

struct SphNodeSetConstraintDefinition {
    QString nodeSetName;
    QString constraintType;
};

struct SphNodalContactDefinition {
    bool enabled{false};
    double searchDistance{50.0};
    double scaleFactor{0.5};
    double penaltyStiffness{5000.0};
};

enum class SphContactType {
    None,
    FrictionlessPenalty
};

struct SphDynamicMaterialDefinition {
    QString name{QStringLiteral("Copper_OFFC")};
    double density{8960.0};
    double youngsModulus{117.0e9};
    double poissonRatio{0.35};
    double initialYieldStress{90.0e6};
    double referencePlasticStrain{0.002};
    double hardeningCoefficient{45.0e6};
    double hardeningExponent{0.307};
    double maximumStrainRate{1.0e5};
    double rateStressCoefficient{45.0e6};
    double strainRateCoefficient{0.01};
    double maximumFlowStress{1.0e12};
    double referenceStrainRate{1.0e-5};
    double referenceTemperature{293.0};
    double meltingTemperature{1356.0};
    double specificHeat{383.0};
    double thermalExpansion{2.3e-5};
    double thermalSofteningExponent{1.0};
    double taylorQuinneyCoefficient{0.9};
    double eosBulkModulus{94.77e9};
    double eosLinearShockSlope{1.489};
    double eosQuadraticShockSlope{0.0};
    double eosCubicShockSlope{0.0};
    double eosGruneisenGamma{2.02};
    double eosGammaVolumeCorrection{0.47};
    double eosEnergyCorrection{0.0};
    double eosReferenceSpecificEnergy{297.0};
};

struct SphJobSettings {
    QString jobName{QStringLiteral("SPH_Analysis")};
    QString jobDirectory;
    QString solverExecutable;
    double timeStepRatio{0.2};
    double simulationTime{80.0e-6};
    int outputInterval{200};
    double initialVelocityX{0.0};
    double initialVelocityY{0.0};
    double initialVelocityZ{-227.0};
    double artificialViscosityAlpha{0.01};
    double artificialViscosityBeta{0.1};
    double hourglassCoefficient{1.0};
    double searchRange{1.1};
    double searchExtension{1.5};
    double gamma{1.6};
    bool updateNeighborhood{true};
    double truncationError{1.0e-6};
    double massFactor{0.0};
    int criticalNeighborCount{8};
    double criticalStrain{0.2};
    int threadCount{1};
    bool lockingFree{false};
    bool adaptiveTimeStep{true};
    SphContactType contactType{SphContactType::None};
    int maximumSteps{0};
    bool preserveSourceNodeSets{true};
    SphDynamicMaterialDefinition material;
    std::vector<SphRigidPlaneDefinition> rigidPlanes;
    std::vector<SphNodeSetConstraintDefinition> nodeSetConstraints;
};

struct SphBodyJobInput {
    int meshObjectId{-1};
    QString bodyName;
    QString meshName;
    QString sourceMeshFilePath;
    bool sourceIsHmAscii{false};
    MeshData mesh;
    double translationX{0.0};
    double translationY{0.0};
    double translationZ{0.0};
    double initialVelocityX{0.0};
    double initialVelocityY{0.0};
    double initialVelocityZ{0.0};
    double energyReleaseRate{0.0};
    double artificialViscosityAlpha{0.01};
    double artificialViscosityBeta{0.1};
    double hourglassCoefficient{1.0};
    bool preserveSourceNodeSets{true};
    SphDynamicMaterialDefinition material;
    SphNodalContactDefinition nodalContact;
    std::vector<SphNodeSetConstraintDefinition> nodeSetConstraints;
};

struct SphMultiBodyJobSettings {
    QString jobName{QStringLiteral("SPH_Analysis")};
    QString jobDirectory;
    QString solverExecutable;
    double timeStepRatio{0.2};
    double simulationTime{80.0e-6};
    int outputInterval{200};
    double searchRange{1.1};
    double searchExtension{1.5};
    double gamma{1.6};
    bool updateNeighborhood{true};
    double truncationError{1.0e-6};
    double massFactor{0.0};
    int criticalNeighborCount{8};
    double criticalStrain{0.2};
    int threadCount{1};
    bool lockingFree{false};
    bool adaptiveTimeStep{true};
    SphContactType contactType{SphContactType::None};
    int maximumSteps{0};
    std::vector<SphRigidPlaneDefinition> rigidPlanes;
};

struct SphMultiBodyJobInput {
    SphMultiBodyJobSettings settings;
    std::vector<SphBodyJobInput> bodies;
};

struct SphJobInput {
    QString meshName;
    QString sourceMeshFilePath;
    bool sourceIsHmAscii{false};
    MeshData mesh;
    SphJobSettings settings;
};

struct SphJobWriteResult {
    bool success{false};
    QString errorMessage;
    QStringList warnings;
    QString configurationFilePath;
    QString outputDirectory;
};
