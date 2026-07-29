#include "VtkResultReader.hpp"

#include <vtkAbstractArray.h>
#include <vtkCellData.h>
#include <vtkDataArray.h>
#include <vtkDataSetAttributes.h>
#include <vtkErrorCode.h>
#include <vtkPointData.h>
#include <vtkUnstructuredGrid.h>
#include <vtkUnstructuredGridReader.h>
#include <vtkXMLUnstructuredGridReader.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <exception>
#include <system_error>

namespace {

std::string lowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char character) {
                       return static_cast<char>(std::tolower(character));
                   });
    return value;
}

bool isStressName(const std::string& name) {
    std::string normalized;
    for (const unsigned char character : name) {
        if (std::isalnum(character) != 0) {
            normalized.push_back(
                static_cast<char>(std::tolower(character)));
        }
    }
    return normalized == "s" ||
           normalized.find("stress") != std::string::npos ||
           normalized.find("sigma") != std::string::npos;
}

std::string pathForVtk(const std::filesystem::path& path) {
#ifdef _WIN32
    const auto utf8 = path.u8string();
    return {reinterpret_cast<const char*>(utf8.data()), utf8.size()};
#else
    return path.string();
#endif
}

bool enumerateArrays(vtkDataSetAttributes* attributes,
                     ResultFieldAssociation association,
                     vtkIdType expectedTupleCount,
                     std::vector<ResultFieldInfo>& fields,
                     std::vector<std::string>& warnings,
                     std::string& errorMessage) {
    if (attributes == nullptr) {
        return true;
    }
    for (int index = 0; index < attributes->GetNumberOfArrays(); ++index) {
        vtkAbstractArray* abstractArray =
            attributes->GetAbstractArray(index);
        if (abstractArray == nullptr) {
            continue;
        }
        if (abstractArray == attributes->GetGlobalIds()) {
            continue;
        }
        const char* rawName = abstractArray->GetName();
        if (rawName == nullptr || *rawName == '\0') {
            warnings.emplace_back("已忽略名称为空的结果数组。");
            continue;
        }
        vtkDataArray* array = vtkDataArray::SafeDownCast(abstractArray);
        if (array == nullptr) {
            warnings.emplace_back(
                std::string("已忽略非数值结果数组：") + rawName);
            continue;
        }
        if (array->GetNumberOfTuples() != expectedTupleCount) {
            errorMessage = std::string("结果数组“") + rawName +
                "”的元组数量与网格不匹配。";
            return false;
        }
        const int componentCount = array->GetNumberOfComponents();
        if (componentCount <= 0) {
            errorMessage = std::string("结果数组“") + rawName +
                "”没有有效分量。";
            return false;
        }

        ResultFieldInfo field;
        field.name = rawName;
        field.association = association;
        field.componentCount = componentCount;
        field.tupleCount =
            static_cast<std::size_t>(array->GetNumberOfTuples());
        field.stressTensor =
            (componentCount == 6 || componentCount == 9) &&
            isStressName(field.name);
        field.componentNames.reserve(
            static_cast<std::size_t>(componentCount));
        for (int component = 0; component < componentCount; ++component) {
            const char* componentName = array->GetComponentName(component);
            field.componentNames.emplace_back(
                componentName == nullptr ? "" : componentName);
        }

        std::size_t invalidValueCount = 0;
        for (vtkIdType tuple = 0; tuple < array->GetNumberOfTuples();
             ++tuple) {
            for (int component = 0; component < componentCount;
                 ++component) {
                if (!std::isfinite(
                        array->GetComponent(tuple, component))) {
                    ++invalidValueCount;
                }
            }
        }
        if (invalidValueCount > 0) {
            warnings.emplace_back(
                std::string("结果数组“") + rawName + "”包含 " +
                std::to_string(invalidValueCount) +
                " 个非有限数值，显示时将忽略。");
        }
        if (componentCount != 1 && componentCount != 3 &&
            componentCount != 6 && componentCount != 9) {
            warnings.emplace_back(
                std::string("结果数组“") + rawName + "”包含 " +
                std::to_string(componentCount) +
                " 个分量，当前仅提供逐分量显示。");
        }
        if (field.stressTensor) {
            const bool allComponentNamesPresent = std::all_of(
                field.componentNames.begin(), field.componentNames.end(),
                [](const std::string& value) { return !value.empty(); });
            if (!allComponentNamesPresent) {
                warnings.emplace_back(
                    "结果文件未提供应力分量名称，当前按约定顺序解释，"
                    "请核对数据来源。");
            }
        }
        fields.push_back(std::move(field));
    }
    return true;
}

