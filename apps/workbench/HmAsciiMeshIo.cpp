#include "HmAsciiMeshIo.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QSaveFile>
#include <QTextStream>

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

constexpr int GmshTriangle3Type = 2;
constexpr int GmshTetrahedron4Type = 4;
constexpr std::size_t MaximumHmAsciiId =
    static_cast<std::size_t>(std::numeric_limits<int>::max());

struct ParsedCommand {
    QString name;
    QStringList arguments;
};

struct FaceKey {
    std::array<std::size_t, 3> nodeIds;

    bool operator==(const FaceKey&) const = default;
};

struct FaceKeyHash {
    std::size_t operator()(const FaceKey& key) const noexcept {
        std::size_t seed = 0;
        for (const std::size_t value : key.nodeIds) {
            seed ^= std::hash<std::size_t>{}(value) +
                    0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U);
        }
        return seed;
    }
};

struct FaceRecord {
    std::array<std::size_t, 3> orientedNodeIds;
    std::size_t count{0};
};

QString translated(const char* text) {
    return QCoreApplication::translate("HmAsciiMeshIo", text);
}

QString commandNameFromLine(const QString& line) {
    const QString trimmed = line.trimmed();
    if (!trimmed.startsWith(QLatin1Char('*'))) {
        return {};
    }
    qsizetype index = 1;
    while (index < trimmed.size() &&
           (trimmed.at(index).isLetterOrNumber() ||
            trimmed.at(index) == QLatin1Char('_'))) {
        ++index;
    }
    return trimmed.mid(1, index - 1).toLower();
}

bool isSupportedCommand(const QString& name) {
    return name == QStringLiteral("component") ||
           name == QStringLiteral("node") ||
           name == QStringLiteral("tetra4") ||
           name == QStringLiteral("tria3");
}

bool parseCommand(const QString& line, ParsedCommand& command,
                  QString& reason) {
    const QString trimmed = line.trimmed();
    command.name = commandNameFromLine(trimmed);
    if (command.name.isEmpty()) {
        reason = translated("缺少有效命令名。");
        return false;
    }

    qsizetype position = 1 + command.name.size();
    while (position < trimmed.size() && trimmed.at(position).isSpace()) {
        ++position;
    }
    if (position >= trimmed.size() ||
        trimmed.at(position) != QLatin1Char('(')) {
        reason = translated("缺少左括号。");
        return false;
    }

    QString current;
    QStringList arguments;
    bool inQuotes = false;
    bool argumentWasQuoted = false;
    bool closed = false;
    for (++position; position < trimmed.size(); ++position) {
        const QChar character = trimmed.at(position);
        if (inQuotes) {
            if (character == QLatin1Char('"')) {
                if (position + 1 < trimmed.size() &&
                    trimmed.at(position + 1) == QLatin1Char('"')) {
                    current.append(QLatin1Char('"'));
                    ++position;
                } else {
                    inQuotes = false;
                }
            } else {
                current.append(character);
            }
            continue;
        }

        if (character == QLatin1Char('"')) {
            if (!current.trimmed().isEmpty()) {
                reason = translated("引号字符串前包含非法字符。");
                return false;
            }
            current.clear();
            inQuotes = true;
            argumentWasQuoted = true;
        } else if (character == QLatin1Char(',')) {
            const QString value = argumentWasQuoted ? current
                                                    : current.trimmed();
            if (value.isEmpty()) {
                reason = translated("存在空参数。");
                return false;
            }
            arguments.append(value);
            current.clear();
            argumentWasQuoted = false;
        } else if (character == QLatin1Char(')')) {
            const QString value = argumentWasQuoted ? current
                                                    : current.trimmed();
            if (!value.isEmpty()) {
                arguments.append(value);
            } else if (!arguments.isEmpty()) {
                reason = translated("最后一个参数为空。");
                return false;
            }
            ++position;
            while (position < trimmed.size() &&
                   trimmed.at(position).isSpace()) {
                ++position;
            }
            if (position != trimmed.size()) {
                reason = translated("右括号后包含多余内容。");
                return false;
            }
            closed = true;
            break;
        } else {
            current.append(character);
        }
    }
    if (inQuotes) {
        reason = translated("引号字符串未闭合。");
        return false;
    }
    if (!closed) {
        reason = translated("缺少右括号。");
        return false;
    }
    command.arguments = arguments;
    return true;
}

