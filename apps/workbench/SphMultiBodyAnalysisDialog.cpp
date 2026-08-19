#include "SphMultiBodyAnalysisDialog.hpp"

#include "SphAnalysisDialog.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTableWidget>
#include <QThread>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <map>

namespace {

QDoubleSpinBox* numberSpin(double minimum, double maximum, double value,
                           int decimals = 8) {
    auto* spin = new QDoubleSpinBox;
    spin->setRange(minimum, maximum);
    spin->setDecimals(decimals);
    spin->setValue(std::clamp(value, minimum, maximum));
    spin->setKeyboardTracking(false);
    spin->setStepType(QAbstractSpinBox::AdaptiveDecimalStepType);
    return spin;
}

SphDynamicMaterialDefinition copperMaterial() {
    return {};
}

SphDynamicMaterialDefinition steelMaterial() {
    SphDynamicMaterialDefinition material;
    material.name = QStringLiteral("Steel");
    material.density = 7700.0;
    material.youngsModulus = 210.0e9;
    material.poissonRatio = 0.29;
    material.initialYieldStress = 400.0e6;
    material.referencePlasticStrain = 0.004;
    material.hardeningCoefficient = 400.0e6;
    material.hardeningExponent = 0.167;
    material.maximumStrainRate = 0.001;
    material.rateStressCoefficient = 400.0e6;
    material.strainRateCoefficient = 0.0167;
    material.maximumFlowStress = 1.0e10;
    material.referenceStrainRate = 0.08;
    material.referenceTemperature = 293.0;
    material.meltingTemperature = 1371.0;
    material.specificHeat = 486.0;
    material.thermalExpansion = 1.2e-5;
    material.thermalSofteningExponent = 0.75;
    material.taylorQuinneyCoefficient = 0.9;
    material.eosBulkModulus = 7700.0 * 3980.0 * 3980.0;
    material.eosLinearShockSlope = 1.58;
    material.eosQuadraticShockSlope = 0.0;
    material.eosCubicShockSlope = 0.0;
    material.eosGruneisenGamma = 1.6;
    material.eosGammaVolumeCorrection = 0.5;
    material.eosEnergyCorrection = 0.0;
    material.eosReferenceSpecificEnergy = 293.0;
    return material;
}

SphDynamicMaterialDefinition aluminiumMaterial() {
    SphDynamicMaterialDefinition material;
    material.name = QStringLiteral("Al2024_T4");
    material.density = 2770.0;
    material.youngsModulus = 73.1e9;
    material.poissonRatio = 0.33;
    material.initialYieldStress = 324.0e6;
    material.referencePlasticStrain = 0.0001;
    material.hardeningCoefficient = 186.1e6;
    material.hardeningExponent = 0.1;
    material.maximumStrainRate = 1.0;
    material.rateStressCoefficient = 186.1e6;
    material.strainRateCoefficient = 0.005;
    material.maximumFlowStress = 1.0e12;
    material.referenceStrainRate = 0.08;
    material.referenceTemperature = 293.0;
    material.meltingTemperature = 911.0;
    material.specificHeat = 875.0;
    material.thermalExpansion = 2.3876e-5;
    material.thermalSofteningExponent = 1.2;
    material.taylorQuinneyCoefficient = 0.9;
    material.eosBulkModulus = 2770.0 * 5328.0 * 5328.0;
    material.eosLinearShockSlope = 1.338;
    material.eosQuadraticShockSlope = 0.0;
    material.eosCubicShockSlope = 0.0;
    material.eosGruneisenGamma = 2.0;
    material.eosGammaVolumeCorrection = 0.48;
    material.eosEnergyCorrection = 0.0;
    material.eosReferenceSpecificEnergy = 293.0;
    return material;
}

class MaterialDialog final : public QDialog {
public:
    explicit MaterialDialog(const SphDynamicMaterialDefinition& material,
                            QWidget* parent)
        : QDialog(parent) {
        setWindowTitle(tr("编辑 SPH 动力学材料"));
        resize(650, 690);
        auto* root = new QVBoxLayout(this);
        auto* presetLayout = new QHBoxLayout;
        presetLayout->addWidget(new QLabel(tr("材料模板："), this));
        auto* preset = new QComboBox(this);
        preset->addItems({tr("保留当前参数"), tr("铜 OFHC"),
                          tr("钢（Fragment 算例）"),
                          tr("铝 2024-T4（Fragment 算例）")});
        presetLayout->addWidget(preset, 1);
        root->addLayout(presetLayout);

        auto* tabs = new QTabWidget(this);
        root->addWidget(tabs, 1);
        auto* strengthPage = new QWidget(tabs);
        auto* strengthForm = new QFormLayout(strengthPage);
        nameEdit_ = new QLineEdit(strengthPage);
        strengthForm->addRow(tr("材料名称"), nameEdit_);
        addField(strengthForm, "density", tr("密度 (kg/m³)"), 1.0, 1.0e8);
        addField(strengthForm, "young", tr("弹性模量 (Pa)"), 1.0, 1.0e15);
        addField(strengthForm, "poisson", tr("泊松比"), -0.99, 0.499999);
        addField(strengthForm, "yield", tr("初始屈服应力 (Pa)"), 1.0, 1.0e15);
        addField(strengthForm, "plastic", tr("参考塑性应变"), 1.0e-12, 1.0e6);
        addField(strengthForm, "hardening", tr("硬化系数 (Pa)"), 0.0, 1.0e15);
        addField(strengthForm, "exponent", tr("硬化指数"), 1.0e-12, 100.0);
        addField(strengthForm, "max_rate", tr("最大应变率 (1/s)"), 0.0, 1.0e15);
        addField(strengthForm, "rate_stress", tr("率相关应力系数 (Pa)"), 0.0, 1.0e15);
        addField(strengthForm, "rate_coeff", tr("应变率系数"), 0.0, 1.0e6);
        addField(strengthForm, "max_flow", tr("最大流动应力 (Pa)"), 1.0, 1.0e18);
        addField(strengthForm, "reference_rate", tr("参考应变率 (1/s)"), 1.0e-12, 1.0e15);
        tabs->addTab(strengthPage, tr("J2 强度"));

        auto* thermalPage = new QWidget(tabs);
        auto* thermalForm = new QFormLayout(thermalPage);
        addField(thermalForm, "reference_temperature", tr("参考温度 (K)"), 0.0, 1.0e6);
        addField(thermalForm, "melting_temperature", tr("熔化温度 (K)"), 0.0, 1.0e6);
        addField(thermalForm, "specific_heat", tr("比热 (J/(kg·K))"), 1.0e-12, 1.0e12);
        addField(thermalForm, "thermal_expansion", tr("热膨胀系数 (1/K)"), 0.0, 1.0);
        addField(thermalForm, "thermal_exponent", tr("热软化指数"), 0.0, 100.0);
        addField(thermalForm, "taylor_quinney", tr("Taylor-Quinney 系数"), 0.0, 1.0);
        tabs->addTab(thermalPage, tr("热参数"));

        auto* eosPage = new QWidget(tabs);
        auto* eosForm = new QFormLayout(eosPage);
        addField(eosForm, "bulk", tr("初始体积模量 K0 (Pa)"), 1.0, 1.0e18);
        addField(eosForm, "s1", tr("冲击斜率 S1"), 0.0, 100.0);
        addField(eosForm, "s2", tr("冲击斜率 S2"), 0.0, 100.0);
        addField(eosForm, "s3", tr("冲击斜率 S3"), 0.0, 100.0);
        addField(eosForm, "gamma", tr("Grüneisen γ0"), 0.0, 100.0);
        addField(eosForm, "gamma_volume", tr("γ 体积修正"), -100.0, 100.0);
        addField(eosForm, "energy", tr("参考比内能 (J/kg)"), -1.0e18, 1.0e18);
        tabs->addTab(eosPage, tr("Mie-Grüneisen 状态方程"));

        auto* buttons = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        root->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, this, [this] {
            const auto value = this->material();
            if (value.name.trimmed().isEmpty() ||
                value.meltingTemperature <= value.referenceTemperature) {
                QMessageBox::warning(
                    this, tr("SPH 材料"),
                    tr("材料名称不能为空，熔化温度必须高于参考温度。"));
                return;
            }
            accept();
        });
        connect(buttons, &QDialogButtonBox::rejected,
                this, &QDialog::reject);
        connect(preset, &QComboBox::currentIndexChanged,
                this, [this](int index) {
                    if (index == 1) setMaterial(copperMaterial());
                    if (index == 2) setMaterial(steelMaterial());
                    if (index == 3) setMaterial(aluminiumMaterial());
                });
        setMaterial(material);
    }

