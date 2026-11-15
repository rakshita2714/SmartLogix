#include "warehouse_manager.h"

#include <cctype>
#include <limits>
#include <ostream>

namespace {
bool containsNonWhitespace(const std::string& text) {
    for (unsigned char ch : text) {
        if (!std::isspace(ch)) {
            return true;
        }
    }
    return false;
}
}  // namespace

WarehouseManager::WarehouseManager(int initialHashCapacity)
    : warehouses(), idToIndex(initialHashCapacity) {}

bool WarehouseManager::isValid(const Warehouse& warehouse) {
    return containsNonWhitespace(warehouse.id) && containsNonWhitespace(warehouse.name) &&
           containsNonWhitespace(warehouse.location) && warehouse.capacity >= 0;
}

bool WarehouseManager::addWarehouse(const Warehouse& warehouse) {
    if (!isValid(warehouse) || searchWarehouse(warehouse.id) != nullptr ||
        warehouses.size() >= static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return false;
    }

    const int index = static_cast<int>(warehouses.size());
    warehouses.push_back(warehouse);
    idToIndex.insert(warehouse.id, index);
    return true;
}

const Warehouse* WarehouseManager::searchWarehouse(const std::string& id) const {
    if (id.empty()) {
        return nullptr;
    }

    const int index = idToIndex.search(id);
    if (index < 0 || static_cast<std::size_t>(index) >= warehouses.size()) {
        return nullptr;
    }

    // Check the index and ID agree so stale or invalid mappings fail safely.
    const Warehouse& warehouse = warehouses[static_cast<std::size_t>(index)];
    return warehouse.id == id ? &warehouse : nullptr;
}

bool WarehouseManager::updateWarehouse(const std::string& id,
                                       const std::string& name,
                                       const std::string& location,
                                       int capacity) {
    Warehouse* warehouse = nullptr;
    const int index = id.empty() ? -1 : idToIndex.search(id);
    if (index >= 0 && static_cast<std::size_t>(index) < warehouses.size()) {
        Warehouse& candidate = warehouses[static_cast<std::size_t>(index)];
        if (candidate.id == id) {
            warehouse = &candidate;
        }
    }

    if (warehouse == nullptr || !containsNonWhitespace(name) ||
        !containsNonWhitespace(location) || capacity < 0) {
        return false;
    }

    warehouse->name = name;
    warehouse->location = location;
    warehouse->capacity = capacity;
    return true;
}

bool WarehouseManager::deleteWarehouse(const std::string& id) {
    if (id.empty()) {
        return false;
    }

    const int index = idToIndex.search(id);
    if (index < 0 || static_cast<std::size_t>(index) >= warehouses.size() ||
        warehouses[static_cast<std::size_t>(index)].id != id) {
        return false;
    }

    const std::size_t removedIndex = static_cast<std::size_t>(index);
    idToIndex.remove(id);
    warehouses.erase(warehouses.begin() + index);

    // Erasing shifts later vector entries, so refresh their stored indices.
    for (std::size_t i = removedIndex; i < warehouses.size(); ++i) {
        idToIndex.insert(warehouses[i].id, static_cast<int>(i));
    }
    return true;
}

void WarehouseManager::displayWarehouses(std::ostream& output) const {
    if (warehouses.empty()) {
        output << "No warehouses found.\n";
        return;
    }

    output << "Warehouses:\n";
    for (const Warehouse& warehouse : warehouses) {
        output << "ID: " << warehouse.id
               << " | Name: " << warehouse.name
               << " | Location: " << warehouse.location
               << " | Capacity: " << warehouse.capacity << '\n';
    }
}

std::vector<Warehouse> WarehouseManager::getAllWarehouses() const {
    return warehouses;
}

std::size_t WarehouseManager::size() const noexcept {
    return warehouses.size();
}
