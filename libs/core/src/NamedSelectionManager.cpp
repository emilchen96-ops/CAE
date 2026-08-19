#include "emilcae/core/NamedSelectionManager.hpp"

#include <algorithm>
#include <string_view>

namespace emilcae::core {

namespace {

std::string_view trimAscii(std::string_view text) {
    constexpr std::string_view whitespace = " \t\r\n\f\v";
    const std::size_t first = text.find_first_not_of(whitespace);
    if (first == std::string_view::npos) {
        return {};
    }
    const std::size_t last = text.find_last_not_of(whitespace);
    return text.substr(first, last - first + 1);
}

bool validLocalIndex(const NamedSelectionItem& item) {
    return item.entityType == NamedSelectionEntityType::Object
        ? item.localIndex == 0
        : item.localIndex > 0;
}

} // namespace

NamedSelectionManager::NamedSelectionManager(
    GeometryObjectExists geometryObjectExists,
    ReferenceChecker referenceChecker)
    : geometryObjectExists_(std::move(geometryObjectExists)),
      referenceChecker_(std::move(referenceChecker)) {}

NamedSelectionOperationResult NamedSelectionManager::create(
    const std::string& name, NamedSelectionEntityType entityType,
    const std::vector<NamedSelectionItem>& items) {
    if (const NamedSelectionError error = validateName(name);
        error != NamedSelectionError::None) {
        return {.error = error};
    }
    std::vector<NamedSelectionItem> normalized;
    if (const NamedSelectionError error =
            normalizeItems(entityType, items, normalized);
        error != NamedSelectionError::None) {
        return {.error = error};
    }
    const NamedSelectionId id = nextId_++;
    namedSelections_.push_back(
        {.id = id,
         .name = std::string(trimAscii(name)),
         .entityType = entityType,
         .items = std::move(normalized)});
    return {.success = true,
            .namedSelectionId = id,
            .changedItemCount = namedSelections_.back().items.size()};
}

NamedSelectionOperationResult NamedSelectionManager::update(
    NamedSelectionId id, const std::string& name,
    NamedSelectionEntityType entityType,
    const std::vector<NamedSelectionItem>& items) {
    NamedSelection* selection = findMutable(id);
    if (selection == nullptr) {
        return {.error = NamedSelectionError::NotFound};
    }
    if (const NamedSelectionError error = validateName(name, id);
        error != NamedSelectionError::None) {
        return {.error = error};
    }
    std::vector<NamedSelectionItem> normalized;
    if (const NamedSelectionError error =
            normalizeItems(entityType, items, normalized);
        error != NamedSelectionError::None) {
        return {.error = error};
    }
    selection->name = std::string(trimAscii(name));
    selection->entityType = entityType;
    selection->items = std::move(normalized);
    return {.success = true,
            .namedSelectionId = id,
            .changedItemCount = selection->items.size()};
}

NamedSelectionOperationResult NamedSelectionManager::rename(
    NamedSelectionId id, const std::string& name) {
    NamedSelection* selection = findMutable(id);
    if (selection == nullptr) {
        return {.error = NamedSelectionError::NotFound};
    }
    if (const NamedSelectionError error = validateName(name, id);
        error != NamedSelectionError::None) {
        return {.error = error};
    }
    selection->name = std::string(trimAscii(name));
    return {.success = true, .namedSelectionId = id};
}

NamedSelectionOperationResult NamedSelectionManager::remove(
    NamedSelectionId id) {
    const auto iterator = std::find_if(
        namedSelections_.begin(), namedSelections_.end(),
        [id](const NamedSelection& selection) { return selection.id == id; });
    if (iterator == namedSelections_.end()) {
        return {.error = NamedSelectionError::NotFound};
    }
    if (referenceChecker_ && referenceChecker_(id)) {
        return {.error = NamedSelectionError::InUse};
    }
    namedSelections_.erase(iterator);
    return {.success = true, .namedSelectionId = id};
}

NamedSelectionOperationResult NamedSelectionManager::addItems(
    NamedSelectionId id, const std::vector<NamedSelectionItem>& items) {
    NamedSelection* selection = findMutable(id);
    if (selection == nullptr) {
        return {.error = NamedSelectionError::NotFound};
    }
    std::vector<NamedSelectionItem> normalized;
    if (const NamedSelectionError error =
            normalizeItems(selection->entityType, items, normalized);
        error != NamedSelectionError::None) {
        return {.error = error};
    }
    std::size_t added = 0;
    for (const NamedSelectionItem& item : normalized) {
        if (std::find(selection->items.begin(), selection->items.end(), item) ==
            selection->items.end()) {
            selection->items.push_back(item);
            ++added;
        }
    }
    return {.success = true,
            .namedSelectionId = id,
            .changedItemCount = added};
}

NamedSelectionOperationResult NamedSelectionManager::removeItems(
    NamedSelectionId id, const std::vector<NamedSelectionItem>& items) {
    NamedSelection* selection = findMutable(id);
    if (selection == nullptr) {
        return {.error = NamedSelectionError::NotFound};
    }
    std::vector<NamedSelectionItem> normalized;
    if (const NamedSelectionError error =
            normalizeItems(selection->entityType, items, normalized);
        error != NamedSelectionError::None) {
        return {.error = error};
    }
    std::vector<NamedSelectionItem> remaining = selection->items;
    std::size_t removed = 0;
    for (const NamedSelectionItem& item : normalized) {
        const auto iterator = std::find(remaining.begin(), remaining.end(), item);
        if (iterator != remaining.end()) {
            remaining.erase(iterator);
            ++removed;
        }
    }
    if (remaining.empty()) {
        return {.error = NamedSelectionError::WouldBecomeEmpty};
    }
    selection->items = std::move(remaining);
    return {.success = true,
            .namedSelectionId = id,
            .changedItemCount = removed};
}

NamedSelectionOperationResult NamedSelectionManager::replaceItems(
    NamedSelectionId id, const std::vector<NamedSelectionItem>& items) {
    NamedSelection* selection = findMutable(id);
    if (selection == nullptr) {
        return {.error = NamedSelectionError::NotFound};
    }
    std::vector<NamedSelectionItem> normalized;
    if (const NamedSelectionError error =
            normalizeItems(selection->entityType, items, normalized);
        error != NamedSelectionError::None) {
        return {.error = error};
    }
    selection->items = std::move(normalized);
    return {.success = true,
            .namedSelectionId = id,
            .changedItemCount = selection->items.size()};
}

const NamedSelection* NamedSelectionManager::find(
    NamedSelectionId id) const {
    const auto iterator = std::find_if(
        namedSelections_.cbegin(), namedSelections_.cend(),
        [id](const NamedSelection& selection) { return selection.id == id; });
    return iterator != namedSelections_.cend() ? &*iterator : nullptr;
}

std::vector<NamedSelection> NamedSelectionManager::namedSelections() const {
    return namedSelections_;
}

std::vector<NamedSelectionId>
NamedSelectionManager::selectionsReferencingGeometry(
    int geometryObjectId) const {
    std::vector<NamedSelectionId> result;
    for (const NamedSelection& selection : namedSelections_) {
        if (std::any_of(selection.items.cbegin(), selection.items.cend(),
                        [geometryObjectId](const NamedSelectionItem& item) {
                            return item.geometryObjectId == geometryObjectId;
                        })) {
            result.push_back(selection.id);
        }
    }
    return result;
}

std::size_t NamedSelectionManager::invalidGeometryReferenceCount(
    NamedSelectionId id) const {
    const NamedSelection* selection = find(id);
    if (selection == nullptr || !geometryObjectExists_) {
        return 0;
    }
    return static_cast<std::size_t>(std::count_if(
        selection->items.cbegin(), selection->items.cend(),
        [this](const NamedSelectionItem& item) {
            return !geometryObjectExists_(item.geometryObjectId);
        }));
}

NamedSelectionError NamedSelectionManager::validateName(
    const std::string& name, NamedSelectionId excludedId) const {
    const std::string_view normalized = trimAscii(name);
    if (normalized.empty()) {
        return NamedSelectionError::EmptyName;
    }
    for (const NamedSelection& selection : namedSelections_) {
        if (selection.id != excludedId && selection.name == normalized) {
            return NamedSelectionError::DuplicateName;
        }
    }
    return NamedSelectionError::None;
}

NamedSelectionError NamedSelectionManager::normalizeItems(
    NamedSelectionEntityType entityType,
    const std::vector<NamedSelectionItem>& items,
    std::vector<NamedSelectionItem>& normalized) const {
    if (items.empty()) {
        return NamedSelectionError::EmptyItems;
    }
    normalized.clear();
    normalized.reserve(items.size());
    for (const NamedSelectionItem& item : items) {
        if (item.entityType != entityType) {
            return NamedSelectionError::MixedEntityTypes;
        }
        if (!geometryObjectExists_ || item.geometryObjectId < 0 ||
            !geometryObjectExists_(item.geometryObjectId)) {
            return NamedSelectionError::InvalidGeometryObject;
        }
        if (!validLocalIndex(item)) {
            return NamedSelectionError::InvalidLocalIndex;
        }
        if (std::find(normalized.begin(), normalized.end(), item) ==
            normalized.end()) {
            normalized.push_back(item);
        }
    }
    return NamedSelectionError::None;
}

NamedSelection* NamedSelectionManager::findMutable(NamedSelectionId id) {
    const auto iterator = std::find_if(
        namedSelections_.begin(), namedSelections_.end(),
        [id](const NamedSelection& selection) { return selection.id == id; });
    return iterator != namedSelections_.end() ? &*iterator : nullptr;
}

} // namespace emilcae::core