    SphDynamicMaterialDefinition material() const {
        SphDynamicMaterialDefinition value;
        value.name = nameEdit_->text().trimmed();
        value.density = field("density");
        value.youngsModulus = field("young");
        value.poissonRatio = field("poisson");
        value.initialYieldStress = field("yield");
        value.referencePlasticStrain = field("plastic");
        value.hardeningCoefficient = field("hardening");
        value.hardeningExponent = field("exponent");
        value.maximumStrainRate = field("max_rate");
        value.rateStressCoefficient = field("rate_stress");
        value.strainRateCoefficient = field("rate_coeff");
        value.maximumFlowStress = field("max_flow");
        value.referenceStrainRate = field("reference_rate");
        value.referenceTemperature = field("reference_temperature");
        value.meltingTemperature = field("melting_temperature");
        value.specificHeat = field("specific_heat");
        value.thermalExpansion = field("thermal_expansion");
        value.thermalSofteningExponent = field("thermal_exponent");
        value.taylorQuinneyCoefficient = field("taylor_quinney");
        value.eosBulkModulus = field("bulk");
        value.eosLinearShockSlope = field("s1");
        value.eosQuadraticShockSlope = field("s2");
        value.eosCubicShockSlope = field("s3");
        value.eosGruneisenGamma = field("gamma");
        value.eosGammaVolumeCorrection = field("gamma_volume");
        value.eosEnergyCorrection = 0.0;
        value.eosReferenceSpecificEnergy = field("energy");
        return value;
    }

private:
    void addField(QFormLayout* form, const char* key, const QString& label,
                  double minimum, double maximum) {
        auto* spin = numberSpin(minimum, maximum, minimum, 10);
        fields_[key] = spin;
        form->addRow(label, spin);
    }

