#include "SphExistingJobDialog.hpp"

#include "SphAnalysisDialog.hpp"

#include <functional>

#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {

constexpr int FullRunMinimumOutputInterval = 5000;

QWidget* pathEditor(QLineEdit*& edit, const QString& buttonText,
                    QWidget* parent, const std::function<void()>& browse) {
    auto* container = new QWidget(parent);
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

SphExistingJobDialog::SphExistingJobDialog(QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(tr("运行已有 SPH 配置"));
    resize(720, 300);

    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout();
    layout->addLayout(form);

    form->addRow(
        tr("配置文件"),
        pathEditor(configurationEdit_, tr("浏览..."), this,
                   [this] { chooseConfiguration(); }));
    form->addRow(
        tr("SPH 求解器"),
        pathEditor(solverEdit_, tr("浏览..."), this,
                   [this] { chooseSolver(); }));
    form->addRow(
        tr("结果目录"),
        pathEditor(outputDirectoryEdit_, tr("浏览..."), this,
                   [this] { chooseOutputDirectory(); }));

    maximumStepsSpin_ = new QSpinBox(this);
    maximumStepsSpin_->setRange(0, 1000000000);
    maximumStepsSpin_->setValue(10);
    maximumStepsSpin_->setSpecialValueText(tr("按配置完整运行"));
    form->addRow(tr("最大步数"), maximumStepsSpin_);

    endTimeSpin_ = new QDoubleSpinBox(this);
    endTimeSpin_->setRange(0.0, 1000.0);
    endTimeSpin_->setDecimals(12);
    endTimeSpin_->setValue(0.0);
    endTimeSpin_->setSuffix(tr(" s"));
    endTimeSpin_->setSpecialValueText(tr("使用配置时间"));
    form->addRow(tr("结束时间"), endTimeSpin_);

    outputIntervalSpin_ = new QSpinBox(this);
    outputIntervalSpin_->setRange(1, 1000000000);
    outputIntervalSpin_->setValue(10);
    form->addRow(tr("结果输出间隔（步）"), outputIntervalSpin_);

    auto* hint = new QLabel(
        tr("建议先用 10～100 步试运行。最大步数设为 0 时，求解器将按配置文件完整运行；完整运行时建议适当增大结果输出间隔，避免产生过多结果文件。"),
        this);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    solverEdit_->setText(SphAnalysisDialog::discoverSolverExecutable());
    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted,
            this, &SphExistingJobDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected,
            this, &QDialog::reject);
    layout->addWidget(buttons);
}

SphExistingJobSettings SphExistingJobDialog::settings() const {
    SphExistingJobSettings result;
    result.configurationFile = QDir::cleanPath(configurationEdit_->text());
    result.solverExecutable = QDir::cleanPath(solverEdit_->text());
    result.outputDirectory = QDir::cleanPath(outputDirectoryEdit_->text());
    result.maximumSteps = maximumStepsSpin_->value();
    result.endTime = endTimeSpin_->value();
    result.outputInterval = outputIntervalSpin_->value();
    return result;
}

void SphExistingJobDialog::accept() {
    const SphExistingJobSettings current = settings();
    if (!QFileInfo(current.configurationFile).isFile()) {
        QMessageBox::warning(this, tr("SPH 配置"),
                             tr("请选择有效的 configure.xml 配置文件。"));
        return;
    }
    if (!QFileInfo(current.solverExecutable).isFile()) {
        QMessageBox::warning(this, tr("SPH 配置"),
                             tr("请选择有效的 sphSolver.exe。"));
        return;
    }
    if (current.outputDirectory.isEmpty()) {
        QMessageBox::warning(this, tr("SPH 配置"),
                             tr("结果目录不能为空。"));
        return;
    }
    const QDir outputDirectory(current.outputDirectory);
    if (outputDirectory.exists() &&
        !outputDirectory.entryList(
             {QStringLiteral("*.vtu"), QStringLiteral("*.vtk"),
              QStringLiteral("*.pvd")}, QDir::Files)
             .isEmpty()) {
        QMessageBox::warning(
            this, tr("SPH 配置"),
            tr("结果目录中已有 VTK 结果文件。为避免旧帧混入新序列，请选择一个空目录。"));
        return;
    }
    const bool runsFullConfiguration =
        current.maximumSteps == 0 && current.endTime <= 0.0;
    if (runsFullConfiguration &&
        current.outputInterval < FullRunMinimumOutputInterval &&
        QMessageBox::warning(
            this, tr("结果输出过于频繁"),
            tr("当前将完整运行，并且每 %1 步输出一次结果。大型 SPH 算例可能因此生成大量 VTU 文件，占用几十 GB 甚至更多磁盘空间。\n\n建议将结果输出间隔调整为至少 %2 步。是否仍按当前设置继续？")
                .arg(current.outputInterval)
                .arg(FullRunMinimumOutputInterval),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) !=
            QMessageBox::Yes) {
        return;
    }
    if (runsFullConfiguration &&
        QMessageBox::question(
            this, tr("完整运行 SPH 配置"),
            tr("将按配置文件完整运行。大型显式动力学算例可能需要很长时间，是否继续？"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) !=
            QMessageBox::Yes) {
        return;
    }
    QSettings().setValue(QStringLiteral("sph/solverExecutable"),
                         current.solverExecutable);
    QDialog::accept();
}

void SphExistingJobDialog::chooseConfiguration() {
    const QString filePath = QFileDialog::getOpenFileName(
        this, tr("选择 SPH 配置文件"), configurationEdit_->text(),
        tr("SPH 配置文件 (configure.xml *.xml);;XML 文件 (*.xml);;所有文件 (*.*)"));
    if (filePath.isEmpty()) return;
    configurationEdit_->setText(QDir::toNativeSeparators(filePath));
    outputDirectoryEdit_->setText(QDir::toNativeSeparators(
        QDir(QFileInfo(filePath).absolutePath())
            .filePath(QStringLiteral("QTCAE_output"))));
}

void SphExistingJobDialog::chooseSolver() {
    const QString filePath = QFileDialog::getOpenFileName(
        this, tr("选择 SPH 求解器"), solverEdit_->text(),
        tr("SPH 求解器 (sphSolver.exe);;可执行文件 (*.exe);;所有文件 (*.*)"));
    if (!filePath.isEmpty())
        solverEdit_->setText(QDir::toNativeSeparators(filePath));
}

void SphExistingJobDialog::chooseOutputDirectory() {
    const QString directory = QFileDialog::getExistingDirectory(
        this, tr("选择 SPH 结果目录"), outputDirectoryEdit_->text());
    if (!directory.isEmpty())
        outputDirectoryEdit_->setText(QDir::toNativeSeparators(directory));
}