vtkSmartPointer<vtkUnstructuredGrid> readXmlGrid(
    const std::string& filePath, std::string& errorMessage) {
    auto reader =
        vtkSmartPointer<vtkXMLUnstructuredGridReader>::New();
    if (reader->CanReadFile(filePath.c_str()) == 0) {
        errorMessage = "VTU 文件格式无效或无法读取。";
        return nullptr;
    }
    reader->SetFileName(filePath.c_str());
    reader->Update();
    if (reader->GetErrorCode() != vtkErrorCode::NoError) {
        errorMessage = std::string("VTU 读取失败：") +
            vtkErrorCode::GetStringFromErrorCode(
                reader->GetErrorCode());
        return nullptr;
    }
    vtkUnstructuredGrid* output = reader->GetOutput();
    if (output == nullptr) {
        errorMessage = "VTU 读取器没有输出非结构网格。";
        return nullptr;
    }
    auto grid = vtkSmartPointer<vtkUnstructuredGrid>::New();
    grid->DeepCopy(output);
    return grid;
}

vtkSmartPointer<vtkUnstructuredGrid> readLegacyGrid(
    const std::string& filePath, std::string& errorMessage) {
    auto reader =
        vtkSmartPointer<vtkUnstructuredGridReader>::New();
    reader->SetFileName(filePath.c_str());
    if (reader->IsFileUnstructuredGrid() == 0) {
        errorMessage =
            "Legacy VTK 文件不是有效的非结构网格数据集。";
        return nullptr;
    }
    reader->ReadAllScalarsOn();
    reader->ReadAllVectorsOn();
    reader->ReadAllTensorsOn();
    reader->ReadAllFieldsOn();
    reader->Update();
    if (reader->GetErrorCode() != vtkErrorCode::NoError) {
        errorMessage = std::string("Legacy VTK 读取失败：") +
            vtkErrorCode::GetStringFromErrorCode(
                reader->GetErrorCode());
        return nullptr;
    }
    vtkUnstructuredGrid* output = reader->GetOutput();
    if (output == nullptr) {
        errorMessage =
            "Legacy VTK 读取器没有输出非结构网格。";
        return nullptr;
    }
    auto grid = vtkSmartPointer<vtkUnstructuredGrid>::New();
    grid->DeepCopy(output);
    return grid;
}

} // namespace

VtkReadResult VtkResultReader::read(
    const std::filesystem::path& filePath) const {
    VtkReadResult result;
    try {
        if (filePath.empty()) {
            result.errorMessage = "VTK 结果文件路径为空。";
            return result;
        }
        std::error_code error;
        if (!std::filesystem::exists(filePath, error) || error) {
            result.errorMessage = "VTK 结果文件不存在。";
            return result;
        }
        if (!std::filesystem::is_regular_file(filePath, error) || error) {
            result.errorMessage = "VTK 结果路径不是普通文件。";
            return result;
        }
        const std::string extension =
            lowerAscii(filePath.extension().string());
        if (extension != ".vtu" && extension != ".vtk") {
            result.errorMessage =
                "当前只支持 .vtu 和 .vtk 结果文件。";
            return result;
        }

        const std::string vtkPath = pathForVtk(filePath);
        result.grid = extension == ".vtu"
            ? readXmlGrid(vtkPath, result.errorMessage)
            : readLegacyGrid(vtkPath, result.errorMessage);
        if (result.grid == nullptr) {
            return result;
        }
        if (result.grid->GetNumberOfPoints() <= 0) {
            result.errorMessage = "VTK 结果网格没有节点。";
            result.grid = nullptr;
            return result;
        }
        if (result.grid->GetNumberOfCells() <= 0) {
            result.errorMessage = "VTK 结果网格没有单元。";
            result.grid = nullptr;
            return result;
        }

        if (!enumerateArrays(
                result.grid->GetPointData(),
                ResultFieldAssociation::Point,
                result.grid->GetNumberOfPoints(), result.fields,
                result.warnings, result.errorMessage) ||
            !enumerateArrays(
                result.grid->GetCellData(),
                ResultFieldAssociation::Cell,
                result.grid->GetNumberOfCells(), result.fields,
                result.warnings, result.errorMessage)) {
            result.grid = nullptr;
            result.fields.clear();
            return result;
        }
        result.success = true;
        return result;
    } catch (const std::exception& exception) {
        result.errorMessage =
            std::string("VTK 结果读取异常：") + exception.what();
    } catch (...) {
        result.errorMessage = "VTK 结果读取发生未知异常。";
    }
    result.grid = nullptr;
    result.fields.clear();
    return result;
}