    double field(const char* key) const {
        return fields_.at(key)->value();
    }

    void setField(const char* key, double value) {
        fields_.at(key)->setValue(value);
    }

    void setMaterial(const SphDynamicMaterialDefinition& value) {
        nameEdit_->setText(value.name);
        setField("density", value.density);
        setField("young", value.youngsModulus);
        setField("poisson", value.poissonRatio);
        setField("yield", value.initialYieldStress);
        setField("plastic", value.referencePlasticStrain);
        setField("hardening", value.hardeningCoefficient);
        setField("exponent", value.hardeningExponent);
        setField("max_rate", value.maximumStrainRate);
        setField("rate_stress", value.rateStressCoefficient);
        setField("rate_coeff", value.strainRateCoefficient);
        setField("max_flow", value.maximumFlowStress);
        setField("reference_rate", value.referenceStrainRate);
        setField("reference_temperature", value.referenceTemperature);
        setField("melting_temperature", value.meltingTemperature);
        setField("specific_heat", value.specificHeat);
        setField("thermal_expansion", value.thermalExpansion);
        setField("thermal_exponent", value.thermalSofteningExponent);
        setField("taylor_quinney", value.taylorQuinneyCoefficient);
        setField("bulk", value.eosBulkModulus);
        setField("s1", value.eosLinearShockSlope);
        setField("s2", value.eosQuadraticShockSlope);
        setField("s3", value.eosCubicShockSlope);
        setField("gamma", value.eosGruneisenGamma);
        setField("gamma_volume", value.eosGammaVolumeCorrection);
        setField("energy", value.eosReferenceSpecificEnergy);
    }

    QLineEdit* nameEdit_{nullptr};
    std::map<std::string, QDoubleSpinBox*> fields_;
};

QDoubleSpinBox* planeSpin(double value, double minimum = -1.0e12,
                          double maximum = 1.0e12) {
    return numberSpin(minimum, maximum, value, 6);
}

} // namespace

