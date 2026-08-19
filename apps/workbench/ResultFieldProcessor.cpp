#include "ResultFieldProcessor.hpp"

#include <vtkCellData.h>
#include <vtkDataArray.h>
#include <vtkDataSetAttributes.h>
#include <vtkDoubleArray.h>
#include <vtkIdTypeArray.h>
#include <vtkPointData.h>
#include <vtkUnstructuredGrid.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <exception>
#include <limits>
#include <optional>
#include <unordered_map>

namespace {

std::string normalize(std::string value) {
    value.erase(
        std::remove_if(value.begin(), value.end(),
                       [](unsigned char character) {
                           return std::isalnum(character) == 0;
                       }),
        value.end());
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char character) {
                       return static_cast<char>(std::tolower(character));
                   });
    return value;
}

std::string operationSuffix(ResultScalarOperation operation,
                            int component) {
    if (operation == ResultScalarOperation::Magnitude) {
        return "Magnitude";
    }
    if (operation == ResultScalarOperation::VonMises) {
        return "VonMises";
    }
    return "Component" + std::to_string(component);
}

std::string derivedName(const std::string& source,
                        ResultScalarOperation operation,
                        int component) {
    return "QTCAE.Derived." + source + "." +
           operationSuffix(operation, component);
}

std::optional<int> namedComponent(
    const ResultFieldInfo& field,
    const std::vector<std::string>& acceptedNames) {
    for (int index = 0; index < field.componentCount; ++index) {
        if (index >= static_cast<int>(field.componentNames.size())) {
            break;
        }
        const std::string candidate =
            normalize(field.componentNames[static_cast<std::size_t>(index)]);
        for (const std::string& accepted : acceptedNames) {
            if (candidate == normalize(accepted)) {
                return index;
            }
        }
    }
    return std::nullopt;
}

struct StressComponents {
    int xx{-1};
    int yy{-1};
    int zz{-1};
    int xy{-1};
    int yz{-1};
    int zx{-1};
    bool usedConvention{false};
};

std::optional<StressComponents> stressComponents(
    const ResultFieldInfo& field) {
    if (!field.stressTensor ||
        (field.componentCount != 6 && field.componentCount != 9)) {
        return std::nullopt;
    }
    StressComponents components;
    const auto find = [&field](
                          std::initializer_list<const char*> names) {
        std::vector<std::string> values;
        values.reserve(names.size());
        for (const char* name : names) {
            values.emplace_back(name);
        }
        return namedComponent(field, values).value_or(-1);
    };
    components.xx = find({"Sxx", "xx", "sigmaxx"});
    components.yy = find({"Syy", "yy", "sigmayy"});
    components.zz = find({"Szz", "zz", "sigmazz"});
    components.xy = find({"Sxy", "xy", "sigmaxy"});
    components.yz = find({"Syz", "yz", "sigmayz"});
    components.zx = find({"Szx", "Sxz", "zx", "xz", "sigmazx",
                          "sigmaxz"});
    if (components.xx >= 0 && components.yy >= 0 &&
        components.zz >= 0 && components.xy >= 0 &&
        components.yz >= 0 && components.zx >= 0) {
        return components;
    }
    components.usedConvention = true;
    if (field.componentCount == 6) {
        components = {0, 1, 2, 3, 4, 5, true};
    } else {
        // 常见三维张量按行展开：
        // xx, xy, xz, yx, yy, yz, zx, zy, zz。
        components = {0, 4, 8, 1, 5, 6, true};
    }
    return components;
}

long long tupleId(vtkUnstructuredGrid* grid,
                  ResultFieldAssociation association,
                  vtkIdType tuple) {
    vtkDataSetAttributes* attributes =
        association == ResultFieldAssociation::Point
        ? static_cast<vtkDataSetAttributes*>(grid->GetPointData())
        : static_cast<vtkDataSetAttributes*>(grid->GetCellData());
    vtkDataArray* globalIds =
        attributes == nullptr ? nullptr : attributes->GetGlobalIds();
    if (globalIds != nullptr &&
        globalIds->GetNumberOfTuples() > tuple) {
        return static_cast<long long>(
            globalIds->GetComponent(tuple, 0));
    }
    return static_cast<long long>(tuple) + 1;
}

} // namespace

