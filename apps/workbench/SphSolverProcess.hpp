#pragma once

#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>

class SphSolverProcess final : public QObject {
    Q_OBJECT

public:
    explicit SphSolverProcess(QObject* parent = nullptr);
    ~SphSolverProcess() override;

    bool start(const QString& executable,
               const QString& configurationFile,
               const QString& outputDirectory,
               const QStringList& additionalArguments = {});
    void stop();
    bool isRunning() const;
    QString lastError() const;

signals:
    void logLine(const QString& line);
    void progressUpdated(qint64 step, double time, double timeStep);
    void runFinished(bool success, const QString& message,
                     const QString& outputDirectory);

private:
    void consumeOutput();
    void consumeLine(const QString& line);
    void finishOnce(bool success, const QString& message);

    QProcess process_;
    QByteArray pendingOutput_;
    QString outputDirectory_;
    QString resultCollectionPath_;
    QString lastError_;
    bool sawSuccessResult_{false};
    bool stopRequested_{false};
    bool finishReported_{false};
};