SphMultiBodyAnalysisDialog::SphMultiBodyAnalysisDialog(
    std::vector<SphBodyJobInput> availableBodies,
    int initiallySelectedMeshId, QWidget* parent)
    : QDialog(parent), bodies_(std::move(availableBodies)),
      bodyEnabled_(bodies_.size(), true) {
    setWindowTitle(tr("设置并运行 SPH 多部件分析"));
    resize(980, 790);
    auto* root = new QVBoxLayout(this);
    root->addWidget(new QLabel(
        tr("从已导入的一阶四面体网格创建 SPH 部件。每个部件可独立设置材料、位置和初速度。"),
        this));
    auto* tabs = new QTabWidget(this);
    root->addWidget(tabs, 1);

    auto* jobPage = new QWidget(tabs);
    auto* jobForm = new QFormLayout(jobPage);
    jobNameEdit_ = new QLineEdit(
        QStringLiteral("SPH_%1").arg(
            QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"))),
        jobPage);
    jobDirectoryEdit_ = new QLineEdit(
        QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
            .filePath(QStringLiteral("QTCAE_SPH_Jobs/%1")
                          .arg(jobNameEdit_->text())),
        jobPage);
    auto* jobDirectoryRow = new QWidget(jobPage);
    auto* jobDirectoryLayout = new QHBoxLayout(jobDirectoryRow);
    jobDirectoryLayout->setContentsMargins(0, 0, 0, 0);
    auto* browseJobButton = new QPushButton(tr("浏览..."), jobDirectoryRow);
    jobDirectoryLayout->addWidget(jobDirectoryEdit_, 1);
    jobDirectoryLayout->addWidget(browseJobButton);
    solverExecutableEdit_ = new QLineEdit(
        SphAnalysisDialog::discoverSolverExecutable(), jobPage);
    auto* solverRow = new QWidget(jobPage);
    auto* solverLayout = new QHBoxLayout(solverRow);
    solverLayout->setContentsMargins(0, 0, 0, 0);
    auto* browseSolverButton = new QPushButton(tr("浏览..."), solverRow);
    solverLayout->addWidget(solverExecutableEdit_, 1);
    solverLayout->addWidget(browseSolverButton);
    simulationTimeSpin_ = numberSpin(1.0e-12, 1.0e6,
                                     bodies_.size() > 1 ? 0.001 : 80.0e-6,
                                     12);
    timeStepRatioSpin_ = numberSpin(1.0e-6, 1.0, bodies_.size() > 1 ? 0.1 : 0.2, 6);
    outputIntervalSpin_ = new QSpinBox(jobPage);
    outputIntervalSpin_->setRange(1, 1000000000);
    outputIntervalSpin_->setValue(bodies_.size() > 1 ? 5000 : 200);
    maximumStepsSpin_ = new QSpinBox(jobPage);
    maximumStepsSpin_->setRange(0, 1000000000);
    maximumStepsSpin_->setSpecialValueText(tr("按终止时间完整运行"));
    threadCountSpin_ = new QSpinBox(jobPage);
    threadCountSpin_->setRange(1, 256);
    threadCountSpin_->setValue(std::clamp(QThread::idealThreadCount(), 1, 16));
    contactTypeCombo_ = new QComboBox(jobPage);
    contactTypeCombo_->addItem(tr("无部件接触"),
                               static_cast<int>(SphContactType::None));
    contactTypeCombo_->addItem(tr("无摩擦法向罚接触"),
                               static_cast<int>(SphContactType::FrictionlessPenalty));
    contactTypeCombo_->setCurrentIndex(bodies_.size() > 1 ? 1 : 0);
    jobForm->addRow(tr("作业名称"), jobNameEdit_);
    jobForm->addRow(tr("作业目录"), jobDirectoryRow);
    jobForm->addRow(tr("SPH 求解器"), solverRow);
    jobForm->addRow(tr("物理终止时间 (s)"), simulationTimeSpin_);
    jobForm->addRow(tr("稳定时间步比例"), timeStepRatioSpin_);
    jobForm->addRow(tr("结果输出间隔（步）"), outputIntervalSpin_);
    jobForm->addRow(tr("本次最多计算步数（0=完整）"), maximumStepsSpin_);
    jobForm->addRow(tr("计算线程数"), threadCountSpin_);
    jobForm->addRow(tr("部件接触"), contactTypeCombo_);
    tabs->addTab(jobPage, tr("作业"));

    auto* bodyPage = new QWidget(tabs);
    auto* bodyLayout = new QHBoxLayout(bodyPage);
    bodyList_ = new QListWidget(bodyPage);
    bodyList_->setMinimumWidth(260);
    for (const auto& body : bodies_) {
        auto* item = new QListWidgetItem(
            tr("%1  （节点 %2 / 四面体 %3）")
                .arg(body.meshName)
                .arg(static_cast<qulonglong>(body.mesh.nodes.size()))
                .arg(static_cast<qulonglong>(body.mesh.tetrahedra.size())),
            bodyList_);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Checked);
    }
    bodyLayout->addWidget(bodyList_);
    auto* bodyEditor = new QWidget(bodyPage);
    auto* bodyForm = new QFormLayout(bodyEditor);
    bodyNameEdit_ = new QLineEdit(bodyEditor);
    materialSummaryEdit_ = new QLineEdit(bodyEditor);
    materialSummaryEdit_->setReadOnly(true);
    auto* materialRow = new QWidget(bodyEditor);
    auto* materialLayout = new QHBoxLayout(materialRow);
    materialLayout->setContentsMargins(0, 0, 0, 0);
    auto* materialButton = new QPushButton(tr("编辑材料..."), materialRow);
    materialLayout->addWidget(materialSummaryEdit_, 1);
    materialLayout->addWidget(materialButton);
    auto* translationRow = new QWidget(bodyEditor);
    auto* translationLayout = new QHBoxLayout(translationRow);
    translationLayout->setContentsMargins(0, 0, 0, 0);
    translationXSpin_ = numberSpin(-1.0e12, 1.0e12, 0.0, 6);
    translationYSpin_ = numberSpin(-1.0e12, 1.0e12, 0.0, 6);
    translationZSpin_ = numberSpin(-1.0e12, 1.0e12, 0.0, 6);
    translationLayout->addWidget(new QLabel(QStringLiteral("X"), translationRow));
    translationLayout->addWidget(translationXSpin_);
    translationLayout->addWidget(new QLabel(QStringLiteral("Y"), translationRow));
    translationLayout->addWidget(translationYSpin_);
    translationLayout->addWidget(new QLabel(QStringLiteral("Z"), translationRow));
    translationLayout->addWidget(translationZSpin_);
    auto* velocityRow = new QWidget(bodyEditor);
    auto* velocityLayout = new QHBoxLayout(velocityRow);
    velocityLayout->setContentsMargins(0, 0, 0, 0);
    velocityXSpin_ = numberSpin(-1.0e9, 1.0e9, 0.0, 6);
    velocityYSpin_ = numberSpin(-1.0e9, 1.0e9, 0.0, 6);
    velocityZSpin_ = numberSpin(-1.0e9, 1.0e9, 0.0, 6);
    velocityLayout->addWidget(new QLabel(QStringLiteral("X"), velocityRow));
    velocityLayout->addWidget(velocityXSpin_);
    velocityLayout->addWidget(new QLabel(QStringLiteral("Y"), velocityRow));
    velocityLayout->addWidget(velocityYSpin_);
    velocityLayout->addWidget(new QLabel(QStringLiteral("Z"), velocityRow));
    velocityLayout->addWidget(velocityZSpin_);
    energyReleaseRateSpin_ = numberSpin(0.0, 1.0e30, 0.0, 6);
    artificialAlphaSpin_ = numberSpin(0.0, 1000.0, 0.01, 6);
    artificialBetaSpin_ = numberSpin(0.0, 1000.0, 0.1, 6);
    hourglassSpin_ = numberSpin(0.0, 1.0e9, 1.0, 6);
    preserveNodeSetsCheck_ = new QCheckBox(tr("保留并应用已知 HMASCII 节点集约束"), bodyEditor);
    nodalContactCheck_ = new QCheckBox(tr("写入节点接触组参数"), bodyEditor);
    contactSearchDistanceSpin_ = numberSpin(1.0e-12, 1.0e12, 50.0, 6);
    contactScaleFactorSpin_ = numberSpin(1.0e-12, 1.0e12, 0.5, 6);
    contactPenaltySpin_ = numberSpin(1.0e-12, 1.0e18, 5000.0, 6);
    bodyForm->addRow(tr("部件名称"), bodyNameEdit_);
    bodyForm->addRow(tr("动力学材料"), materialRow);
    bodyForm->addRow(tr("网格平移 (mm)"), translationRow);
    bodyForm->addRow(tr("初速度 (m/s)"), velocityRow);
    bodyForm->addRow(tr("能量释放率"), energyReleaseRateSpin_);
    bodyForm->addRow(tr("人工黏性 α"), artificialAlphaSpin_);
    bodyForm->addRow(tr("人工黏性 β"), artificialBetaSpin_);
    bodyForm->addRow(tr("沙漏控制系数"), hourglassSpin_);
    bodyForm->addRow(QString(), preserveNodeSetsCheck_);
    bodyForm->addRow(QString(), nodalContactCheck_);
    bodyForm->addRow(tr("节点接触搜索距离"), contactSearchDistanceSpin_);
    bodyForm->addRow(tr("节点接触比例系数"), contactScaleFactorSpin_);
    bodyForm->addRow(tr("节点接触罚刚度"), contactPenaltySpin_);
    bodyLayout->addWidget(bodyEditor, 1);
    tabs->addTab(bodyPage, tr("部件"));

    auto* controlsPage = new QWidget(tabs);
    auto* controlsForm = new QFormLayout(controlsPage);
    searchRangeSpin_ = numberSpin(1.000001, 100.0, bodies_.size() > 1 ? 1.5 : 1.1, 6);
    searchExtensionSpin_ = numberSpin(1.000001, 100.0, bodies_.size() > 1 ? 2.1 : 1.5, 6);
    gammaSpin_ = numberSpin(1.0e-6, 100.0, 1.6, 6);
    massFactorSpin_ = numberSpin(0.0, 100.0, bodies_.size() > 1 ? 0.01 : 0.0, 8);
    criticalNeighborCountSpin_ = new QSpinBox(controlsPage);
    criticalNeighborCountSpin_->setRange(0, 1000000);
    criticalNeighborCountSpin_->setValue(8);
    criticalStrainSpin_ = numberSpin(0.0, 1.0e9, bodies_.size() > 1 ? 0.3 : 0.2, 6);
    lockingFreeCheck_ = new QCheckBox(tr("启用无锁定修正"), controlsPage);
    lockingFreeCheck_->setChecked(bodies_.size() > 1);
    adaptiveTimeStepCheck_ = new QCheckBox(tr("启用自适应时间步"), controlsPage);
    adaptiveTimeStepCheck_->setChecked(true);
    updateNeighborhoodCheck_ = new QCheckBox(tr("每步更新邻域"), controlsPage);
    updateNeighborhoodCheck_->setChecked(true);
    controlsForm->addRow(tr("邻域搜索范围"), searchRangeSpin_);
    controlsForm->addRow(tr("搜索扩展系数"), searchExtensionSpin_);
    controlsForm->addRow(tr("核函数 γ"), gammaSpin_);
    controlsForm->addRow(tr("质量因子"), massFactorSpin_);
    controlsForm->addRow(tr("临界邻居数"), criticalNeighborCountSpin_);
    controlsForm->addRow(tr("临界等效塑性应变"), criticalStrainSpin_);
    controlsForm->addRow(QString(), lockingFreeCheck_);
    controlsForm->addRow(QString(), adaptiveTimeStepCheck_);
    controlsForm->addRow(QString(), updateNeighborhoodCheck_);
    tabs->addTab(controlsPage, tr("求解控制"));

    auto* planePage = new QWidget(tabs);
    auto* planeLayout = new QVBoxLayout(planePage);
    planeLayout->addWidget(new QLabel(
        tr("可启用最多三个无限刚性平面。刚度以 Pa 输入；法向不能为零。"),
        planePage));
    rigidPlaneTable_ = new QTableWidget(3, 8, planePage);
    rigidPlaneTable_->setHorizontalHeaderLabels(
        {tr("启用"), tr("原点 X"), tr("原点 Y"), tr("原点 Z"),
         tr("法向 X"), tr("法向 Y"), tr("法向 Z"), tr("刚度 (Pa)")});
    for (int row = 0; row < 3; ++row) {
        auto* enabled = new QCheckBox(rigidPlaneTable_);
        rigidPlaneTable_->setCellWidget(row, 0, enabled);
        rigidPlaneTable_->setCellWidget(row, 1, planeSpin(0.0));
        rigidPlaneTable_->setCellWidget(row, 2, planeSpin(0.0));
        rigidPlaneTable_->setCellWidget(row, 3, planeSpin(0.0));
        rigidPlaneTable_->setCellWidget(row, 4, planeSpin(row == 1 ? 1.0 : 0.0, -1.0, 1.0));
        rigidPlaneTable_->setCellWidget(row, 5, planeSpin(row == 2 ? 1.0 : 0.0, -1.0, 1.0));
        rigidPlaneTable_->setCellWidget(row, 6, planeSpin(row == 0 ? 1.0 : 0.0, -1.0, 1.0));
        rigidPlaneTable_->setCellWidget(row, 7, planeSpin(row == 0 ? 5.0e9 : 1.0e9, 1.0, 1.0e18));
    }
    rigidPlaneTable_->horizontalHeader()->setStretchLastSection(true);
    planeLayout->addWidget(rigidPlaneTable_, 1);
    tabs->addTab(planePage, tr("刚性平面"));

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("生成并运行"));
    root->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted,
            this, &SphMultiBodyAnalysisDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected,
            this, &QDialog::reject);
    connect(browseJobButton, &QPushButton::clicked,
            this, &SphMultiBodyAnalysisDialog::browseJobDirectory);
    connect(browseSolverButton, &QPushButton::clicked,
            this, &SphMultiBodyAnalysisDialog::browseSolverExecutable);
    connect(materialButton, &QPushButton::clicked,
            this, &SphMultiBodyAnalysisDialog::editCurrentMaterial);
    connect(bodyList_, &QListWidget::currentRowChanged,
            this, &SphMultiBodyAnalysisDialog::changeCurrentBody);
    connect(bodyList_, &QListWidget::itemChanged, this,
            [this](QListWidgetItem* item) {
                const int row = bodyList_->row(item);
                if (row >= 0 && row < static_cast<int>(bodyEnabled_.size())) {
                    bodyEnabled_[static_cast<std::size_t>(row)] =
                        item->checkState() == Qt::Checked;
                }
            });

    for (auto& body : bodies_) {
        if (body.bodyName.trimmed().isEmpty()) body.bodyName = body.meshName;
        if (body.nodeSetConstraints.empty()) {
            body.nodeSetConstraints = defaultNodeSetConstraints();
        }
    }
    int initialRow = 0;
    for (std::size_t index = 0; index < bodies_.size(); ++index) {
        if (bodies_[index].meshObjectId == initiallySelectedMeshId) {
            initialRow = static_cast<int>(index);
            break;
        }
    }
    bodyList_->setCurrentRow(initialRow);
}

