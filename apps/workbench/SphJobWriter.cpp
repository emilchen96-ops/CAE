#include "SphJobWriter.hpp"

#include "HmAsciiMeshIo.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTextStream>
#include <QXmlStreamWriter>

#include <cmath>
#include <exception>
#include <set>

namespace {

QString number(double value) {
    return QLocale::c().toString(value, 'g', 17);
}

bool finite(double value) {
    return std::isfinite(value);
}

bool finitePositive(double value) {
    return finite(value) && value > 0.0;
}

bool finiteNonNegative(double value) {
    return finite(value) && value >= 0.0;
}

QString safeFileStem(QString value) {
    value = value.trimmed();
    for (QChar& character : value) {
        if (QStringLiteral("<>:\"/\\|?*").contains(character) ||
            character.isNull() ||
            character.category() == QChar::Other_Control) {
            character = QLatin1Char('_');
        }
    }
    return value.isEmpty() ? QStringLiteral("SPH_Analysis") : value;
}

bool writeBytesAtomically(const QString& targetPath,
                          const QByteArray& contents,
                          QString& errorMessage) {
    QSaveFile output(targetPath);
    if (!output.open(QIODevice::WriteOnly)) {
        errorMessage = QObject::tr("无法创建文件：%1").arg(targetPath);
        return false;
    }
    if (output.write(contents) != contents.size()) {
        output.cancelWriting();
        errorMessage = QObject::tr("写入文件失败：%1").arg(targetPath);
        return false;
    }
    if (!output.commit()) {
        errorMessage = QObject::tr("提交文件失败：%1").arg(targetPath);
        return false;
    }
    return true;
}

bool clearPreviousResults(const QString& outputDirectory,
                          QStringList& warnings,
                          QString& errorMessage) {
    QDir directory(outputDirectory);
    const QFileInfoList generatedFiles = directory.entryInfoList(
        {QStringLiteral("solid_*.vtu"),
         QStringLiteral("body*_*.vtu"),
         QStringLiteral("solid_results.pvd"),
         QStringLiteral("diagnostics.csv")},
        QDir::Files | QDir::NoDotAndDotDot);
    for (const QFileInfo& file : generatedFiles) {
        if (!QFile::remove(file.absoluteFilePath())) {
            errorMessage = QObject::tr("无法清理旧 SPH 结果文件：%1")
                               .arg(file.absoluteFilePath());
            return false;
        }
    }
    if (!generatedFiles.isEmpty()) {
        warnings.append(QObject::tr("已清理 %1 个旧 SPH 结果文件。")
                            .arg(generatedFiles.size()));
    }
    return true;
}

std::set<QString> sourceNodeSets(const QString& sourcePath,
                                 QStringList& warnings) {
    std::set<QString> result;
    QFile source(sourcePath);
    if (!source.open(QIODevice::ReadOnly | QIODevice::Text)) {
        warnings.append(QObject::tr("无法扫描源 HMASCII 节点集，将不写入节点约束。"));
        return result;
    }
    const QString contents = QString::fromUtf8(source.readAll());
    static const QRegularExpression pattern(
        QStringLiteral(
            R"regex(\*set\(\s*\d+\s*,\s*"([^"]+)"\s*,\s*"nodes"\s*(?:,\s*\d+)?\s*\))regex"),
        QRegularExpression::CaseInsensitiveOption);
    auto match = pattern.globalMatch(contents);
    while (match.hasNext()) {
        result.insert(match.next().captured(1));
    }
    return result;
}

bool validateMaterial(const SphDynamicMaterialDefinition& material,
                      QString& errorMessage) {
    if (material.name.trimmed().isEmpty()) {
        errorMessage = QObject::tr("SPH 材料名称不能为空。");
        return false;
    }
    if (!finitePositive(material.density) ||
        !finitePositive(material.youngsModulus) ||
        !finite(material.poissonRatio) ||
        material.poissonRatio <= -1.0 || material.poissonRatio >= 0.5 ||
        !finitePositive(material.initialYieldStress) ||
        !finitePositive(material.referencePlasticStrain) ||
        !finiteNonNegative(material.hardeningCoefficient) ||
        !finitePositive(material.hardeningExponent) ||
        !finitePositive(material.referenceStrainRate) ||
        !finitePositive(material.specificHeat) ||
        !finitePositive(material.eosBulkModulus) ||
        !(material.meltingTemperature > material.referenceTemperature)) {
        errorMessage = QObject::tr("SPH 材料或状态方程参数无效。");
        return false;
    }
    return true;
}

QByteArray materialContents(const SphDynamicMaterialDefinition& material) {
    QString contents;
    QTextStream stream(&contents);
    stream.setLocale(QLocale::c());
    const QStringList values = {
        safeFileStem(material.name), QStringLiteral("0.0"),
        QStringLiteral("0.0"), number(material.youngsModulus),
        number(material.poissonRatio), number(material.density),
        number(material.initialYieldStress),
        number(material.referencePlasticStrain),
        number(material.hardeningCoefficient),
        number(material.hardeningExponent),
        number(material.maximumStrainRate),
        number(material.rateStressCoefficient),
        number(material.strainRateCoefficient),
        number(material.maximumFlowStress),
        number(material.referenceStrainRate),
        number(material.referenceTemperature),
        number(material.meltingTemperature), number(material.specificHeat),
        number(material.thermalExpansion),
        number(material.thermalSofteningExponent),
        number(material.taylorQuinneyCoefficient)};
    for (const QString& value : values) {
        stream << value << '\n';
    }
    return contents.toUtf8();
}

void writeTextElement(QXmlStreamWriter& writer, const QString& name,
                      const QString& value) {
    writer.writeTextElement(name, value);
}

QByteArray configurationContents(
    const SphMultiBodyJobInput& input,
    const QStringList& meshFileNames,
    const QStringList& materialFileNames,
    const std::vector<std::vector<SphNodeSetConstraintDefinition>>& constraints) {
    const SphMultiBodyJobSettings& settings = input.settings;
    QByteArray data;
    QXmlStreamWriter writer(&data);
    writer.setAutoFormatting(true);
    writer.writeStartDocument();
    writer.writeStartElement(QStringLiteral("QTCAE"));
    writeTextElement(writer, QStringLiteral("VERSION"), QStringLiteral("2.0.0"));

    writer.writeStartElement(QStringLiteral("GLOBAL"));
    writeTextElement(writer, QStringLiteral("Visualization"),
                     QStringLiteral("QTCAE_VTK_NONE"));
    writeTextElement(writer, QStringLiteral("Dimension"), QStringLiteral("3"));
    writeTextElement(writer, QStringLiteral("Unit"),
                     QStringLiteral("QTCAE_MILLIMETER"));
    writeTextElement(writer, QStringLiteral("NumofBodies"),
                     QString::number(input.bodies.size()));
    writer.writeStartElement(QStringLiteral("Explicit_Solution"));
    writeTextElement(writer, QStringLiteral("Time_Step_Ratio"),
                     number(settings.timeStepRatio));
    writer.writeEndElement();
    writeTextElement(writer, QStringLiteral("Adaptive_Time_Step"),
                     settings.adaptiveTimeStep ? QStringLiteral("1")
                                               : QStringLiteral("0"));
    writeTextElement(writer, QStringLiteral("Simulation_Time"),
                     number(settings.simulationTime));
    writeTextElement(writer, QStringLiteral("Dump"),
                     QString::number(settings.outputInterval));
    writeTextElement(writer, QStringLiteral("Search_Range"),
                     number(settings.searchRange));
    writeTextElement(writer, QStringLiteral("Extension"),
                     number(settings.searchExtension));
    writeTextElement(writer, QStringLiteral("Gamma"), number(settings.gamma));
    writeTextElement(writer, QStringLiteral("Update"),
                     settings.updateNeighborhood ? QStringLiteral("1")
                                                 : QStringLiteral("0"));
    writeTextElement(writer, QStringLiteral("Truncation_Error"),
                     number(settings.truncationError));
    writeTextElement(writer, QStringLiteral("Mass_Factor"),
                     number(settings.massFactor));
    writeTextElement(writer, QStringLiteral("Critical_NumofNeighbors"),
                     QString::number(settings.criticalNeighborCount));
    writeTextElement(writer, QStringLiteral("Critical_Strain"),
                     number(settings.criticalStrain));
    writeTextElement(writer, QStringLiteral("Number_of_Threads"),
                     QString::number(settings.threadCount));
    writeTextElement(writer, QStringLiteral("Locking_Free"),
                     settings.lockingFree ? QStringLiteral("1")
                                          : QStringLiteral("0"));
    if (settings.contactType == SphContactType::FrictionlessPenalty) {
        writeTextElement(writer, QStringLiteral("Contact_Type"),
                         QStringLiteral("QTCAE_FRICTIONLESS_PENALTY_CONTACT"));
    }
    writer.writeEndElement();

    for (const SphRigidPlaneDefinition& plane : settings.rigidPlanes) {
        if (!plane.enabled) continue;
        writer.writeStartElement(QStringLiteral("INFINITE_RIGID_SURFACE"));
        writeTextElement(writer, QStringLiteral("Origin"),
                         QStringLiteral("%1,%2,%3")
                             .arg(number(plane.originX), number(plane.originY),
                                  number(plane.originZ)));
        writeTextElement(writer, QStringLiteral("Normal"),
                         QStringLiteral("%1,%2,%3")
                             .arg(number(plane.normalX), number(plane.normalY),
                                  number(plane.normalZ)));
        writeTextElement(writer, QStringLiteral("Stiffness"),
                         number(plane.stiffness));
        writeTextElement(writer, QStringLiteral("Velocity"),
                         QStringLiteral("QTCAE_TIMEFUNCTION_CONSTANT,0,0,0"));
        writer.writeEndElement();
    }

    for (std::size_t index = 0; index < input.bodies.size(); ++index) {
        const SphBodyJobInput& body = input.bodies[index];
        const SphDynamicMaterialDefinition& material = body.material;
        writer.writeStartElement(QStringLiteral("BODY_%1").arg(
            static_cast<qulonglong>(index)));
        writeTextElement(writer, QStringLiteral("Mesh_File"),
                         meshFileNames.at(static_cast<qsizetype>(index)));
        if (body.translationX != 0.0 || body.translationY != 0.0 ||
            body.translationZ != 0.0) {
            writeTextElement(writer, QStringLiteral("Mesh_Translation"),
                             QStringLiteral("%1,%2,%3")
                                 .arg(number(body.translationX),
                                      number(body.translationY),
                                      number(body.translationZ)));
        }
        writer.writeStartElement(QStringLiteral("Element_Groups"));
        writer.writeStartElement(QStringLiteral("all"));
        writeTextElement(writer, QStringLiteral("Element_Type"),
                         QStringLiteral("QTCAE_MATERIAL_POINT"));
        writeTextElement(writer, QStringLiteral("Density"),
                         number(material.density));
        writeTextElement(writer, QStringLiteral("Material_Type"),
                         QStringLiteral("QTCAE_MAT_J2_POWERLAW"));
        const QString eosParameters = QStringLiteral(
            "QTCAE_EOS_MIEGRUNEISEN,%1,%2,%3,%4,%5,%6,%7,%8;%9")
            .arg(number(material.eosBulkModulus),
                 number(material.eosLinearShockSlope),
                 number(material.eosQuadraticShockSlope),
                 number(material.eosCubicShockSlope),
                 number(material.eosGruneisenGamma),
                 number(material.eosGammaVolumeCorrection),
                 number(material.eosEnergyCorrection),
                 number(material.eosReferenceSpecificEnergy),
                 materialFileNames.at(static_cast<qsizetype>(index)));
        writeTextElement(writer, QStringLiteral("Material_Parameters"),
                         eosParameters);
        writeTextElement(writer, QStringLiteral("Energy_Release_Rate"),
                         number(body.energyReleaseRate));
        writeTextElement(writer, QStringLiteral("Artificial_Viscosity"),
                         QStringLiteral("1,%1,%2,0,0")
                             .arg(number(body.artificialViscosityAlpha),
                                  number(body.artificialViscosityBeta)));
        writeTextElement(writer, QStringLiteral("Hourglass_Control"),
                         number(body.hourglassCoefficient));
        writer.writeEndElement();
        writer.writeEndElement();

        if (!constraints[index].empty()) {
            writer.writeStartElement(QStringLiteral("Nodal_Constraints"));
            for (const auto& constraint : constraints[index]) {
                writeTextElement(writer, constraint.nodeSetName,
                                 constraint.constraintType);
            }
            writer.writeEndElement();
        }
        if (body.nodalContact.enabled) {
            writer.writeStartElement(QStringLiteral("Nodal_Contacts"));
            writeTextElement(
                writer, QStringLiteral("Group_Data"),
                QStringLiteral("%1,%2,%3")
                    .arg(number(body.nodalContact.searchDistance),
                         number(body.nodalContact.scaleFactor),
                         number(body.nodalContact.penaltyStiffness)));
            writer.writeEndElement();
        }
        writeTextElement(writer, QStringLiteral("Initial_Velocity"),
                         QStringLiteral("%1,%2,%3")
                             .arg(number(body.initialVelocityX),
                                  number(body.initialVelocityY),
                                  number(body.initialVelocityZ)));
        writer.writeEndElement();
    }
    writer.writeEndElement();
    writer.writeEndDocument();
    return data;
}

bool validateGlobalSettings(const SphMultiBodyJobSettings& settings,
                            QString& errorMessage) {
    if (settings.jobDirectory.trimmed().isEmpty()) {
        errorMessage = QObject::tr("SPH 作业目录不能为空。");
        return false;
    }
    if (!finitePositive(settings.timeStepRatio) || settings.timeStepRatio > 1.0 ||
        !finitePositive(settings.simulationTime) ||
        settings.outputInterval <= 0 ||
        !finitePositive(settings.searchRange) || settings.searchRange <= 1.0 ||
        !finitePositive(settings.searchExtension) ||
        settings.searchExtension <= 1.0 || !finitePositive(settings.gamma) ||
        !finiteNonNegative(settings.truncationError) ||
        !finiteNonNegative(settings.massFactor) ||
        settings.criticalNeighborCount < 0 ||
        !finiteNonNegative(settings.criticalStrain) ||
        settings.threadCount <= 0 || settings.maximumSteps < 0) {
        errorMessage = QObject::tr("SPH 全局时间、搜索或求解控制参数无效。");
        return false;
    }
    for (const auto& plane : settings.rigidPlanes) {
        if (!plane.enabled) continue;
        const double normalMagnitude = std::sqrt(
            plane.normalX * plane.normalX + plane.normalY * plane.normalY +
            plane.normalZ * plane.normalZ);
        if (!finitePositive(normalMagnitude) ||
            !finitePositive(plane.stiffness)) {
            errorMessage = QObject::tr("SPH 刚性平面的法向或刚度无效。");
            return false;
        }
    }
    return true;
}

bool validateBody(const SphBodyJobInput& body, int bodyNumber,
                  QString& errorMessage) {
    if (body.mesh.nodes.empty() || body.mesh.tetrahedra.empty()) {
        errorMessage = QObject::tr("部件 %1 需要非空的一阶四面体网格。")
                           .arg(bodyNumber + 1);
        return false;
    }
    if (!finite(body.translationX) || !finite(body.translationY) ||
        !finite(body.translationZ) || !finite(body.initialVelocityX) ||
        !finite(body.initialVelocityY) || !finite(body.initialVelocityZ) ||
        !finiteNonNegative(body.energyReleaseRate) ||
        !finiteNonNegative(body.artificialViscosityAlpha) ||
        !finiteNonNegative(body.artificialViscosityBeta) ||
        !finiteNonNegative(body.hourglassCoefficient)) {
        errorMessage = QObject::tr("部件 %1 的位置、速度或稳定化参数无效。")
                           .arg(bodyNumber + 1);
        return false;
    }
    if (body.nodalContact.enabled &&
        (!finitePositive(body.nodalContact.searchDistance) ||
         !finitePositive(body.nodalContact.scaleFactor) ||
         !finitePositive(body.nodalContact.penaltyStiffness))) {
        errorMessage = QObject::tr("部件 %1 的节点接触参数无效。")
                           .arg(bodyNumber + 1);
        return false;
    }
    QString materialError;
    if (!validateMaterial(body.material, materialError)) {
        errorMessage = QObject::tr("部件 %1：%2")
                           .arg(bodyNumber + 1)
                           .arg(materialError);
        return false;
    }
    return true;
}

} // namespace

SphJobWriteResult SphJobWriter::write(const SphJobInput& input) const {
    SphMultiBodyJobInput multi;
    multi.settings.jobName = input.settings.jobName;
    multi.settings.jobDirectory = input.settings.jobDirectory;
    multi.settings.solverExecutable = input.settings.solverExecutable;
    multi.settings.timeStepRatio = input.settings.timeStepRatio;
    multi.settings.simulationTime = input.settings.simulationTime;
    multi.settings.outputInterval = input.settings.outputInterval;
    multi.settings.searchRange = input.settings.searchRange;
    multi.settings.searchExtension = input.settings.searchExtension;
    multi.settings.gamma = input.settings.gamma;
    multi.settings.updateNeighborhood = input.settings.updateNeighborhood;
    multi.settings.truncationError = input.settings.truncationError;
    multi.settings.massFactor = input.settings.massFactor;
    multi.settings.criticalNeighborCount = input.settings.criticalNeighborCount;
    multi.settings.criticalStrain = input.settings.criticalStrain;
    multi.settings.threadCount = input.settings.threadCount;
    multi.settings.lockingFree = input.settings.lockingFree;
    multi.settings.adaptiveTimeStep = input.settings.adaptiveTimeStep;
    multi.settings.contactType = input.settings.contactType;
    multi.settings.maximumSteps = input.settings.maximumSteps;
    multi.settings.rigidPlanes = input.settings.rigidPlanes;

    SphBodyJobInput body;
    body.bodyName = input.meshName;
    body.meshName = input.meshName;
    body.sourceMeshFilePath = input.sourceMeshFilePath;
    body.sourceIsHmAscii = input.sourceIsHmAscii;
    body.mesh = input.mesh;
    body.initialVelocityX = input.settings.initialVelocityX;
    body.initialVelocityY = input.settings.initialVelocityY;
    body.initialVelocityZ = input.settings.initialVelocityZ;
    body.artificialViscosityAlpha = input.settings.artificialViscosityAlpha;
    body.artificialViscosityBeta = input.settings.artificialViscosityBeta;
    body.hourglassCoefficient = input.settings.hourglassCoefficient;
    body.preserveSourceNodeSets = input.settings.preserveSourceNodeSets;
    body.material = input.settings.material;
    body.nodeSetConstraints = input.settings.nodeSetConstraints;
    multi.bodies.push_back(std::move(body));
    return write(multi);
}

SphJobWriteResult SphJobWriter::write(
    const SphMultiBodyJobInput& input) const {
    SphJobWriteResult result;
    try {
        if (input.bodies.empty()) {
            result.errorMessage = QObject::tr("SPH 作业至少需要一个已启用部件。");
            return result;
        }
        if (!validateGlobalSettings(input.settings, result.errorMessage)) {
            return result;
        }
        for (std::size_t index = 0; index < input.bodies.size(); ++index) {
            if (!validateBody(input.bodies[index], static_cast<int>(index),
                              result.errorMessage)) {
                return result;
            }
        }

        const QString jobDirectory = QDir::cleanPath(input.settings.jobDirectory);
        QDir directory;
        if (!directory.mkpath(jobDirectory)) {
            result.errorMessage = QObject::tr("无法创建 SPH 作业目录：%1")
                                      .arg(jobDirectory);
            return result;
        }
        result.outputDirectory =
            QDir(jobDirectory).filePath(QStringLiteral("output"));
        if (!directory.mkpath(result.outputDirectory)) {
            result.errorMessage = QObject::tr("无法创建 SPH 结果目录：%1")
                                      .arg(result.outputDirectory);
            return result;
        }
        if (!clearPreviousResults(result.outputDirectory, result.warnings,
                                  result.errorMessage)) {
            return result;
        }

        QStringList meshFileNames;
        QStringList materialFileNames;
        std::vector<std::vector<SphNodeSetConstraintDefinition>> constraints(
            input.bodies.size());
        for (std::size_t index = 0; index < input.bodies.size(); ++index) {
            const SphBodyJobInput& body = input.bodies[index];
            const QString prefix = QStringLiteral("body_%1").arg(
                static_cast<qulonglong>(index));
            const QString meshFileName = prefix + QStringLiteral(".hmascii");
            const QString materialFileName =
                prefix + QStringLiteral("_material.dat");
            const QString meshPath = QDir(jobDirectory).filePath(meshFileName);
            const QString materialPath =
                QDir(jobDirectory).filePath(materialFileName);

            std::set<QString> availableNodeSets;
            if (body.sourceIsHmAscii &&
                QFileInfo::exists(body.sourceMeshFilePath)) {
                QFile source(body.sourceMeshFilePath);
                if (!source.open(QIODevice::ReadOnly)) {
                    result.errorMessage = QObject::tr("无法读取部件 %1 的源 HMASCII 网格：%2")
                                              .arg(static_cast<qulonglong>(index + 1))
                                              .arg(body.sourceMeshFilePath);
                    return result;
                }
                if (!writeBytesAtomically(meshPath, source.readAll(),
                                          result.errorMessage)) {
                    return result;
                }
                if (body.preserveSourceNodeSets) {
                    availableNodeSets =
                        sourceNodeSets(body.sourceMeshFilePath, result.warnings);
                }
            } else {
                const HmAsciiIoResult meshResult = HmAsciiMeshIo().exportFile(
                    meshPath, body.mesh,
                    body.meshName.isEmpty() ? prefix : body.meshName);
                if (!meshResult.success) {
                    result.errorMessage = meshResult.errorMessage;
                    return result;
                }
                result.warnings.append(meshResult.warnings);
                if (body.preserveSourceNodeSets &&
                    !body.nodeSetConstraints.empty()) {
                    result.warnings.append(QObject::tr(
                        "部件 %1 没有原始 HMASCII 节点集，未写入节点约束。")
                                               .arg(static_cast<qulonglong>(index + 1)));
                }
            }

            if (body.preserveSourceNodeSets && !availableNodeSets.empty()) {
                for (const auto& constraint : body.nodeSetConstraints) {
                    if (availableNodeSets.contains(constraint.nodeSetName)) {
                        constraints[index].push_back(constraint);
                    } else {
                        result.warnings.append(QObject::tr(
                            "部件 %1 的源 HMASCII 中不存在节点集“%2”，已跳过对应约束。")
                                                   .arg(static_cast<qulonglong>(index + 1))
                                                   .arg(constraint.nodeSetName));
                    }
                }
            }
            if (!writeBytesAtomically(materialPath,
                                      materialContents(body.material),
                                      result.errorMessage)) {
                return result;
            }
            meshFileNames.append(meshFileName);
            materialFileNames.append(materialFileName);
        }

        result.configurationFilePath =
            QDir(jobDirectory).filePath(QStringLiteral("configure.xml"));
        if (!writeBytesAtomically(
                result.configurationFilePath,
                configurationContents(input, meshFileNames,
                                      materialFileNames, constraints),
                result.errorMessage)) {
            return result;
        }
        result.success = true;
    } catch (const std::exception& error) {
        result.errorMessage = QObject::tr("生成 SPH 作业时发生异常：%1")
                                  .arg(QString::fromUtf8(error.what()));
    } catch (...) {
        result.errorMessage = QObject::tr("生成 SPH 作业时发生未知异常。");
    }
    return result;
}