std::vector<ResultScalarOption> ResultFieldProcessor::scalarOptions(
    const ResultFieldInfo& field) const {
    std::vector<ResultScalarOption> options;
    const auto appendComponent =
        [&options, &field](std::string label, int component) {
            options.push_back({
                .displayName = std::move(label),
                .sourceArrayName = field.name,
                .derivedArrayName = derivedName(
                    field.name, ResultScalarOperation::Component,
                    component),
                .association = field.association,
                .operation = ResultScalarOperation::Component,
                .component = component
            });
        };
    if (field.componentCount == 1) {
        appendComponent(field.name, 0);
        return options;
    }
    if (field.componentCount == 3) {
        appendComponent("X", 0);
        appendComponent("Y", 1);
        appendComponent("Z", 2);
        options.push_back({
            .displayName = "模",
            .sourceArrayName = field.name,
            .derivedArrayName = derivedName(
                field.name, ResultScalarOperation::Magnitude, -1),
            .association = field.association,
            .operation = ResultScalarOperation::Magnitude,
            .component = -1
        });
        return options;
    }
    if (const auto stress = stressComponents(field)) {
        appendComponent("Sxx", stress->xx);
        appendComponent("Syy", stress->yy);
        appendComponent("Szz", stress->zz);
        appendComponent("Sxy", stress->xy);
        appendComponent("Syz", stress->yz);
        appendComponent("Szx", stress->zx);
        options.push_back({
            .displayName = "Von Mises",
            .sourceArrayName = field.name,
            .derivedArrayName = derivedName(
                field.name, ResultScalarOperation::VonMises, -1),
            .association = field.association,
            .operation = ResultScalarOperation::VonMises,
            .component = -1
        });
        return options;
    }
    for (int component = 0; component < field.componentCount;
         ++component) {
        std::string label;
        if (component < static_cast<int>(field.componentNames.size())) {
            label = field.componentNames[
                static_cast<std::size_t>(component)];
        }
        if (label.empty()) {
            label = "分量 " + std::to_string(component + 1);
        }
        appendComponent(std::move(label), component);
    }
    return options;
}

ResultScalarBuildResult ResultFieldProcessor::buildScalar(
    vtkUnstructuredGrid* grid,
    const ResultScalarOption& option) const {
    ResultScalarBuildResult result;
    try {
        if (grid == nullptr) {
            result.errorMessage = "结果网格为空。";
            return result;
        }
        vtkDataArray* source = findArray(
            grid, option.sourceArrayName, option.association);
        if (source == nullptr) {
            result.errorMessage = "找不到指定结果数组。";
            return result;
        }
        const vtkIdType expectedTuples =
            option.association == ResultFieldAssociation::Point
            ? grid->GetNumberOfPoints()
            : grid->GetNumberOfCells();
        if (source->GetNumberOfTuples() != expectedTuples) {
            result.errorMessage = "结果数组元组数量与网格不匹配。";
            return result;
        }
        const int components = source->GetNumberOfComponents();
        if (option.operation == ResultScalarOperation::Component &&
            (option.component < 0 || option.component >= components)) {
            result.errorMessage = "结果分量索引无效。";
            return result;
        }
        if (option.operation == ResultScalarOperation::Magnitude &&
            components != 3) {
            result.errorMessage = "矢量模只支持三分量数组。";
            return result;
        }

        ResultFieldInfo stressField;
        std::optional<StressComponents> stress;
        if (option.operation == ResultScalarOperation::VonMises) {
            stressField.name = option.sourceArrayName;
            stressField.association = option.association;
            stressField.componentCount = components;
            stressField.stressTensor = true;
            stressField.componentNames.reserve(
                static_cast<std::size_t>(components));
            for (int component = 0; component < components; ++component) {
                const char* componentName =
                    source->GetComponentName(component);
                stressField.componentNames.emplace_back(
                    componentName == nullptr ? "" : componentName);
            }
            stress = stressComponents(stressField);
            if (!stress) {
                result.errorMessage =
                    "当前数组不能解释为三维应力张量。";
                return result;
            }
            if (stress->usedConvention) {
                result.warnings.emplace_back(
                    "结果文件未提供应力分量名称，当前按约定顺序解释，"
                    "请核对数据来源。");
            }
        }

        auto derived = vtkSmartPointer<vtkDoubleArray>::New();
        derived->SetName(option.derivedArrayName.c_str());
        derived->SetNumberOfComponents(1);
        derived->SetNumberOfTuples(expectedTuples);

        double minimum = std::numeric_limits<double>::infinity();
        double maximum = -std::numeric_limits<double>::infinity();
        vtkIdType minimumTuple = -1;
        vtkIdType maximumTuple = -1;
        for (vtkIdType tuple = 0; tuple < expectedTuples; ++tuple) {
            double value = std::numeric_limits<double>::quiet_NaN();
            if (option.operation == ResultScalarOperation::Component) {
                value = source->GetComponent(tuple, option.component);
            } else if (option.operation ==
                       ResultScalarOperation::Magnitude) {
                const double x = source->GetComponent(tuple, 0);
                const double y = source->GetComponent(tuple, 1);
                const double z = source->GetComponent(tuple, 2);
                if (std::isfinite(x) && std::isfinite(y) &&
                    std::isfinite(z)) {
                    value = std::sqrt(x * x + y * y + z * z);
                }
            } else {
                const double xx =
                    source->GetComponent(tuple, stress->xx);
                const double yy =
                    source->GetComponent(tuple, stress->yy);
                const double zz =
                    source->GetComponent(tuple, stress->zz);
                const double xy =
                    source->GetComponent(tuple, stress->xy);
                const double yz =
                    source->GetComponent(tuple, stress->yz);
                const double zx =
                    source->GetComponent(tuple, stress->zx);
                if (std::isfinite(xx) && std::isfinite(yy) &&
                    std::isfinite(zz) && std::isfinite(xy) &&
                    std::isfinite(yz) && std::isfinite(zx)) {
                    const double normal =
                        ((xx - yy) * (xx - yy) +
                         (yy - zz) * (yy - zz) +
                         (zz - xx) * (zz - xx)) /
                        2.0;
                    const double shear =
                        3.0 * (xy * xy + yz * yz + zx * zx);
                    value = std::sqrt(std::max(0.0, normal + shear));
                }
            }
            derived->SetValue(tuple, value);
            if (!std::isfinite(value)) {
                ++result.statistics.ignoredValueCount;
                continue;
            }
            if (value < minimum) {
                minimum = value;
                minimumTuple = tuple;
            }
            if (value > maximum) {
                maximum = value;
                maximumTuple = tuple;
            }
        }
        if (minimumTuple < 0 || maximumTuple < 0) {
            result.errorMessage = "当前结果字段没有有效有限数值。";
            return result;
        }

        vtkDataSetAttributes* target =
            option.association == ResultFieldAssociation::Point
            ? static_cast<vtkDataSetAttributes*>(grid->GetPointData())
            : static_cast<vtkDataSetAttributes*>(grid->GetCellData());
        target->RemoveArray(option.derivedArrayName.c_str());
        target->AddArray(derived);

        result.statistics.success = true;
        result.statistics.minimum = minimum;
        result.statistics.maximum = maximum;
        result.statistics.minimumId =
            tupleId(grid, option.association, minimumTuple);
        result.statistics.maximumId =
            tupleId(grid, option.association, maximumTuple);
        if (option.association == ResultFieldAssociation::Point) {
            grid->GetPoint(minimumTuple,
                           result.statistics.minimumPosition.data());
            grid->GetPoint(maximumTuple,
                           result.statistics.maximumPosition.data());
            result.statistics.hasPositions = true;
        }
        if (result.statistics.ignoredValueCount > 0) {
            result.warnings.emplace_back(
                "当前字段忽略了 " +
                std::to_string(result.statistics.ignoredValueCount) +
                " 个 NaN 或无穷值。");
        }
        result.success = true;
        result.arrayName = option.derivedArrayName;
        return result;
    } catch (const std::exception& exception) {
        result.errorMessage =
            std::string("结果字段处理异常：") + exception.what();
    } catch (...) {
        result.errorMessage = "结果字段处理发生未知异常。";
    }
    return result;
}