SphMultiBodyJobInput SphMultiBodyAnalysisDialog::jobInput() const {
    SphMultiBodyJobInput input;
    auto& settings = input.settings;
    settings.jobName = jobNameEdit_->text().trimmed();
    settings.jobDirectory = QDir::cleanPath(jobDirectoryEdit_->text().trimmed());
    settings.solverExecutable = solverExecutableEdit_->text().trimmed();
    settings.simulationTime = simulationTimeSpin_->value();
    settings.timeStepRatio = timeStepRatioSpin_->value();
    settings.outputInterval = outputIntervalSpin_->value();
    settings.maximumSteps = maximumStepsSpin_->value();
    settings.threadCount = threadCountSpin_->value();
    settings.searchRange = searchRangeSpin_->value();
    settings.searchExtension = searchExtensionSpin_->value();
    settings.gamma = gammaSpin_->value();
    settings.massFactor = massFactorSpin_->value();
    settings.criticalNeighborCount = criticalNeighborCountSpin_->value();
    settings.criticalStrain = criticalStrainSpin_->value();
    settings.lockingFree = lockingFreeCheck_->isChecked();
    settings.adaptiveTimeStep = adaptiveTimeStepCheck_->isChecked();
    settings.updateNeighborhood = updateNeighborhoodCheck_->isChecked();
    settings.contactType = static_cast<SphContactType>(
        contactTypeCombo_->currentData().toInt());
    settings.rigidPlanes = rigidPlanes();
    for (std::size_t index = 0; index < bodies_.size(); ++index) {
        if (bodyEnabled_[index]) input.bodies.push_back(bodies_[index]);
    }
    return input;
}