QString lineError(const QString& filePath, qsizetype lineNumber,
                  const QString& commandName, const QString& reason) {
    return translated("%1：第 %2 行，命令 *%3：%4")
        .arg(QFileInfo(filePath).fileName())
        .arg(lineNumber)
        .arg(commandName.isEmpty() ? translated("未知") : commandName,
             reason);
}

bool parsePositiveId(const QString& text, std::size_t& value) {
    bool valid = false;
    const qulonglong parsed = text.toULongLong(&valid, 10);
    if (!valid || parsed == 0 ||
        parsed > std::numeric_limits<std::size_t>::max()) {
        return false;
    }
    value = static_cast<std::size_t>(parsed);
    return true;
}

bool parseFiniteDouble(const QString& text, double& value) {
    bool valid = false;
    value = QLocale::c().toDouble(text, &valid);
    return valid && std::isfinite(value);
}

FaceKey makeFaceKey(std::array<std::size_t, 3> nodeIds) {
    std::sort(nodeIds.begin(), nodeIds.end());
    return {nodeIds};
}

std::vector<MeshElement> extractBoundaryTriangles(
    const std::vector<MeshElement>& tetrahedra) {
    std::unordered_map<FaceKey, FaceRecord, FaceKeyHash> faces;
    faces.reserve(tetrahedra.size() * 4);
    for (const MeshElement& tetrahedron : tetrahedra) {
        const auto& n = tetrahedron.nodeIds;
        const std::array<std::array<std::size_t, 3>, 4> tetrahedronFaces = {{
            {n[0], n[2], n[1]},
            {n[0], n[1], n[3]},
            {n[1], n[2], n[3]},
            {n[2], n[0], n[3]}
        }};
        for (const auto& orientedFace : tetrahedronFaces) {
            const FaceKey key = makeFaceKey(orientedFace);
            auto [iterator, inserted] = faces.try_emplace(
                key, FaceRecord{orientedFace, 0});
            ++iterator->second.count;
        }
    }

    std::vector<MeshElement> triangles;
    triangles.reserve(faces.size());
    std::size_t triangleId = 1;
    for (const auto& [key, record] : faces) {
        Q_UNUSED(key)
        if (record.count == 1) {
            triangles.push_back({triangleId++, GmshTriangle3Type,
                                 {record.orientedNodeIds[0],
                                  record.orientedNodeIds[1],
                                  record.orientedNodeIds[2]}});
        }
    }
    return triangles;
}

bool hasDuplicateNodeIds(const std::vector<std::size_t>& nodeIds) {
    std::unordered_set<std::size_t> uniqueIds;
    uniqueIds.reserve(nodeIds.size());
    for (const std::size_t id : nodeIds) {
        if (!uniqueIds.insert(id).second) {
            return true;
        }
    }
    return false;
}

QString safeComponentName(QString name) {
    name = name.trimmed();
    for (QChar& character : name) {
        if (character == QLatin1Char('"')) {
            character = QLatin1Char('\'');
        } else if (character.isNull() || character.isSpace() ||
                   character.category() == QChar::Other_Control) {
            character = QLatin1Char(' ');
        }
    }
    name = name.simplified();
    return name.isEmpty() ? QStringLiteral("QTCAE_Mesh") : name;
}

struct ExportNumbering {
    std::vector<std::size_t> nodeIds;
    std::vector<std::size_t> tetrahedronIds;
    std::vector<std::size_t> triangleIds;
    std::unordered_map<std::size_t, std::size_t> referencedNodeIds;
    QStringList warnings;
};

std::vector<std::size_t> validOrSequentialElementIds(
    const std::vector<MeshElement>& elements, const QString& entityName,
    QStringList& warnings) {
    std::unordered_set<std::size_t> ids;
    ids.reserve(elements.size());
    bool preserve = true;
    for (const MeshElement& element : elements) {
        preserve = preserve && element.id > 0 &&
                   element.id <= MaximumHmAsciiId &&
                   ids.insert(element.id).second;
    }
    std::vector<std::size_t> result;
    result.reserve(elements.size());
    if (preserve) {
        for (const MeshElement& element : elements) {
            result.push_back(element.id);
        }
        return result;
    }
    for (std::size_t index = 0; index < elements.size(); ++index) {
        result.push_back(index + 1);
    }
    warnings.append(translated("%1 ID 无效或重复，导出时已连续重新编号。")
                        .arg(entityName));
    return result;
}

