#include "SphSolverProcess.hpp"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTimer>

SphSolverProcess::SphSolverProcess(QObject* parent)
    : QObject(parent) {
    process_.setProcessChannelMode(QProcess::MergedChannels);
    connect(&process_, &QProcess::readyReadStandardOutput,
            this, &SphSolverProcess::consumeOutput);
    connect(&process_, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError error) {
                if (error == QProcess::FailedToStart) {
                    lastError_ = tr("SPH 求解器启动失败：%1")
                                     .arg(process_.errorString());
                    finishOnce(false, lastError_);
                }
            });
    connect(&process_, qOverload<int, QProcess::ExitStatus>(
                           &QProcess::finished),
            this, [this](int exitCode, QProcess::ExitStatus exitStatus) {
                consumeOutput();
                if (!pendingOutput_.isEmpty()) {
                    consumeLine(QString::fromLocal8Bit(pendingOutput_).trimmed());
                    pendingOutput_.clear();
                }
                if (stopRequested_) {
                    finishOnce(false, tr("SPH 分析已由用户停止。"));
                    return;
                }
                const bool successfulExit =
                    exitStatus == QProcess::NormalExit && exitCode == 0;
                if (successfulExit && sawSuccessResult_) {
                    finishOnce(true, tr("SPH 分析计算完成。"));
                } else {
                    finishOnce(
                        false,
                        tr("SPH 求解器异常结束（退出码 %1）。")
                            .arg(exitCode));
                }
            });
}

SphSolverProcess::~SphSolverProcess() {
    disconnect(&process_, nullptr, this, nullptr);
    if (process_.state() != QProcess::NotRunning) {
        process_.kill();
        process_.waitForFinished(2000);
    }
}

bool SphSolverProcess::start(const QString& executable,
                             const QString& configurationFile,
                             const QString& outputDirectory,
                             const QStringList& additionalArguments) {
    if (isRunning()) {
        lastError_ = tr("已有 SPH 分析正在运行。");
        return false;
    }
    if (!QFileInfo::exists(executable) ||
        !QFileInfo::exists(configurationFile)) {
        lastError_ = tr("SPH 求解器或配置文件不存在。");
        return false;
    }
    pendingOutput_.clear();
    outputDirectory_ = QDir::cleanPath(outputDirectory);
    resultCollectionPath_.clear();
    lastError_.clear();
    sawSuccessResult_ = false;
    stopRequested_ = false;
    finishReported_ = false;
    process_.setWorkingDirectory(QFileInfo(configurationFile).absolutePath());
    QStringList arguments{
        QStringLiteral("--config"), configurationFile,
        QStringLiteral("--output"), outputDirectory_};
    arguments.append(additionalArguments);
    process_.start(executable, arguments);
    if (!process_.waitForStarted(3000)) {
        // waitForStarted 超时不会停止进程：求解器可能仍在后台启动。
        // 若不清理，会在 finishOnce 之后到达 finished 信号，导致
        // 完成结果被静默吞掉，且 isRunning() 一直为 true 阻止新作业。
        if (process_.state() != QProcess::NotRunning) {
            process_.kill();
            process_.waitForFinished(1000);
        }
        lastError_ = tr("SPH 求解器启动失败：%1")
                         .arg(process_.errorString());
        finishOnce(false, lastError_);
        return false;
    }
    return true;
}

void SphSolverProcess::stop() {
    if (!isRunning()) {
        return;
    }
    stopRequested_ = true;
    process_.terminate();
    QTimer::singleShot(2000, this, [this] {
        if (process_.state() != QProcess::NotRunning) {
            process_.kill();
        }
    });
}

bool SphSolverProcess::isRunning() const {
    return process_.state() != QProcess::NotRunning;
}

QString SphSolverProcess::lastError() const {
    return lastError_;
}

void SphSolverProcess::consumeOutput() {
    pendingOutput_.append(process_.readAllStandardOutput());
    qsizetype newline = pendingOutput_.indexOf('\n');
    while (newline >= 0) {
        QByteArray line = pendingOutput_.left(newline);
        pendingOutput_.remove(0, newline + 1);
        if (!line.isEmpty() && line.endsWith('\r')) {
            line.chop(1);
        }
        consumeLine(QString::fromLocal8Bit(line));
        newline = pendingOutput_.indexOf('\n');
    }
}

void SphSolverProcess::consumeLine(const QString& line) {
    const QString trimmed = line.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }
    emit logLine(trimmed);
    static const QRegularExpression progressPattern(
        QStringLiteral(
            R"(^SPH_PROGRESS\s+step=(\d+)\s+time=([^\s]+)\s+dt=([^\s]+)$)"));
    const auto progressMatch = progressPattern.match(trimmed);
    if (progressMatch.hasMatch()) {
        bool stepValid = false;
        bool timeValid = false;
        bool dtValid = false;
        const qint64 step = progressMatch.captured(1).toLongLong(&stepValid);
        const double time = progressMatch.captured(2).toDouble(&timeValid);
        const double dt = progressMatch.captured(3).toDouble(&dtValid);
        if (stepValid && timeValid && dtValid) {
            emit progressUpdated(step, time, dt);
        }
        return;
    }
    if (trimmed.startsWith(QStringLiteral("SPH_RESULT status=success"))) {
        sawSuccessResult_ = true;
        return;
    }
    const QString collectionPrefix =
        QStringLiteral("SPH_RESULT_COLLECTION ");
    if (trimmed.startsWith(collectionPrefix)) {
        resultCollectionPath_ = trimmed.mid(collectionPrefix.size()).trimmed();
        const QFileInfo collection(resultCollectionPath_);
        if (collection.exists()) {
            outputDirectory_ = collection.absolutePath();
        }
    }
}

void SphSolverProcess::finishOnce(bool success, const QString& message) {
    if (finishReported_) {
        return;
    }
    finishReported_ = true;
    if (!success) {
        lastError_ = message;
    }
    emit runFinished(success, message, outputDirectory_);
}
