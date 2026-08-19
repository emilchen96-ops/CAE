#include "SphAnalysisDialog.hpp"

#include <QCheckBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

namespace {

QDoubleSpinBox* numberSpin(double minimum, double maximum,
                           double value, int decimals,
                           const QString& suffix = {}) {
    auto* spin = new QDoubleSpinBox;
    spin->setRange(minimum, maximum);
    spin->setDecimals(decimals);
    spin->setValue(std::clamp(value, minimum, maximum));
    spin->setSuffix(suffix);
    spin->setKeyboardTracking(false);
    return spin;
}

QWidget* pathEditor(QLineEdit*& edit, const QString& buttonText,
                    const std::function<void()>& browse) {
    auto* container = new QWidget;
    auto* layout = new QHBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    edit = new QLineEdit(container);
    auto* button = new QPushButton(buttonText, container);
    layout->addWidget(edit, 1);
    layout->addWidget(button);
    QObject::connect(button, &QPushButton::clicked, container, browse);
    return container;
}

} // namespace

SphAnalysisDialog::SphAnalysisDialog(
    const QString& meshName, bool sourceIsHmAscii,
    const SphDynamicMaterialDefinition& initialMaterial, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(tr("设置并运行 SPH 分析"));
    resize(720, 720);
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->addWidget(new QLabel(
        tr("当前网格：%1\n求解器将作为独立进程运行，完成后自动加载 VTU 结果序列。")
            .arg(meshName), this));

    auto* tabs = new QTabWidget(this);
    rootLayout->addWidget(tabs, 1);

    auto* jobPage = new QWidget(tabs);
    auto* jobForm = new QFormLayout(jobPage);
    jobNameEdit_ = new QLineEdit(
        QStringLiteral("Taylor_Impact_%1")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"))),
        jobPage);
    jobForm->addRow(tr("作业名称"), jobNameEdit_);
    QWidget* directoryEditor = pathEditor(
        jobDirectoryEdit_, tr("浏览..."), [this] {
            const QString selected = QFileDialog::getExistingDirectory(
                this, tr("选择 SPH 作业根目录"), jobDirectoryEdit_->text());
            if (!selected.isEmpty()) {
                jobDirectoryEdit_->setText(selected);
            }
        });
    const QString documents = QStandardPaths::writableLocation(
        QStandardPaths::DocumentsLocation);
    jobDirectoryEdit_->setText(
        QDir(documents).filePath(QStringLiteral("QTCAE/SPH_Jobs")));
    jobForm->addRow(tr("作业根目录"), directoryEditor);
    QWidget* solverEditor = pathEditor(
        solverExecutableEdit_, tr("浏览..."), [this] {
            const QString selected = QFileDialog::getOpenFileName(
                this, tr("选择 SPH 求解器"), solverExecutableEdit_->text(),
                tr("SPH 求解器 (sphSolver.exe);;可执行文件 (*.exe);;所有文件 (*.*)"));
            if (!selected.isEmpty()) {
                solverExecutableEdit_->setText(selected);
            }
        });
    solverExecutableEdit_->setText(discoverSolverExecutable());
    jobForm->addRow(tr("求解器路径"), solverEditor);
    simulationTimeSpin_ = numberSpin(0.001, 1.0e9, 80.0, 6, tr(" μs"));
    timeStepRatioSpin_ = numberSpin(0.001, 1.0, 0.2, 4);
    outputIntervalSpin_ = new QSpinBox(jobPage);
    outputIntervalSpin_->setRange(1, 100000000);
    outputIntervalSpin_->setValue(200);
    velocityXSpin_ = numberSpin(-1.0e7, 1.0e7, 0.0, 6, tr(" m/s"));
    velocityYSpin_ = numberSpin(-1.0e7, 1.0e7, 0.0, 6, tr(" m/s"));
    velocityZSpin_ = numberSpin(-1.0e7, 1.0e7, -227.0, 6, tr(" m/s"));
    jobForm->addRow(tr("仿真时间"), simulationTimeSpin_);
    jobForm->addRow(tr("时间步比例"), timeStepRatioSpin_);
    jobForm->addRow(tr("结果输出间隔（步）"), outputIntervalSpin_);
    jobForm->addRow(tr("初速度 X"), velocityXSpin_);
    jobForm->addRow(tr("初速度 Y"), velocityYSpin_);
    jobForm->addRow(tr("初速度 Z"), velocityZSpin_);
    tabs->addTab(jobPage, tr("作业"));

    auto* materialPage = new QWidget(tabs);
    auto* materialForm = new QFormLayout(materialPage);
    materialNameEdit_ = new QLineEdit(initialMaterial.name, materialPage);
    densitySpin_ = numberSpin(1.0, 1.0e6, initialMaterial.density, 6, tr(" kg/m³"));
    youngsModulusSpin_ = numberSpin(1.0e-9, 1.0e6,
                                    initialMaterial.youngsModulus / 1.0e9,
                                    6, tr(" GPa"));
    poissonRatioSpin_ = numberSpin(-0.99, 0.4999,
                                   initialMaterial.poissonRatio, 6);
    yieldStressSpin_ = numberSpin(1.0e-9, 1.0e9,
                                  initialMaterial.initialYieldStress / 1.0e6,
                                  6, tr(" MPa"));
    referencePlasticStrainSpin_ = numberSpin(
        1.0e-12, 1.0e6, initialMaterial.referencePlasticStrain, 8);
    hardeningCoefficientSpin_ = numberSpin(
        0.0, 1.0e9, initialMaterial.hardeningCoefficient / 1.0e6,
        6, tr(" MPa"));
    hardeningExponentSpin_ = numberSpin(
        1.0e-9, 100.0, initialMaterial.hardeningExponent, 6);
    strainRateCoefficientSpin_ = numberSpin(
        0.0, 100.0, initialMaterial.strainRateCoefficient, 8);
    referenceStrainRateSpin_ = numberSpin(
        1.0e-12, 1.0e12, initialMaterial.referenceStrainRate, 8, tr(" 1/s"));
    referenceTemperatureSpin_ = numberSpin(
        0.0, 1.0e5, initialMaterial.referenceTemperature, 3, tr(" K"));
    meltingTemperatureSpin_ = numberSpin(
        0.0, 1.0e5, initialMaterial.meltingTemperature, 3, tr(" K"));
    specificHeatSpin_ = numberSpin(
        1.0e-9, 1.0e9, initialMaterial.specificHeat, 6, tr(" J/(kg·K)"));
    thermalExponentSpin_ = numberSpin(
        1.0e-9, 100.0, initialMaterial.thermalSofteningExponent, 6);
    taylorQuinneySpin_ = numberSpin(
        0.0, 1.0, initialMaterial.taylorQuinneyCoefficient, 6);
    materialForm->addRow(tr("材料名称"), materialNameEdit_);
    materialForm->addRow(tr("密度"), densitySpin_);
    materialForm->addRow(tr("杨氏模量"), youngsModulusSpin_);
    materialForm->addRow(tr("泊松比"), poissonRatioSpin_);
    materialForm->addRow(tr("初始屈服应力"), yieldStressSpin_);
    materialForm->addRow(tr("参考塑性应变"), referencePlasticStrainSpin_);
    materialForm->addRow(tr("硬化系数"), hardeningCoefficientSpin_);
    materialForm->addRow(tr("硬化指数"), hardeningExponentSpin_);
    materialForm->addRow(tr("应变率系数"), strainRateCoefficientSpin_);
    materialForm->addRow(tr("参考应变率"), referenceStrainRateSpin_);
    materialForm->addRow(tr("参考温度"), referenceTemperatureSpin_);
    materialForm->addRow(tr("熔化温度"), meltingTemperatureSpin_);
    materialForm->addRow(tr("比热容"), specificHeatSpin_);
    materialForm->addRow(tr("热软化指数"), thermalExponentSpin_);
    materialForm->addRow(tr("Taylor-Quinney 系数"), taylorQuinneySpin_);
    tabs->addTab(materialPage, tr("J2 材料"));

    auto* eosPage = new QWidget(tabs);
    auto* eosForm = new QFormLayout(eosPage);
    eosBulkModulusSpin_ = numberSpin(
        1.0e-9, 1.0e6, initialMaterial.eosBulkModulus / 1.0e9,
        6, tr(" GPa"));
    eosShockSlopeSpin_ = numberSpin(
        0.0, 1000.0, initialMaterial.eosLinearShockSlope, 8);
    eosGammaSpin_ = numberSpin(
        0.0, 1000.0, initialMaterial.eosGruneisenGamma, 8);
    eosForm->addRow(tr("参考体积模量"), eosBulkModulusSpin_);
    eosForm->addRow(tr("线性冲击斜率"), eosShockSlopeSpin_);
    eosForm->addRow(tr("Grüneisen 系数"), eosGammaSpin_);
    eosForm->addRow(new QLabel(
        tr("第一版固定二次/三次冲击斜率和体积修正为 OFHC 铜模板值。"),
        eosPage));
    tabs->addTab(eosPage, tr("状态方程"));

    auto* boundaryPage = new QWidget(tabs);
    auto* boundaryLayout = new QVBoxLayout(boundaryPage);
    preserveNodeSetsCheck_ = new QCheckBox(
        tr("保留源 HMASCII 节点集并映射固定/对称约束"), boundaryPage);
    preserveNodeSetsCheck_->setChecked(sourceIsHmAscii);
    preserveNodeSetsCheck_->setEnabled(sourceIsHmAscii);
    boundaryLayout->addWidget(preserveNodeSetsCheck_);
    auto* planes = new QGroupBox(tr("轴对齐无限刚性平面"), boundaryPage);
    auto* planeGrid = new QGridLayout(planes);
    planeGrid->addWidget(new QLabel(tr("平面")), 0, 0);
    planeGrid->addWidget(new QLabel(tr("启用")), 0, 1);
    planeGrid->addWidget(new QLabel(tr("刚度")), 0, 2);
    zPlaneCheck_ = new QCheckBox(planes);
    xPlaneCheck_ = new QCheckBox(planes);
    yPlaneCheck_ = new QCheckBox(planes);
    zPlaneCheck_->setChecked(true);
    xPlaneCheck_->setChecked(true);
    yPlaneCheck_->setChecked(true);
    zPlaneStiffnessSpin_ = numberSpin(1.0e-9, 1.0e9, 5.0, 6, tr(" GPa"));
    xPlaneStiffnessSpin_ = numberSpin(1.0e-9, 1.0e9, 1.0, 6, tr(" GPa"));
    yPlaneStiffnessSpin_ = numberSpin(1.0e-9, 1.0e9, 1.0, 6, tr(" GPa"));
    planeGrid->addWidget(new QLabel(tr("Z=0，法向 +Z")), 1, 0);
    planeGrid->addWidget(zPlaneCheck_, 1, 1);
    planeGrid->addWidget(zPlaneStiffnessSpin_, 1, 2);
    planeGrid->addWidget(new QLabel(tr("X=0，法向 +X")), 2, 0);
    planeGrid->addWidget(xPlaneCheck_, 2, 1);
    planeGrid->addWidget(xPlaneStiffnessSpin_, 2, 2);
    planeGrid->addWidget(new QLabel(tr("Y=0，法向 +Y")), 3, 0);
    planeGrid->addWidget(yPlaneCheck_, 3, 1);
    planeGrid->addWidget(yPlaneStiffnessSpin_, 3, 2);
    boundaryLayout->addWidget(planes);
    auto* stabilization = new QGroupBox(tr("SPH 稳定化"), boundaryPage);
    auto* stabilizationForm = new QFormLayout(stabilization);
    artificialAlphaSpin_ = numberSpin(0.0, 100.0, 0.01, 8);
    artificialBetaSpin_ = numberSpin(0.0, 100.0, 0.1, 8);
    hourglassSpin_ = numberSpin(0.0, 100.0, 1.0, 8);
    stabilizationForm->addRow(tr("人工黏性 α"), artificialAlphaSpin_);
    stabilizationForm->addRow(tr("人工黏性 β"), artificialBetaSpin_);
    stabilizationForm->addRow(tr("Hourglass 系数"), hourglassSpin_);
    boundaryLayout->addWidget(stabilization);
    boundaryLayout->addStretch(1);
    tabs->addTab(boundaryPage, tr("边界与稳定化"));

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("生成并运行"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    connect(buttons, &QDialogButtonBox::accepted,
            this, &SphAnalysisDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected,
            this, &QDialog::reject);
    rootLayout->addWidget(buttons);
}

SphJobSettings SphAnalysisDialog::settings() const {
    SphJobSettings result;
    result.jobName = jobNameEdit_->text().trimmed();
    result.jobDirectory = QDir(jobDirectoryEdit_->text().trimmed())
                              .filePath(result.jobName);
    result.solverExecutable = solverExecutableEdit_->text().trimmed();
    result.simulationTime = simulationTimeSpin_->value() * 1.0e-6;
    result.timeStepRatio = timeStepRatioSpin_->value();
    result.outputInterval = outputIntervalSpin_->value();
    result.initialVelocityX = velocityXSpin_->value();
    result.initialVelocityY = velocityYSpin_->value();
    result.initialVelocityZ = velocityZSpin_->value();
    result.artificialViscosityAlpha = artificialAlphaSpin_->value();
    result.artificialViscosityBeta = artificialBetaSpin_->value();
    result.hourglassCoefficient = hourglassSpin_->value();
    result.preserveSourceNodeSets = preserveNodeSetsCheck_->isChecked();

    auto& material = result.material;
    material.name = materialNameEdit_->text().trimmed();
    material.density = densitySpin_->value();
    material.youngsModulus = youngsModulusSpin_->value() * 1.0e9;
    material.poissonRatio = poissonRatioSpin_->value();
    material.initialYieldStress = yieldStressSpin_->value() * 1.0e6;
    material.referencePlasticStrain = referencePlasticStrainSpin_->value();
    material.hardeningCoefficient = hardeningCoefficientSpin_->value() * 1.0e6;
    material.hardeningExponent = hardeningExponentSpin_->value();
    material.strainRateCoefficient = strainRateCoefficientSpin_->value();
    material.referenceStrainRate = referenceStrainRateSpin_->value();
    material.referenceTemperature = referenceTemperatureSpin_->value();
    material.meltingTemperature = meltingTemperatureSpin_->value();
    material.specificHeat = specificHeatSpin_->value();
    material.thermalSofteningExponent = thermalExponentSpin_->value();
    material.taylorQuinneyCoefficient = taylorQuinneySpin_->value();
    material.eosBulkModulus = eosBulkModulusSpin_->value() * 1.0e9;
    material.eosLinearShockSlope = eosShockSlopeSpin_->value();
    material.eosGruneisenGamma = eosGammaSpin_->value();

    result.rigidPlanes = {
        {zPlaneCheck_->isChecked(), 0, 0, 0, 0, 0, 1,
         zPlaneStiffnessSpin_->value() * 1.0e9},
        {xPlaneCheck_->isChecked(), 0, 0, 0, 1, 0, 0,
         xPlaneStiffnessSpin_->value() * 1.0e9},
        {yPlaneCheck_->isChecked(), 0, 0, 0, 0, 1, 0,
         yPlaneStiffnessSpin_->value() * 1.0e9}};
    result.nodeSetConstraints = defaultNodeSetConstraints();
    return result;
}

void SphAnalysisDialog::accept() {
    const SphJobSettings current = settings();
    if (current.jobName.isEmpty()) {
        QMessageBox::warning(this, tr("SPH 分析"), tr("作业名称不能为空。"));
        return;
    }
    if (jobDirectoryEdit_->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, tr("SPH 分析"), tr("作业根目录不能为空。"));
        return;
    }
    const QFileInfo solver(current.solverExecutable);
    if (!solver.exists() || !solver.isFile()) {
        QMessageBox::warning(
            this, tr("SPH 分析"),
            tr("请选择有效的 sphSolver.exe。\n当前路径：%1")
                .arg(current.solverExecutable));
        return;
    }
    if (current.material.name.isEmpty() ||
        !(current.material.meltingTemperature >
          current.material.referenceTemperature)) {
        QMessageBox::warning(
            this, tr("SPH 分析"),
            tr("材料名称不能为空，且熔化温度必须高于参考温度。"));
        return;
    }
    QSettings().setValue(QStringLiteral("sph/solverExecutable"),
                         current.solverExecutable);
    QDialog::accept();
}

