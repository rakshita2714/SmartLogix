#ifndef WAREHOUSE_MANAGER_H
#define WAREHOUSE_MANAGER_H

#include "hash_table.h"
#include "warehouse.h"

#include <cstddef>
#include <iosfwd>
#include <string>
#include <vector>

// Coordinates warehouse records with the custom ID-to-vector-index hash table.
class WarehouseManager {
private:
    std::vector<Warehouse> warehouses;
    HashTable idToIndex;

    static bool isValid(const Warehouse& warehouse);

public:
    explicit WarehouseManager(int initialHashCapacity = 10);

    bool addWarehouse(const Warehouse& warehouse);
    // The pointer refers to an internal vector entry and may be invalidated by later mutations.
    const Warehouse* searchWarehouse(const std::string& id) const;
    bool updateWarehouse(const std::string& id,
                         const std::string& name,
                         const std::string& location,
                         int capacity);
    bool deleteWarehouse(const std::string& id);
    void displayWarehouses(std::ostream& output) const;

    std::vector<Warehouse> getAllWarehouses() const;
    std::size_t size() const noexcept;
};

#endif