void SphMultiBodyAnalysisDialog::browseJobDirectory() {
    const QString directory = QFileDialog::getExistingDirectory(
        this, tr("选择 SPH 作业目录"), jobDirectoryEdit_->text());
    if (!directory.isEmpty()) jobDirectoryEdit_->setText(QDir::toNativeSeparators(directory));
}

void SphMultiBodyAnalysisDialog::browseSolverExecutable() {
    const QString path = QFileDialog::getOpenFileName(
        this, tr("选择 SPH 求解器"), solverExecutableEdit_->text(),
        tr("SPH 求解器 (sphSolver.exe);;可执行文件 (*.exe)"));
    if (!path.isEmpty()) solverExecutableEdit_->setText(QDir::toNativeSeparators(path));
}

void SphMultiBodyAnalysisDialog::changeCurrentBody(int row) {
    storeCurrentBody();
    currentBodyRow_ = row;
    loadCurrentBody();
}

void SphMultiBodyAnalysisDialog::storeCurrentBody() {
    if (currentBodyRow_ < 0 ||
        currentBodyRow_ >= static_cast<int>(bodies_.size())) return;
    auto& body = bodies_[static_cast<std::size_t>(currentBodyRow_)];
    body.bodyName = bodyNameEdit_->text().trimmed();
    body.translationX = translationXSpin_->value();
    body.translationY = translationYSpin_->value();
    body.translationZ = translationZSpin_->value();
    body.initialVelocityX = velocityXSpin_->value();
    body.initialVelocityY = velocityYSpin_->value();
    body.initialVelocityZ = velocityZSpin_->value();
    body.energyReleaseRate = energyReleaseRateSpin_->value();
    body.artificialViscosityAlpha = artificialAlphaSpin_->value();
    body.artificialViscosityBeta = artificialBetaSpin_->value();
    body.hourglassCoefficient = hourglassSpin_->value();
    body.preserveSourceNodeSets = preserveNodeSetsCheck_->isChecked();
    body.nodalContact.enabled = nodalContactCheck_->isChecked();
    body.nodalContact.searchDistance = contactSearchDistanceSpin_->value();
    body.nodalContact.scaleFactor = contactScaleFactorSpin_->value();
    body.nodalContact.penaltyStiffness = contactPenaltySpin_->value();
}