QString SphAnalysisDialog::discoverSolverExecutable() {
    const QString environment = qEnvironmentVariable("QTCAE_SPH_SOLVER");
    if (QFileInfo::exists(environment)) {
        return QDir::toNativeSeparators(environment);
    }
    const QString stored = QSettings().value(
        QStringLiteral("sph/solverExecutable")).toString();
    if (QFileInfo::exists(stored)) {
        return QDir::toNativeSeparators(stored);
    }
    QDir directory(QCoreApplication::applicationDirPath());
    const QString local = directory.filePath(QStringLiteral("sphSolver.exe"));
    if (QFileInfo::exists(local)) {
        return QDir::toNativeSeparators(QFileInfo(local).absoluteFilePath());
    }
    for (int level = 0; level < 8; ++level) {
        const QString presetReleaseCandidate = directory.filePath(
            QStringLiteral("SPH-Solver/src/build/windows-msvc-release/bin/Release/sphSolver.exe"));
        if (QFileInfo::exists(presetReleaseCandidate)) {
            return QDir::toNativeSeparators(
                QFileInfo(presetReleaseCandidate).absoluteFilePath());
        }
        const QString presetReleaseFlatCandidate = directory.filePath(
            QStringLiteral("SPH-Solver/src/build/windows-msvc-release/bin/sphSolver.exe"));
        if (QFileInfo::exists(presetReleaseFlatCandidate)) {
            return QDir::toNativeSeparators(
                QFileInfo(presetReleaseFlatCandidate).absoluteFilePath());
        }
        const QString presetDebugCandidate = directory.filePath(
            QStringLiteral("SPH-Solver/src/build/windows-msvc-debug/bin/Debug/sphSolver.exe"));
        if (QFileInfo::exists(presetDebugCandidate)) {
            return QDir::toNativeSeparators(
                QFileInfo(presetDebugCandidate).absoluteFilePath());
        }
        const QString releaseCandidate = directory.filePath(
            QStringLiteral("SPH-Solver/src/build-integration-release/bin/sphSolver.exe"));
        if (QFileInfo::exists(releaseCandidate)) {
            return QDir::toNativeSeparators(
                QFileInfo(releaseCandidate).absoluteFilePath());
        }
        const QString debugCandidate = directory.filePath(
            QStringLiteral("SPH-Solver/src/build-integration-baseline/bin/sphSolver.exe"));
        if (QFileInfo::exists(debugCandidate)) {
            return QDir::toNativeSeparators(
                QFileInfo(debugCandidate).absoluteFilePath());
        }
        if (!directory.cdUp()) {
            break;
        }
    }
    return {};
}

std::vector<SphNodeSetConstraintDefinition>
SphAnalysisDialog::defaultNodeSetConstraints() {
    return {
        {QStringLiteral("fixed_nodes"), QStringLiteral("QTCAE_FIXED")},
        {QStringLiteral("yz_nodes"), QStringLiteral("QTCAE_YZFREE")},
        {QStringLiteral("z_nodes"), QStringLiteral("QTCAE_ZFREE")},
        {QStringLiteral("x_nodes"), QStringLiteral("QTCAE_XFREE")},
        {QStringLiteral("y_nodes"), QStringLiteral("QTCAE_YFREE")},
        {QStringLiteral("xy_nodes"), QStringLiteral("QTCAE_XYFREE")},
        {QStringLiteral("xz_nodes"), QStringLiteral("QTCAE_XZFREE")}};
}
