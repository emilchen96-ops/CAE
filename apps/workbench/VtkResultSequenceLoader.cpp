#include "VtkResultSequenceLoader.hpp"

#include "VtkResultReader.hpp"

#include <vtkAbstractArray.h>
#include <vtkAppendFilter.h>
#include <vtkCellData.h>
#include <vtkDataArray.h>
#include <vtkDataSetAttributes.h>
#include <vtkPointData.h>
#include <vtkUnstructuredGrid.h>

#include <algorithm>
#include <exception>
#include <map>
#include <set>
#include <tuple>

namespace {

using FieldKey =
    std::tuple<ResultFieldAssociation, std::string, int, int>;

FieldKey fieldKey(vtkUnstructuredGrid* grid,
                  const ResultFieldInfo& field) {
    vtkDataArray* array =
        field.association == ResultFieldAssociation::Point
        ? grid->GetPointData()->GetArray(field.name.c_str())
        : grid->GetCellData()->GetArray(field.name.c_str());
    return {field.association, field.name, field.componentCount,
            array == nullptr ? -1 : array->GetDataType()};
}

std::string associationName(ResultFieldAssociation association) {
    return association == ResultFieldAssociation::Point ? "节点" : "单元";
}

void retainArrays(vtkDataSetAttributes* attributes,
                  ResultFieldAssociation association,
                  const std::set<FieldKey>& commonFields) {
    if (attributes == nullptr) {
        return;
    }
    std::vector<std::string> removeNames;
    for (int index = 0; index < attributes->GetNumberOfArrays();
         ++index) {
        vtkAbstractArray* abstractArray =
            attributes->GetAbstractArray(index);
        const char* rawName =
            abstractArray == nullptr ? nullptr : abstractArray->GetName();
        vtkDataArray* array = vtkDataArray::SafeDownCast(abstractArray);
        if (rawName == nullptr || array == nullptr ||
            !commonFields.contains(
                {association, rawName, array->GetNumberOfComponents(),
                 array->GetDataType()})) {
            if (rawName != nullptr) {
                removeNames.emplace_back(rawName);
            }
        }
    }
    for (const std::string& name : removeNames) {
        attributes->RemoveArray(name.c_str());
    }
}

} // namespace

VtkResultSequenceLoadResult VtkResultSequenceLoader::load(
    const VtkResultSequenceFrame& frame) const {
    VtkResultSequenceLoadResult result;
    try {
        if (frame.parts.empty()) {
            result.errorMessage = "当前帧没有可加载的结果文件。";
            return result;
        }
        const VtkResultReader reader;
        std::vector<VtkReadResult> reads;
        reads.reserve(frame.parts.size());
        for (const auto& part : frame.parts) {
            VtkReadResult read = reader.read(part.filePath);
            if (!read.success || read.grid == nullptr) {
                result.errorMessage =
                    "帧 " + std::to_string(frame.frameNumber) +
                    " 的 " + part.name + " 文件加载失败（" +
                    part.filePath.string() + "）：" +
                    (read.errorMessage.empty()
                         ? std::string("未知错误")
                         : read.errorMessage);
                return result;
            }
            result.loadedParts.push_back(part.name);
            result.warnings.insert(result.warnings.end(),
                                   read.warnings.begin(),
                                   read.warnings.end());
            reads.push_back(std::move(read));
        }

        std::set<FieldKey> commonFields;
        std::set<std::pair<ResultFieldAssociation, std::string>>
            allFieldIdentities;
        for (const ResultFieldInfo& field : reads.front().fields) {
            const FieldKey key = fieldKey(reads.front().grid, field);
            commonFields.insert(key);
            allFieldIdentities.emplace(
                field.association, field.name);
        }
        for (std::size_t index = 1; index < reads.size(); ++index) {
            std::set<FieldKey> current;
            for (const ResultFieldInfo& field : reads[index].fields) {
                current.insert(fieldKey(reads[index].grid, field));
                allFieldIdentities.emplace(
                    field.association, field.name);
            }
            std::erase_if(commonFields, [&current](const FieldKey& key) {
                return !current.contains(key);
            });
        }
        for (const auto& identity : allFieldIdentities) {
            const bool compatible = std::any_of(
                commonFields.begin(), commonFields.end(),
                [&identity](const FieldKey& key) {
                    return std::get<0>(key) == identity.first &&
                           std::get<1>(key) == identity.second;
                });
            if (!compatible) {
                result.warnings.push_back(
                    "结果字段“" + identity.second + "”（" +
                    associationName(identity.first) +
                    "）未在当前帧所有对象中兼容存在，已忽略。");
            }
        }

        auto append = vtkSmartPointer<vtkAppendFilter>::New();
        append->MergePointsOff();
        std::vector<vtkSmartPointer<vtkUnstructuredGrid>> cleanGrids;
        cleanGrids.reserve(reads.size());
        for (const VtkReadResult& read : reads) {
            auto clean = vtkSmartPointer<vtkUnstructuredGrid>::New();
            clean->DeepCopy(read.grid);
            retainArrays(clean->GetPointData(),
                         ResultFieldAssociation::Point, commonFields);
            retainArrays(clean->GetCellData(),
                         ResultFieldAssociation::Cell, commonFields);
            append->AddInputData(clean);
            cleanGrids.push_back(std::move(clean));
        }
        append->Update();
        vtkUnstructuredGrid* output = append->GetOutput();
        if (output == nullptr || output->GetNumberOfPoints() <= 0 ||
            output->GetNumberOfCells() <= 0) {
            result.errorMessage =
                "当前帧的 solid/bolt 合并后没有有效网格。";
            return result;
        }
        result.grid = vtkSmartPointer<vtkUnstructuredGrid>::New();
        result.grid->DeepCopy(output);

        for (const ResultFieldInfo& field : reads.front().fields) {
            if (!commonFields.contains(
                    fieldKey(reads.front().grid, field))) {
                continue;
            }
            ResultFieldInfo combined = field;
            combined.tupleCount = static_cast<std::size_t>(
                field.association == ResultFieldAssociation::Point
                ? result.grid->GetNumberOfPoints()
                : result.grid->GetNumberOfCells());
            result.fields.push_back(std::move(combined));
        }
        result.success = true;
    } catch (const std::exception& exception) {
        result.errorMessage =
            std::string("加载 VTK 结果帧异常：") + exception.what();
    } catch (...) {
        result.errorMessage = "加载 VTK 结果帧发生未知异常。";
    }
    if (!result.success) {
        result.grid = nullptr;
        result.fields.clear();
    }
    return result;
}
