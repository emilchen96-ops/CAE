#include "GeometryImporter.hpp"

#include <BRep_Builder.hxx>
#include <BRepTools.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <IGESControl_Reader.hxx>
#include <STEPControl_Reader.hxx>
#include <Standard_Failure.hxx>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <exception>

namespace {

QString translated(const char* text) {
    return QCoreApplication::translate("GeometryImporter", text);
}

void configureOcctResources() {
    const QDir resourceRoot(
        QCoreApplication::applicationDirPath() +
        QStringLiteral("/resources/occt"));
    const auto setResource = [&resourceRoot](const char* variable,
                                             const QString& directory) {
        const QString path = resourceRoot.filePath(directory);
        if (QDir(path).exists()) {
            qputenv(variable, QFile::encodeName(QDir::toNativeSeparators(path)));
        }
    };

    setResource("CSF_XSMessage", QStringLiteral("XSMessage"));
    setResource("CSF_SHMessage", QStringLiteral("SHMessage"));
    setResource("CSF_STEPDefaults", QStringLiteral("XSTEPResource"));
    setResource("CSF_IGESDefaults", QStringLiteral("XSTEPResource"));
    setResource("CSF_StandardDefaults", QStringLiteral("StdResource"));
}

QByteArray encodedPath(const QFileInfo& fileInfo, QString& errorMessage) {
    const QString nativePath =
        QDir::toNativeSeparators(fileInfo.absoluteFilePath());
    const QByteArray encoded = nativePath.toUtf8();
    if (QString::fromUtf8(encoded) != nativePath) {
        errorMessage = translated("当前系统无法可靠转换该文件路径。");
        return {};
    }
    return encoded;
}

GeometryImporter::ImportResult importStep(const QByteArray& path) {
    STEPControl_Reader reader;
    if (reader.ReadFile(path.constData()) != IFSelect_RetDone) {
        return {false, {}, translated("STEP 文件读取失败。")};
    }
    if (reader.TransferRoots() <= 0 || reader.NbShapes() <= 0) {
        return {false, {}, translated("STEP 数据转换失败，未生成几何体。")};
    }

    TopoDS_Shape shape = reader.OneShape();
    if (shape.IsNull()) {
        return {false, {}, translated("STEP 读取结果为空。")};
    }
    return {true, shape, {}};
}

GeometryImporter::ImportResult importIges(const QByteArray& path) {
    IGESControl_Reader reader;
    if (reader.ReadFile(path.constData()) != IFSelect_RetDone) {
        return {false, {}, translated("IGES 文件读取失败。")};
    }
    if (reader.TransferRoots() <= 0 || reader.NbShapes() <= 0) {
        return {false, {}, translated("IGES 数据转换失败，未生成几何体。")};
    }

    TopoDS_Shape shape = reader.OneShape();
    if (shape.IsNull()) {
        return {false, {}, translated("IGES 读取结果为空。")};
    }
    return {true, shape, {}};
}

GeometryImporter::ImportResult importBrep(const QByteArray& path) {
    TopoDS_Shape shape;
    BRep_Builder builder;
    if (!BRepTools::Read(shape, path.constData(), builder)) {
        return {false, {}, translated("BREP 文件读取失败。")};
    }
    if (shape.IsNull()) {
        return {false, {}, translated("BREP 读取结果为空。")};
    }
    return {true, shape, {}};
}

} // namespace

GeometryImporter::ImportResult
GeometryImporter::importFile(const QString& filePath) const {
    configureOcctResources();
    const QFileInfo fileInfo(filePath);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        return {false, {}, translated("几何文件不存在。")};
    }

    const QString extension = fileInfo.suffix().toLower();
    if (extension != QStringLiteral("step") &&
        extension != QStringLiteral("stp") &&
        extension != QStringLiteral("iges") &&
        extension != QStringLiteral("igs") &&
        extension != QStringLiteral("brep")) {
        return {false, {}, translated("不支持该文件扩展名。")};
    }

    QString pathError;
    const QByteArray path = encodedPath(fileInfo, pathError);
    if (path.isEmpty()) {
        return {false, {}, pathError};
    }

    try {
        if (extension == QStringLiteral("step") ||
            extension == QStringLiteral("stp")) {
            return importStep(path);
        }
        if (extension == QStringLiteral("iges") ||
            extension == QStringLiteral("igs")) {
            return importIges(path);
        }
        return importBrep(path);
    } catch (const Standard_Failure& failure) {
        const char* message = failure.GetMessageString();
        return {false, {},
                translated("OpenCASCADE 导入异常：") +
                    (message != nullptr
                         ? QString::fromLocal8Bit(message)
                         : translated("未知错误"))};
    } catch (const std::exception& exception) {
        return {false, {},
                translated("几何导入异常：") +
                    QString::fromLocal8Bit(exception.what())};
    } catch (...) {
        return {false, {}, translated("几何导入发生未知异常。")};
    }
}