bool createExportNumbering(const MeshData& mesh, ExportNumbering& numbering,
                           QString& errorMessage) {
    if (mesh.nodes.size() > MaximumHmAsciiId ||
        mesh.tetrahedra.size() > MaximumHmAsciiId ||
        mesh.surfaceTriangles.size() > MaximumHmAsciiId) {
        errorMessage = translated("网格实体数量超过 HMASCII 子集支持的编号范围。");
        return false;
    }
    std::unordered_set<std::size_t> uniqueNodeIds;
    uniqueNodeIds.reserve(mesh.nodes.size());
    bool preserveNodes = true;
    for (const MeshNode& node : mesh.nodes) {
        preserveNodes = preserveNodes && node.id > 0 &&
                        node.id <= MaximumHmAsciiId &&
                        uniqueNodeIds.insert(node.id).second;
    }
    numbering.nodeIds.reserve(mesh.nodes.size());
    numbering.referencedNodeIds.reserve(mesh.nodes.size());
    for (std::size_t index = 0; index < mesh.nodes.size(); ++index) {
        const std::size_t exportedId = preserveNodes
            ? mesh.nodes[index].id
            : index + 1;
        numbering.nodeIds.push_back(exportedId);
        numbering.referencedNodeIds.try_emplace(mesh.nodes[index].id,
                                                exportedId);
    }
    if (!preserveNodes) {
        numbering.warnings.append(
            translated("节点 ID 无效或重复，导出时已连续重新编号；重复标签按首次出现节点映射。"));
    }

    auto validateConnections = [&](const std::vector<MeshElement>& elements,
                                   std::size_t expectedNodeCount,
                                   const QString& entityName) {
        for (const MeshElement& element : elements) {
            if (element.nodeIds.size() != expectedNodeCount) {
                errorMessage = translated("%1 %2 的节点数量无效。")
                    .arg(entityName)
                    .arg(element.id);
                return false;
            }
            if (hasDuplicateNodeIds(element.nodeIds)) {
                errorMessage = translated("%1 %2 重复引用同一节点。")
                    .arg(entityName)
                    .arg(element.id);
                return false;
            }
            for (const std::size_t nodeId : element.nodeIds) {
                if (!numbering.referencedNodeIds.contains(nodeId)) {
                    errorMessage = translated("%1 %2 引用不存在的节点 %3。")
                        .arg(entityName)
                        .arg(element.id)
                        .arg(nodeId);
                    return false;
                }
            }
        }
        return true;
    };
    if (!validateConnections(mesh.tetrahedra, 4, translated("四面体")) ||
        !validateConnections(mesh.surfaceTriangles, 3, translated("三角形"))) {
        return false;
    }
    numbering.tetrahedronIds = validOrSequentialElementIds(
        mesh.tetrahedra, translated("四面体"), numbering.warnings);
    numbering.triangleIds = validOrSequentialElementIds(
        mesh.surfaceTriangles, translated("三角形"), numbering.warnings);
    return true;
}

} // namespace

