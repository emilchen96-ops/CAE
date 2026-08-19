#pragma once

#include <QDialog>
#include <QString>

class QDoubleSpinBox;
class QLineEdit;
class QSpinBox;

struct SphExistingJobSettings {
    QString configurationFile;
    QString solverExecutable;
    QString outputDirectory;
    qint64 maximumSteps{10};
    double endTime{0.0};
    int outputInterval{10};
};

class SphExistingJobDialog final : public QDialog {
    Q_OBJECT

public:
    explicit SphExistingJobDialog(QWidget* parent = nullptr);

    SphExistingJobSettings settings() const;

protected:
    void accept() override;

private:
    void chooseConfiguration();
    void chooseSolver();
    void chooseOutputDirectory();

    QLineEdit* configurationEdit_{nullptr};
    QLineEdit* solverEdit_{nullptr};
    QLineEdit* outputDirectoryEdit_{nullptr};
    QSpinBox* maximumStepsSpin_{nullptr};
    QDoubleSpinBox* endTimeSpin_{nullptr};
    QSpinBox* outputIntervalSpin_{nullptr};
};
