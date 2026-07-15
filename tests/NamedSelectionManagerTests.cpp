#include "emilcae/core/NamedSelectionManager.hpp"

#include <iostream>
#include <unordered_set>

using namespace emilcae::core;

namespace {

NamedSelectionItem item(int geometryObjectId,
                        NamedSelectionEntityType type,
                        int localIndex) {
    return {geometryObjectId, type, localIndex};
}

} // namespace

int main() {
    int failures = 0;
    auto check = [&failures](bool condition, const char* name) {
        if (!condition) {
            ++failures;
            std::cerr << "FAILED: " << name << '\n';
        }
    };

    std::unordered_set<int> geometryObjects{1, 2};
    NamedSelectionManager manager(
        [&geometryObjects](int id) { return geometryObjects.contains(id); });

    const auto created = manager.create(
        " Fixed Faces ", NamedSelectionEntityType::Face,
        {item(1, NamedSelectionEntityType::Face, 3),
         item(1, NamedSelectionEntityType::Face, 3),
         item(2, NamedSelectionEntityType::Face, 8)});
    check(created.success && created.namedSelectionId == 1,
          "create named selection");
    const NamedSelection* selection = manager.find(created.namedSelectionId);
    check(selection != nullptr && selection->name == "Fixed Faces" &&
              selection->items.size() == 2,
          "find selection and remove duplicate members");

    const auto duplicateName = manager.create(
        "Fixed Faces", NamedSelectionEntityType::Face,
        {item(1, NamedSelectionEntityType::Face, 1)});
    check(duplicateName.error == NamedSelectionError::DuplicateName,
          "reject duplicate name");
    check(manager.create("Mixed", NamedSelectionEntityType::Face,
                         {item(1, NamedSelectionEntityType::Edge, 1)})
              .error == NamedSelectionError::MixedEntityTypes,
          "reject mixed entity types");
    check(manager.create("Missing", NamedSelectionEntityType::Face,
                         {item(99, NamedSelectionEntityType::Face, 1)})
              .error == NamedSelectionError::InvalidGeometryObject,
          "reject missing geometry object");
    check(manager.create("Bad Index", NamedSelectionEntityType::Face,
                         {item(1, NamedSelectionEntityType::Face, 0)})
              .error == NamedSelectionError::InvalidLocalIndex,
          "reject invalid subshape index");
    check(manager.create("Object Set", NamedSelectionEntityType::Object,
                         {item(1, NamedSelectionEntityType::Object, 0)})
              .success,
          "accept object member index zero");

    check(manager.addItems(
              created.namedSelectionId,
              {item(1, NamedSelectionEntityType::Face, 3),
               item(1, NamedSelectionEntityType::Face, 4)})
              .changedItemCount == 1,
          "add new members without duplicates");
    check(manager.removeItems(
              created.namedSelectionId,
              {item(1, NamedSelectionEntityType::Face, 3)})
              .success,
          "remove matching member");
    check(manager.replaceItems(
              created.namedSelectionId,
              {item(2, NamedSelectionEntityType::Face, 2)})
              .success &&
              manager.find(created.namedSelectionId)->items.size() == 1,
          "replace members");
    check(manager.removeItems(
              created.namedSelectionId,
              {item(2, NamedSelectionEntityType::Face, 2)})
              .error == NamedSelectionError::WouldBecomeEmpty,
          "prevent empty selection after removal");
    check(manager.rename(created.namedSelectionId, "Load Faces").success &&
              manager.find(created.namedSelectionId)->name == "Load Faces",
          "rename without changing ID or members");

    geometryObjects.erase(2);
    const auto references = manager.selectionsReferencingGeometry(2);
    check(references.size() == 1 &&
              references.front() == created.namedSelectionId,
          "retain reference after geometry deletion for invalid reporting");
    check(manager.invalidGeometryReferenceCount(created.namedSelectionId) == 1,
          "detect invalid reference after geometry deletion");
    check(manager.remove(created.namedSelectionId).success &&
              manager.find(created.namedSelectionId) == nullptr,
          "remove named selection without geometry mutation");

    if (failures == 0) {
        std::cout << "Named selection tests passed: selections="
                  << manager.namedSelections().size() << '\n';
    }
    return failures == 0 ? 0 : 1;
}