HmAsciiImportResult HmAsciiMeshIo::importFile(const QString& filePath) const {
    HmAsciiImportResult result;
    const QFileInfo fileInfo(filePath);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        result.errorMessage = translated("HMASCII 文件不存在：%1").arg(filePath);
        return result;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        result.errorMessage = translated("无法读取 HMASCII 文件：%1").arg(filePath);
        return result;
    }

    try {
        QTextStream stream(&file);
        stream.setEncoding(QStringConverter::Utf8);
        std::unordered_map<std::size_t, std::size_t> nodeIdToIndex;
        std::unordered_set<std::size_t> tetrahedronIds;
        std::unordered_set<std::size_t> triangleIds;
        std::unordered_map<std::size_t, qsizetype> tetrahedronLines;
        std::unordered_map<std::size_t, qsizetype> triangleLines;
        std::unordered_map<QString, std::size_t> unsupportedCommands;
        bool hasComponent = false;
        bool warnedMissingComponent = false;
        std::size_t componentCount = 0;
        qsizetype lineNumber = 0;

        while (!stream.atEnd()) {
            const QString line = stream.readLine();
            ++lineNumber;
            if (line.contains(QChar::ReplacementCharacter)) {
                result.errorMessage = lineError(
                    filePath, lineNumber, translated("文本"),
                    translated("文件不是有效 UTF-8 文本。"));
                return result;
            }
            const QString trimmed = line.trimmed();
            if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('#'))) {
                continue;
            }

            const QString commandName = commandNameFromLine(trimmed);
            if (!isSupportedCommand(commandName)) {
                const QString warningName = commandName.isEmpty()
                    ? translated("非命令文本")
                    : QStringLiteral("*") + commandName;
                ++unsupportedCommands[warningName];
                continue;
            }

            ParsedCommand command;
            QString parseReason;
            if (!parseCommand(trimmed, command, parseReason)) {
                result.errorMessage = lineError(
                    filePath, lineNumber, commandName, parseReason);
                return result;
            }

            if (command.name == QStringLiteral("component")) {
                if (command.arguments.size() < 2) {
                    result.errorMessage = lineError(
                        filePath, lineNumber, command.name,
                        translated("参数数量不足，至少需要组件 ID 和名称。"));
                    return result;
                }
                std::size_t componentId = 0;
                if (!parsePositiveId(command.arguments.at(0), componentId)) {
                    result.errorMessage = lineError(
                        filePath, lineNumber, command.name,
                        translated("组件 ID 必须为正整数。"));
                    return result;
                }
                Q_UNUSED(componentId)
                ++componentCount;
                hasComponent = true;
                if (result.componentName.isEmpty()) {
                    result.componentName = command.arguments.at(1).trimmed();
                }
                continue;
            }

            if (command.name == QStringLiteral("node")) {
                if (command.arguments.size() < 7) {
                    result.errorMessage = lineError(
                        filePath, lineNumber, command.name,
                        translated("参数数量不足，节点至少需要 7 个参数。"));
                    return result;
                }
                MeshNode node;
                if (!parsePositiveId(command.arguments.at(0), node.id)) {
                    result.errorMessage = lineError(
                        filePath, lineNumber, command.name,
                        translated("节点 ID 必须为正整数。"));
                    return result;
                }
                if (nodeIdToIndex.contains(node.id)) {
                    result.errorMessage = lineError(
                        filePath, lineNumber, command.name,
                        translated("节点 ID %1 重复。").arg(node.id));
                    return result;
                }
                if (!parseFiniteDouble(command.arguments.at(1), node.x) ||
                    !parseFiniteDouble(command.arguments.at(2), node.y) ||
                    !parseFiniteDouble(command.arguments.at(3), node.z)) {
                    result.errorMessage = lineError(
                        filePath, lineNumber, command.name,
                        translated("节点坐标必须是有限数值。"));
                    return result;
                }
                bool inputValid = false;
                bool outputValid = false;
                const qlonglong inputSystem =
                    command.arguments.at(5).toLongLong(&inputValid);
                const qlonglong outputSystem =
                    command.arguments.at(6).toLongLong(&outputValid);
                if (!inputValid || !outputValid) {
                    result.errorMessage = lineError(
                        filePath, lineNumber, command.name,
                        translated("输入或输出坐标系 ID 无效。"));
                    return result;
                }
                if (inputSystem != 0 || outputSystem != 0) {
                    result.errorMessage = lineError(
                        filePath, lineNumber, command.name,
                        translated("当前版本暂不支持非全局坐标系节点。"));
                    return result;
                }
                nodeIdToIndex.emplace(node.id, result.mesh.nodes.size());
                result.mesh.nodes.push_back(node);
                continue;
            }

            const bool tetrahedron = command.name == QStringLiteral("tetra4");
            const qsizetype expectedArguments = tetrahedron ? 7 : 6;
            const std::size_t expectedNodes = tetrahedron ? 4 : 3;
            if (command.arguments.size() != expectedArguments) {
                result.errorMessage = lineError(
                    filePath, lineNumber, command.name,
                    translated("参数数量必须为 %1。").arg(expectedArguments));
                return result;
            }
            MeshElement element;
            if (!parsePositiveId(command.arguments.at(0), element.id)) {
                result.errorMessage = lineError(
                    filePath, lineNumber, command.name,
                    translated("单元 ID 必须为正整数。"));
                return result;
            }
            bool typeValid = false;
            bool propertyValid = false;
            command.arguments.at(1).toLongLong(&typeValid);
            command.arguments.constLast().toLongLong(&propertyValid);
            if (!typeValid || !propertyValid) {
                result.errorMessage = lineError(
                    filePath, lineNumber, command.name,
                    translated("单元类型或属性编号必须是整数。"));
                return result;
            }
            std::unordered_set<std::size_t>& ids = tetrahedron
                ? tetrahedronIds
                : triangleIds;
            if (!ids.insert(element.id).second) {
                result.errorMessage = lineError(
                    filePath, lineNumber, command.name,
                    translated("单元 ID %1 重复。").arg(element.id));
                return result;
            }
            element.type = tetrahedron ? GmshTetrahedron4Type
                                       : GmshTriangle3Type;
            for (std::size_t index = 0; index < expectedNodes; ++index) {
                std::size_t nodeId = 0;
                if (!parsePositiveId(command.arguments.at(
                        static_cast<qsizetype>(index + 2)), nodeId)) {
                    result.errorMessage = lineError(
                        filePath, lineNumber, command.name,
                        translated("单元节点 ID 必须为正整数。"));
                    return result;
                }
                element.nodeIds.push_back(nodeId);
            }
            if (hasDuplicateNodeIds(element.nodeIds)) {
                result.errorMessage = lineError(
                    filePath, lineNumber, command.name,
                    translated("同一单元不得重复引用节点。"));
                return result;
            }
            if (!hasComponent && !warnedMissingComponent) {
                result.warnings.append(
                    translated("单元出现在 *component 之前，已使用默认组件名称。"));
                warnedMissingComponent = true;
            }
            if (tetrahedron) {
                tetrahedronLines.emplace(element.id, lineNumber);
                result.mesh.tetrahedra.push_back(std::move(element));
            } else {
                triangleLines.emplace(element.id, lineNumber);
                result.mesh.surfaceTriangles.push_back(std::move(element));
            }
        }

        if (stream.status() != QTextStream::Ok) {
            result.errorMessage = translated("读取 HMASCII 文件时发生文本错误：%1")
                                      .arg(filePath);
            return result;
        }
        if (result.mesh.nodes.empty()) {
            result.errorMessage = translated("HMASCII 文件没有有效节点：%1")
                                      .arg(filePath);
            return result;
        }
        if (result.mesh.tetrahedra.empty()) {
            result.errorMessage = translated("HMASCII 文件没有 tetra4 单元：%1")
                                      .arg(filePath);
            return result;
        }

        auto validateReferences = [&result, &filePath, &nodeIdToIndex](
                                      const std::vector<MeshElement>& elements,
                                      const std::unordered_map<std::size_t,
                                                               qsizetype>& lines,
                                      const QString& commandName,
                                      const QString& entityName) {
            for (const MeshElement& element : elements) {
                for (const std::size_t nodeId : element.nodeIds) {
                    if (!nodeIdToIndex.contains(nodeId)) {
                        result.errorMessage = lineError(
                            filePath, lines.at(element.id), commandName,
                            translated("%1 %2 引用不存在的节点 %3。")
                                .arg(entityName)
                                .arg(element.id)
                                .arg(nodeId));
                        return false;
                    }
                }
            }
            return true;
        };
        if (!validateReferences(result.mesh.tetrahedra, tetrahedronLines,
                                QStringLiteral("tetra4"),
                                translated("四面体")) ||
            !validateReferences(result.mesh.surfaceTriangles, triangleLines,
                                QStringLiteral("tria3"),
                                translated("三角形"))) {
            return result;
        }

        if (result.mesh.surfaceTriangles.empty()) {
            result.mesh.surfaceTriangles =
                extractBoundaryTriangles(result.mesh.tetrahedra);
            result.warnings.append(
                translated("文件没有 tria3，已从 tetra4 提取外表面三角形。"));
        }
        if (result.mesh.surfaceTriangles.empty()) {
            result.errorMessage = translated("未能生成有效表面三角形：%1")
                                      .arg(filePath);
            return result;
        }
        if (componentCount > 1) {
            result.warnings.append(
                translated("文件包含 %1 个 Component，已合并为一个网格对象。")
                    .arg(componentCount));
        }
        if (result.componentName.isEmpty()) {
            result.componentName = QStringLiteral("Imported HMASCII Mesh");
        }
        if (!unsupportedCommands.empty()) {
            QStringList descriptions;
            std::size_t total = 0;
            std::vector<std::pair<QString, std::size_t>> sortedCommands(
                unsupportedCommands.cbegin(), unsupportedCommands.cend());
            std::sort(sortedCommands.begin(), sortedCommands.end(),
                      [](const auto& left, const auto& right) {
                          return left.first < right.first;
                      });
            for (const auto& [name, count] : sortedCommands) {
                total += count;
                descriptions.append(QStringLiteral("%1 (%2)").arg(name).arg(count));
            }
            result.warnings.append(
                translated("已跳过 %1 条不支持的 HMASCII 命令：%2")
                    .arg(total)
                    .arg(descriptions.join(QStringLiteral("、"))));
        }
        result.success = true;
        return result;
    } catch (const std::exception& exception) {
        result.errorMessage = translated("HMASCII 导入异常：%1")
                                  .arg(QString::fromUtf8(exception.what()));
        return result;
    } catch (...) {
        result.errorMessage = translated("HMASCII 导入发生未知异常。");
        return result;
    }
}