void SphMultiBodyAnalysisDialog::loadCurrentBody() {
    const bool valid = currentBodyRow_ >= 0 &&
        currentBodyRow_ < static_cast<int>(bodies_.size());
    if (!valid) return;
    const auto& body = bodies_[static_cast<std::size_t>(currentBodyRow_)];
    bodyNameEdit_->setText(body.bodyName);
    materialSummaryEdit_->setText(
        tr("%1；ρ=%2 kg/m³；E=%3 Pa")
            .arg(body.material.name)
            .arg(body.material.density, 0, 'g', 8)
            .arg(body.material.youngsModulus, 0, 'g', 8));
    translationXSpin_->setValue(body.translationX);
    translationYSpin_->setValue(body.translationY);
    translationZSpin_->setValue(body.translationZ);
    velocityXSpin_->setValue(body.initialVelocityX);
    velocityYSpin_->setValue(body.initialVelocityY);
    velocityZSpin_->setValue(body.initialVelocityZ);
    energyReleaseRateSpin_->setValue(body.energyReleaseRate);
    artificialAlphaSpin_->setValue(body.artificialViscosityAlpha);
    artificialBetaSpin_->setValue(body.artificialViscosityBeta);
    hourglassSpin_->setValue(body.hourglassCoefficient);
    preserveNodeSetsCheck_->setChecked(body.preserveSourceNodeSets);
    nodalContactCheck_->setChecked(body.nodalContact.enabled);
    contactSearchDistanceSpin_->setValue(body.nodalContact.searchDistance);
    contactScaleFactorSpin_->setValue(body.nodalContact.scaleFactor);
    contactPenaltySpin_->setValue(body.nodalContact.penaltyStiffness);
}

