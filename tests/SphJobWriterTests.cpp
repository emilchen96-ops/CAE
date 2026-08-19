#include "SphJobWriter.hpp"
#include "HmAsciiMeshIo.hpp"
#include "VtkResultSequenceScanner.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>

#include <cstdlib>
#include <iostream>
#include <filesystem>
#include <utility>

namespace {

bool require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
    }
    return condition;
}

QByteArray readAll(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

MeshData tetrahedronMesh() {
    MeshData mesh;
    mesh.nodes = {
        {1, 0.0, 0.0, 0.0},
        {2, 1.0, 0.0, 0.0},
        {3, 0.0, 1.0, 0.0},
        {4, 0.0, 0.0, 1.0}};
    mesh.tetrahedra = {{1, 4, {1, 2, 3, 4}}};
    mesh.surfaceTriangles = {
        {1, 2, {1, 2, 3}}, {2, 2, {1, 2, 4}},
        {3, 2, {1, 3, 4}}, {4, 2, {2, 3, 4}}};
    return mesh;
}

SphDynamicMaterialDefinition fragmentSteel() {
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
    material.eosBulkModulus = 7700.0 * 3980.0 * 3980.0;
    material.eosLinearShockSlope = 1.58;
    material.eosGruneisenGamma = 1.6;
    material.eosGammaVolumeCorrection = 0.5;
    material.eosReferenceSpecificEnergy = 293.0;
    return material;
}

SphDynamicMaterialDefinition fragmentAluminium() {
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
    material.eosBulkModulus = 2770.0 * 5328.0 * 5328.0;
    material.eosLinearShockSlope = 1.338;
    material.eosGruneisenGamma = 2.0;
    material.eosGammaVolumeCorrection = 0.48;
    material.eosReferenceSpecificEnergy = 293.0;
    return material;
}

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    QTemporaryDir temporary;
    if (!require(temporary.isValid(), "temporary directory unavailable")) {
        return EXIT_FAILURE;
    }

    const QString sourcePath =
        QDir(temporary.path()).filePath(QStringLiteral("source mesh.hmascii"));
    QFile source(sourcePath);
    if (!require(source.open(QIODevice::WriteOnly),
                 "cannot create source HMASCII")) {
        return EXIT_FAILURE;
    }
    const QByteArray sourceBytes =
        "*component(1,\"Taylor rod\")\n"
        "*node(1,0,0,0)\n"
        "*node(2,1,0,0)\n"
        "*node(3,0,1,0)\n"
        "*node(4,0,0,1)\n"
        "*tetra4(1,1,2,3,4)\n"
        "*set(1,\"fixed_nodes\",\"nodes\",0)\n"
        "*setid(1)\n";
    source.write(sourceBytes);
    source.close();

    SphJobInput input;
    input.meshName = QStringLiteral("Taylor rod");
    input.sourceMeshFilePath = sourcePath;
    input.sourceIsHmAscii = true;
    input.mesh = tetrahedronMesh();
    input.settings.jobName = QStringLiteral("writer-test");
    input.settings.jobDirectory =
        QDir(temporary.path()).filePath(QStringLiteral("job with spaces"));
    input.settings.nodeSetConstraints = {
        {QStringLiteral("fixed_nodes"), QStringLiteral("QTCAE_FIXED")},
        {QStringLiteral("missing_nodes"), QStringLiteral("QTCAE_XFREE")}};

    const SphJobWriteResult result = SphJobWriter().write(input);
    bool okay = true;
    okay &= require(result.success, "valid SPH job was rejected");
    okay &= require(QFile::exists(result.configurationFilePath),
                    "configure.xml missing");
    okay &= require(QDir(result.outputDirectory).exists(),
                    "output directory missing");

    const QString copiedMesh = QDir(input.settings.jobDirectory).filePath(
        QStringLiteral("body_0.hmascii"));
    const QString material = QDir(input.settings.jobDirectory).filePath(
        QStringLiteral("body_0_material.dat"));
    const QByteArray configBytes = readAll(result.configurationFilePath);
    okay &= require(readAll(copiedMesh) == sourceBytes,
                    "source HMASCII was not preserved byte-for-byte");
    okay &= require(configBytes.contains(
                        "<Mesh_File>body_0.hmascii</Mesh_File>"),
                    "configuration does not use a relative mesh path");
    okay &= require(configBytes.contains(
                        "<fixed_nodes>QTCAE_FIXED</fixed_nodes>"),
                    "existing node-set constraint missing");
    okay &= require(!configBytes.contains("missing_nodes"),
                    "missing node set was written as a constraint");
    okay &= require(readAll(material).split('\n').size() >= 21,
                    "legacy material file is incomplete");
    okay &= require(!result.warnings.isEmpty(),
                    "missing node-set warning was not reported");

    const QString staleResult =
        QDir(result.outputDirectory).filePath(
            QStringLiteral("solid_999999.vtu"));
    QFile stale(staleResult);
    okay &= require(stale.open(QIODevice::WriteOnly),
                    "cannot create stale SPH result");
    stale.write("stale");
    stale.close();
    const SphJobWriteResult rewritten = SphJobWriter().write(input);
    okay &= require(rewritten.success && !QFile::exists(staleResult),
                    "stale SPH result was not removed on rerun");

    SphJobInput invalid = input;
    invalid.settings.jobDirectory =
        QDir(temporary.path()).filePath(QStringLiteral("invalid"));
    invalid.mesh = {};
    const SphJobWriteResult invalidResult = SphJobWriter().write(invalid);
    okay &= require(!invalidResult.success &&
                        !invalidResult.errorMessage.isEmpty(),
                    "empty mesh was not rejected with an error");

    SphMultiBodyJobInput multi;
    multi.settings.jobName = QStringLiteral("fragment-ui-test");
    multi.settings.jobDirectory =
        QDir(temporary.path()).filePath(QStringLiteral("fragment multi body"));
    multi.settings.timeStepRatio = 0.1;
    multi.settings.simulationTime = 0.001;
    multi.settings.outputInterval = 5000;
    multi.settings.searchRange = 1.5;
    multi.settings.searchExtension = 2.1;
    multi.settings.massFactor = 0.01;
    multi.settings.criticalStrain = 0.3;
    multi.settings.threadCount = 16;
    multi.settings.lockingFree = true;
    multi.settings.contactType = SphContactType::FrictionlessPenalty;

    SphBodyJobInput projectile;
    projectile.meshObjectId = 10;
    projectile.bodyName = QStringLiteral("fragment");
    projectile.meshName = QStringLiteral("fragment");
    projectile.sourceMeshFilePath = sourcePath;
    projectile.sourceIsHmAscii = true;
    projectile.mesh = tetrahedronMesh();
    projectile.translationY = 4.0;
    projectile.translationZ = 8.0;
    projectile.initialVelocityZ = -770.0;
    projectile.energyReleaseRate = 1.0e16;
    projectile.nodalContact = {true, 50.0, 0.5, 5000.0};
    projectile.nodeSetConstraints = input.settings.nodeSetConstraints;
    projectile.material.name = QStringLiteral("Steel");
    projectile.material.density = 7700.0;
    projectile.material.youngsModulus = 210.0e9;
    projectile.material.poissonRatio = 0.29;
    projectile.material.eosBulkModulus = 7700.0 * 3980.0 * 3980.0;
    projectile.material.eosLinearShockSlope = 1.58;
    projectile.material.eosGruneisenGamma = 1.6;
    projectile.material.eosGammaVolumeCorrection = 0.5;

    SphBodyJobInput target = projectile;
    target.meshObjectId = 11;
    target.bodyName = QStringLiteral("skin");
    target.meshName = QStringLiteral("skin");
    target.sourceIsHmAscii = false;
    target.sourceMeshFilePath.clear();
    target.translationY = 0.0;
    target.translationZ = 0.0;
    target.initialVelocityZ = 0.0;
    target.hourglassCoefficient = 20.0;
    target.material.name = QStringLiteral("Al2024_T4");
    target.material.density = 2770.0;
    target.material.youngsModulus = 73.1e9;
    target.material.poissonRatio = 0.33;
    target.material.eosBulkModulus = 2770.0 * 5328.0 * 5328.0;
    target.material.eosLinearShockSlope = 1.338;
    target.material.eosGruneisenGamma = 2.0;
    target.material.eosGammaVolumeCorrection = 0.48;
    multi.bodies = {projectile, target};

    const SphJobWriteResult multiResult = SphJobWriter().write(multi);
    okay &= require(multiResult.success,
                    "valid two-body SPH job was rejected");
    const QByteArray multiConfig = readAll(multiResult.configurationFilePath);
    okay &= require(multiConfig.contains("<NumofBodies>2</NumofBodies>"),
                    "two-body count was not written");
    okay &= require(multiConfig.contains("<BODY_0>") &&
                        multiConfig.contains("<BODY_1>"),
                    "two body sections were not written");
    okay &= require(multiConfig.contains(
                        "<Contact_Type>QTCAE_FRICTIONLESS_PENALTY_CONTACT</Contact_Type>"),
                    "multi-body contact type was not written");
    okay &= require(multiConfig.contains(
                        "<Mesh_Translation>0,4,8</Mesh_Translation>") &&
                        multiConfig.contains(
                            "<Initial_Velocity>0,0,-770</Initial_Velocity>"),
                    "projectile placement or velocity was not written");
    okay &= require(multiConfig.contains("<Nodal_Contacts>") &&
                        multiConfig.contains(
                            "<Group_Data>50,0.5,5000</Group_Data>"),
                    "nodal contact parameters were not written");
    okay &= require(multiConfig.contains("<Hourglass_Control>20</Hourglass_Control>"),
                    "per-body hourglass coefficient was not written");
    okay &= require(QDir(multi.settings.jobDirectory)
                            .entryList({QStringLiteral("body_*_material.dat")},
                                       QDir::Files)
                            .size() == 2,
                    "two independent material files were not written");

    SphMultiBodyJobInput noBodies = multi;
    noBodies.settings.jobDirectory =
        QDir(temporary.path()).filePath(QStringLiteral("no bodies"));
    noBodies.bodies.clear();
    const SphJobWriteResult noBodiesResult = SphJobWriter().write(noBodies);
    okay &= require(!noBodiesResult.success &&
                        !noBodiesResult.errorMessage.isEmpty(),
                    "empty multi-body job was not rejected");
    if (!okay || argc < 4) {
        if (!okay) {
            return EXIT_FAILURE;
        }
        std::cerr
            << "SKIPPED: 真实求解器集成冒烟需要命令行参数"
               "（求解器路径、真实 HMASCII 网格、作业目录）；"
               "CTest 无参数运行仅验证单元级行为。\n";
        return 77; // CTest SKIP_RETURN_CODE：显式标记跳过，避免静默“假通过”
    }

    const QString solverPath = QString::fromLocal8Bit(argv[1]);
    const QString realMeshPath = QString::fromLocal8Bit(argv[2]);
    const QString realJobDirectory = QString::fromLocal8Bit(argv[3]);
    const HmAsciiImportResult imported =
        HmAsciiMeshIo().importFile(realMeshPath);
    okay &= require(imported.success, "real HMASCII import failed");
    if (!okay) {
        return EXIT_FAILURE;
    }

    SphJobInput realInput;
    realInput.meshName = imported.componentName;
    realInput.sourceMeshFilePath = realMeshPath;
    realInput.sourceIsHmAscii = true;
    realInput.mesh = imported.mesh;
    realInput.settings.jobName = QStringLiteral("Taylor integration smoke");
    realInput.settings.jobDirectory = realJobDirectory;
    realInput.settings.simulationTime = 1.0e-7;
    realInput.settings.outputInterval = 1;
    realInput.settings.rigidPlanes = {
        {true, 0, 0, 0, 0, 0, 1, 5.0e9},
        {true, 0, 0, 0, 1, 0, 0, 1.0e9},
        {true, 0, 0, 0, 0, 1, 0, 1.0e9}};
    realInput.settings.nodeSetConstraints = {
        {QStringLiteral("fixed_nodes"), QStringLiteral("QTCAE_FIXED")},
        {QStringLiteral("yz_nodes"), QStringLiteral("QTCAE_YZFREE")},
        {QStringLiteral("z_nodes"), QStringLiteral("QTCAE_ZFREE")},
        {QStringLiteral("x_nodes"), QStringLiteral("QTCAE_XFREE")},
        {QStringLiteral("y_nodes"), QStringLiteral("QTCAE_YFREE")},
        {QStringLiteral("xy_nodes"), QStringLiteral("QTCAE_XYFREE")},
        {QStringLiteral("xz_nodes"), QStringLiteral("QTCAE_XZFREE")}};
    const SphJobWriteResult realJob = SphJobWriter().write(realInput);
    okay &= require(realJob.success, "real SPH job generation failed");
    if (!okay) {
        return EXIT_FAILURE;
    }

    QProcess solver;
    solver.setProcessChannelMode(QProcess::MergedChannels);
    solver.start(solverPath,
                 {QStringLiteral("--config"), realJob.configurationFilePath,
                  QStringLiteral("--output"), realJob.outputDirectory,
                  QStringLiteral("--steps"), QStringLiteral("5")});
    okay &= require(solver.waitForStarted(5000), "SPH solver did not start");
    okay &= require(solver.waitForFinished(180000),
                    "SPH solver integration smoke timed out");
    const QByteArray solverOutput = solver.readAll();
    okay &= require(solver.exitStatus() == QProcess::NormalExit &&
                        solver.exitCode() == 0,
                    "SPH solver integration smoke failed");
    okay &= require(solverOutput.contains("SPH_RESULT status=success"),
                    "SPH solver success marker missing");
    okay &= require(QFile::exists(
                        QDir(realJob.outputDirectory)
                            .filePath(QStringLiteral("solid_results.pvd"))),
                    "SPH result collection missing");

    const VtkResultSequenceScanResult sequence =
        VtkResultSequenceScanner().scan(
            std::filesystem::path(realJob.outputDirectory.toStdWString()));
    okay &= require(sequence.success && !sequence.sequence.frames.empty(),
                    "QTCAE result sequence scanner rejected SPH output");
    if (!okay) {
        std::cerr << solverOutput.constData() << '\n';
    }
    if (!okay || argc < 7) {
        if (!okay) {
            return EXIT_FAILURE;
        }
        std::cerr
            << "SKIPPED: 多体集成冒烟需要额外命令行参数"
               "（碎片网格、蒙皮网格、碎片作业目录）；"
               "无参数运行仅覆盖单体作业与求解器启动部分。\n";
        return 77; // CTest SKIP_RETURN_CODE
    }

    const QString fragmentMeshPath = QString::fromLocal8Bit(argv[4]);
    const QString skinMeshPath = QString::fromLocal8Bit(argv[5]);
    const QString fragmentJobDirectory = QString::fromLocal8Bit(argv[6]);
    const HmAsciiImportResult fragmentMesh =
        HmAsciiMeshIo().importFile(fragmentMeshPath);
    const HmAsciiImportResult skinMesh =
        HmAsciiMeshIo().importFile(skinMeshPath);
    okay &= require(fragmentMesh.success && skinMesh.success,
                    "fragment HMASCII import failed");
    if (!okay) return EXIT_FAILURE;

    SphMultiBodyJobInput fragmentInput;
    fragmentInput.settings.jobName = QStringLiteral("Fragment integration smoke");
    fragmentInput.settings.jobDirectory = fragmentJobDirectory;
    fragmentInput.settings.timeStepRatio = 0.1;
    fragmentInput.settings.simulationTime = 0.001;
    fragmentInput.settings.outputInterval = 1;
    fragmentInput.settings.searchRange = 1.5;
    fragmentInput.settings.searchExtension = 2.1;
    fragmentInput.settings.massFactor = 0.01;
    fragmentInput.settings.criticalStrain = 0.3;
    fragmentInput.settings.threadCount = 4;
    fragmentInput.settings.lockingFree = true;
    fragmentInput.settings.contactType = SphContactType::FrictionlessPenalty;

    SphBodyJobInput impactor;
    impactor.bodyName = QStringLiteral("fragment");
    impactor.meshName = fragmentMesh.componentName;
    impactor.sourceMeshFilePath = fragmentMeshPath;
    impactor.sourceIsHmAscii = true;
    impactor.mesh = fragmentMesh.mesh;
    impactor.translationY = 4.0;
    impactor.translationZ = 8.0;
    impactor.initialVelocityZ = -770.0;
    impactor.energyReleaseRate = 1.0e16;
    impactor.material = fragmentSteel();
    impactor.nodalContact = {true, 50.0, 0.5, 5000.0};

    SphBodyJobInput skin;
    skin.bodyName = QStringLiteral("skin");
    skin.meshName = skinMesh.componentName;
    skin.sourceMeshFilePath = skinMeshPath;
    skin.sourceIsHmAscii = true;
    skin.mesh = skinMesh.mesh;
    skin.energyReleaseRate = 1.0e16;
    skin.hourglassCoefficient = 20.0;
    skin.material = fragmentAluminium();
    skin.nodalContact = {true, 50.0, 0.5, 5000.0};
    fragmentInput.bodies = {std::move(impactor), std::move(skin)};

    const SphJobWriteResult fragmentJob =
        SphJobWriter().write(fragmentInput);
    okay &= require(fragmentJob.success,
                    "fragment multi-body job generation failed");
    if (!okay) return EXIT_FAILURE;

    QProcess fragmentSolver;
    fragmentSolver.setProcessChannelMode(QProcess::MergedChannels);
    fragmentSolver.start(
        solverPath,
        {QStringLiteral("--config"), fragmentJob.configurationFilePath,
         QStringLiteral("--output"), fragmentJob.outputDirectory,
         QStringLiteral("--steps"), QStringLiteral("5"),
         QStringLiteral("--output-interval"), QStringLiteral("1")});
    okay &= require(fragmentSolver.waitForStarted(5000),
                    "fragment SPH solver did not start");
    okay &= require(fragmentSolver.waitForFinished(180000),
                    "fragment SPH integration smoke timed out");
    const QByteArray fragmentOutput = fragmentSolver.readAll();
    okay &= require(fragmentSolver.exitStatus() == QProcess::NormalExit &&
                        fragmentSolver.exitCode() == 0 &&
                        fragmentOutput.contains("SPH_RESULT status=success"),
                    "fragment SPH integration smoke failed");
    const VtkResultSequenceScanResult fragmentSequence =
        VtkResultSequenceScanner().scan(
            std::filesystem::path(
                fragmentJob.outputDirectory.toStdWString()));
    okay &= require(fragmentSequence.success &&
                        !fragmentSequence.sequence.frames.empty(),
                    "QTCAE scanner rejected fragment SPH output");
    if (!okay) std::cerr << fragmentOutput.constData() << '\n';
    return okay ? EXIT_SUCCESS : EXIT_FAILURE;
}