HmAsciiIoResult HmAsciiMeshIo::exportFile(
    const QString& filePath, const MeshData& mesh,
    const QString& componentName) const {
    HmAsciiIoResult result;
    if (mesh.nodes.empty()) {
        result.errorMessage = translated("当前网格没有节点，无法导出。");
        return result;
    }
    if (mesh.tetrahedra.empty()) {
        result.errorMessage = translated("当前网格没有四面体，无法导出。");
        return result;
    }
    for (const MeshNode& node : mesh.nodes) {
        if (!std::isfinite(node.x) || !std::isfinite(node.y) ||
            !std::isfinite(node.z)) {
            result.errorMessage = translated("节点 %1 包含 NaN 或无穷大坐标。")
                                      .arg(node.id);
            return result;
        }
    }

    try {
        ExportNumbering numbering;
        if (!createExportNumbering(mesh, numbering, result.errorMessage)) {
            return result;
        }

        QSaveFile file(filePath);
        file.setDirectWriteFallback(false);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            result.errorMessage = translated("无法创建 HMASCII 临时输出文件：%1")
                                      .arg(filePath);
            return result;
        }
        QTextStream stream(&file);
        stream.setEncoding(QStringConverter::Utf8);
        stream << "*component(1,\"" << safeComponentName(componentName)
               << "\",0,3,0)\n\n";
        for (std::size_t index = 0; index < mesh.nodes.size(); ++index) {
            const MeshNode& node = mesh.nodes[index];
            stream << "*node(" << numbering.nodeIds[index] << ','
                   << QString::number(node.x, 'g', 17) << ','
                   << QString::number(node.y, 'g', 17) << ','
                   << QString::number(node.z, 'g', 17)
                   << ",0,0,0,0,0)\n";
        }
        stream << '\n';
        for (std::size_t index = 0; index < mesh.tetrahedra.size(); ++index) {
            const MeshElement& element = mesh.tetrahedra[index];
            stream << "*tetra4(" << numbering.tetrahedronIds[index] << ",0";
            for (const std::size_t nodeId : element.nodeIds) {
                stream << ',' << numbering.referencedNodeIds.at(nodeId);
            }
            stream << ",0)\n";
        }
        if (!mesh.surfaceTriangles.empty()) {
            stream << '\n';
        }
        for (std::size_t index = 0;
             index < mesh.surfaceTriangles.size(); ++index) {
            const MeshElement& element = mesh.surfaceTriangles[index];
            stream << "*tria3(" << numbering.triangleIds[index] << ",0";
            for (const std::size_t nodeId : element.nodeIds) {
                stream << ',' << numbering.referencedNodeIds.at(nodeId);
            }
            stream << ",0)\n";
        }
        stream.flush();
        if (stream.status() != QTextStream::Ok) {
            file.cancelWriting();
            result.errorMessage = translated("写入 HMASCII 数据失败：%1")
                                      .arg(filePath);
            return result;
        }
        if (!file.commit()) {
            result.errorMessage = translated("无法安全替换 HMASCII 文件：%1")
                                      .arg(filePath);
            return result;
        }
        result.warnings = numbering.warnings;
        result.success = true;
        return result;
    } catch (const std::exception& exception) {
        result.errorMessage = translated("HMASCII 导出异常：%1")
                                  .arg(QString::fromUtf8(exception.what()));
        return result;
    } catch (...) {
        result.errorMessage = translated("HMASCII 导出发生未知异常。");
        return result;
    }
}