ResultValidationResult
ResultFieldProcessor::validateDisplacementField(
    vtkUnstructuredGrid* grid,
    const std::string& arrayName) const {
    if (grid == nullptr) {
        return {.errorMessage = "结果网格为空。"};
    }
    vtkDataArray* array = findArray(
        grid, arrayName, ResultFieldAssociation::Point);
    if (array == nullptr) {
        return {.errorMessage = "找不到指定节点位移数组。"};
    }
    if (array->GetNumberOfComponents() != 3) {
        return {.errorMessage = "位移字段必须是三分量节点矢量。"};
    }
    if (array->GetNumberOfTuples() != grid->GetNumberOfPoints()) {
        return {.errorMessage = "位移字段元组数量与节点数量不匹配。"};
    }
    for (vtkIdType tuple = 0; tuple < array->GetNumberOfTuples(); ++tuple) {
        for (int component = 0; component < 3; ++component) {
            if (!std::isfinite(
                    array->GetComponent(tuple, component))) {
                return {.errorMessage =
                            "位移字段包含 NaN 或无穷值。"};
            }
        }
    }
    return {.success = true};
}

ResultValidationResult
ResultFieldProcessor::validateDeformationScale(double scale) const {
    if (!std::isfinite(scale)) {
        return {.errorMessage = "变形倍率必须是有限数。"};
    }
    if (scale < 0.0) {
        return {.errorMessage = "变形倍率不能小于零。"};
    }
    return {.success = true};
}

ResultValidationResult ResultFieldProcessor::validateScalarRange(
    double minimum, double maximum) const {
    if (!std::isfinite(minimum) || !std::isfinite(maximum)) {
        return {.errorMessage = "云图范围必须使用有限数值。"};
    }
    if (minimum >= maximum) {
        return {.errorMessage = "云图最小值必须小于最大值。"};
    }
    return {.success = true};
}

vtkDataArray* ResultFieldProcessor::findArray(
    vtkUnstructuredGrid* grid,
    const std::string& name,
    ResultFieldAssociation association) const {
    if (grid == nullptr || name.empty()) {
        return nullptr;
    }
    return association == ResultFieldAssociation::Point
        ? grid->GetPointData()->GetArray(name.c_str())
        : grid->GetCellData()->GetArray(name.c_str());
}