void SphMultiBodyAnalysisDialog::editCurrentMaterial() {
    storeCurrentBody();
    if (currentBodyRow_ < 0 ||
        currentBodyRow_ >= static_cast<int>(bodies_.size())) return;
    auto& body = bodies_[static_cast<std::size_t>(currentBodyRow_)];
    MaterialDialog dialog(body.material, this);
    if (dialog.exec() == QDialog::Accepted) {
        body.material = dialog.material();
        loadCurrentBody();
    }
}

std::vector<SphRigidPlaneDefinition>
SphMultiBodyAnalysisDialog::rigidPlanes() const {
    std::vector<SphRigidPlaneDefinition> result;
    for (int row = 0; row < rigidPlaneTable_->rowCount(); ++row) {
        SphRigidPlaneDefinition plane;
        plane.enabled = qobject_cast<QCheckBox*>(
            rigidPlaneTable_->cellWidget(row, 0))->isChecked();
        const auto value = [this, row](int column) {
            return qobject_cast<QDoubleSpinBox*>(
                rigidPlaneTable_->cellWidget(row, column))->value();
        };
        plane.originX = value(1);
        plane.originY = value(2);
        plane.originZ = value(3);
        plane.normalX = value(4);
        plane.normalY = value(5);
        plane.normalZ = value(6);
        plane.stiffness = value(7);
        result.push_back(plane);
    }
    return result;
}

void SphMultiBodyAnalysisDialog::accept() {
    storeCurrentBody();
    const SphMultiBodyJobInput input = jobInput();
    if (input.settings.jobName.isEmpty() ||
        input.settings.jobDirectory.isEmpty()) {
        QMessageBox::warning(this, tr("SPH 分析"),
                             tr("作业名称和作业目录不能为空。"));
        return;
    }
    if (!QFileInfo::exists(input.settings.solverExecutable)) {
        QMessageBox::warning(
            this, tr("SPH 分析"),
            tr("请选择有效的 sphSolver.exe。\n当前路径：%1")
                .arg(input.settings.solverExecutable));
        return;
    }
    if (input.bodies.empty()) {
        QMessageBox::warning(this, tr("SPH 分析"),
                             tr("请至少勾选一个 SPH 部件。"));
        return;
    }
    for (const auto& body : input.bodies) {
        if (body.bodyName.trimmed().isEmpty()) {
            QMessageBox::warning(this, tr("SPH 分析"),
                                 tr("部件名称不能为空。"));
            return;
        }
    }
    if (input.bodies.size() > 1 &&
        input.settings.contactType == SphContactType::None &&
        QMessageBox::question(
            this, tr("多部件接触"),
            tr("当前有多个部件，但未启用部件接触。各部件会相互穿透，是否仍继续？"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) !=
            QMessageBox::Yes) {
        return;
    }
    if (input.settings.maximumSteps == 0 &&
        QMessageBox::question(
            this, tr("完整 SPH 计算"),
            tr("本次将按物理终止时间完整运行。大网格冲击算例可能需要数小时，是否继续？"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) !=
            QMessageBox::Yes) {
        return;
    }
    QSettings().setValue(QStringLiteral("sph/solverExecutable"),
                         input.settings.solverExecutable);
    QDialog::accept();
}

std::vector<SphNodeSetConstraintDefinition>
SphMultiBodyAnalysisDialog::defaultNodeSetConstraints() {
    return {
        {QStringLiteral("fixed_nodes"), QStringLiteral("QTCAE_FIXED")},
        {QStringLiteral("yz_nodes"), QStringLiteral("QTCAE_YZFREE")},
        {QStringLiteral("z_nodes"), QStringLiteral("QTCAE_ZFREE")},
        {QStringLiteral("x_nodes"), QStringLiteral("QTCAE_XFREE")},
        {QStringLiteral("y_nodes"), QStringLiteral("QTCAE_YFREE")},
        {QStringLiteral("xy_nodes"), QStringLiteral("QTCAE_XYFREE")},
        {QStringLiteral("xz_nodes"), QStringLiteral("QTCAE_XZFREE")}};
}
