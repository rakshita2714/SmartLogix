#include "smartlogix_service.h"

#include <mutex>

SmartLogixService::SmartLogixService(int initialHashCapacity)
    : warehouses_(initialHashCapacity),
      routes_(warehouses_, initialHashCapacity),
      fulfillment_(initialHashCapacity) {}

bool SmartLogixService::addWarehouse(const Warehouse& warehouse) {
    std::unique_lock<std::mutex> writerLock(networkWriterMutex_);
    std::unique_lock<std::shared_mutex> lock(networkMutex_);
    if (!warehouses_.addWarehouse(warehouse)) {
        return false;
    }
    if (!routes_.registerWarehouseVertex(warehouse.id)) {
        warehouses_.deleteWarehouse(warehouse.id);
        return false;
    }
    return true;
}

std::optional<Warehouse> SmartLogixService::searchWarehouse(const std::string& id) const {
    std::shared_lock<std::shared_mutex> lock(networkMutex_);
    const Warehouse* warehouse = warehouses_.searchWarehouse(id);
    if (warehouse == nullptr) {
        return std::nullopt;
    }
    return *warehouse;
}

bool SmartLogixService::updateWarehouse(const std::string& id,
                                        const std::string& name,
                                        const std::string& location,
                                        int capacity) {
    std::unique_lock<std::mutex> writerLock(networkWriterMutex_);
    std::unique_lock<std::shared_mutex> lock(networkMutex_);
    return warehouses_.updateWarehouse(id, name, location, capacity);
}

bool SmartLogixService::deleteWarehouse(const std::string& id) {
    std::unique_lock<std::mutex> writerLock(networkWriterMutex_);
    std::unique_lock<std::shared_mutex> lock(networkMutex_);
    if (warehouses_.searchWarehouse(id) == nullptr || !routes_.removeWarehouseVertex(id)) {
        return false;
    }
    if (!warehouses_.deleteWarehouse(id)) {
        // Preserve the warehouse/vertex pairing if manager removal fails.
        routes_.registerWarehouseVertex(id);
        return false;
    }
    return true;
}

std::vector<Warehouse> SmartLogixService::getAllWarehouses() const {
    std::shared_lock<std::shared_mutex> lock(networkMutex_);
    return warehouses_.getAllWarehouses();
}

bool SmartLogixService::addRoute(const std::string& fromWarehouseId,
                                 const std::string& toWarehouseId,
                                 double distanceKm) {
    std::unique_lock<std::mutex> writerLock(networkWriterMutex_);
    std::unique_lock<std::shared_mutex> lock(networkMutex_);
    return routes_.addRoute(fromWarehouseId, toWarehouseId, distanceKm);
}

bool SmartLogixService::removeRoute(const std::string& fromWarehouseId,
                                    const std::string& toWarehouseId) {
    std::unique_lock<std::mutex> writerLock(networkWriterMutex_);
    std::unique_lock<std::shared_mutex> lock(networkMutex_);
    return routes_.removeRoute(fromWarehouseId, toWarehouseId);
}

std::optional<std::vector<Route>> SmartLogixService::getOutgoingRoutes(
    const std::string& warehouseId) const {
    std::shared_lock<std::shared_mutex> lock(networkMutex_);
    std::vector<Route> outgoing;
    if (!routes_.getOutgoingRoutes(warehouseId, outgoing)) {
        return std::nullopt;
    }
    return outgoing;
}

std::vector<Route> SmartLogixService::getAllRoutes() const {
    std::shared_lock<std::shared_mutex> lock(networkMutex_);
    return routes_.getAllRoutes();
}

std::size_t SmartLogixService::vertexCount() const {
    std::shared_lock<std::shared_mutex> lock(networkMutex_);
    return routes_.vertexCount();
}

std::size_t SmartLogixService::routeCount() const {
    std::shared_lock<std::shared_mutex> lock(networkMutex_);
    return routes_.routeCount();
}

ShortestPathResult SmartLogixService::shortestPath(
    const std::string& sourceWarehouseId,
    const std::string& destinationWarehouseId) const {
    std::shared_lock<std::shared_mutex> lock(networkMutex_);
    return GraphAlgorithms::dijkstra(routes_, sourceWarehouseId, destinationWarehouseId);
}

TraversalResult SmartLogixService::breadthFirstSearch(
    const std::string& startWarehouseId) const {
    std::shared_lock<std::shared_mutex> lock(networkMutex_);
    return GraphAlgorithms::breadthFirstSearch(routes_, startWarehouseId);
}

TraversalResult SmartLogixService::depthFirstSearch(
    const std::string& startWarehouseId) const {
    std::shared_lock<std::shared_mutex> lock(networkMutex_);
    return GraphAlgorithms::depthFirstSearch(routes_, startWarehouseId);
}

bool SmartLogixService::addOperation(const std::string& operationId) {
    std::unique_lock<std::mutex> writerLock(fulfillmentWriterMutex_);
    std::unique_lock<std::shared_mutex> lock(fulfillmentMutex_);
    return fulfillment_.addOperation(operationId);
}

bool SmartLogixService::addDependency(const std::string& prerequisiteId,
                                      const std::string& dependentId) {
    std::unique_lock<std::mutex> writerLock(fulfillmentWriterMutex_);
    std::unique_lock<std::shared_mutex> lock(fulfillmentMutex_);
    return fulfillment_.addDependency(prerequisiteId, dependentId);
}

bool SmartLogixService::hasFulfillmentCycle() const {
    std::shared_lock<std::shared_mutex> lock(fulfillmentMutex_);
    return fulfillment_.hasCycle();
}

TopologicalSortResult SmartLogixService::topologicalSort() const {
    std::shared_lock<std::shared_mutex> lock(fulfillmentMutex_);
    return fulfillment_.topologicalSort();
}

std::vector<std::string> SmartLogixService::getOperationIds() const {
    std::shared_lock<std::shared_mutex> lock(fulfillmentMutex_);
    return fulfillment_.getOperationIds();
}

std::vector<FulfillmentDependency> SmartLogixService::getDependencies() const {
    std::shared_lock<std::shared_mutex> lock(fulfillmentMutex_);
    return fulfillment_.getDependencies();
}

FulfillmentSnapshot SmartLogixService::getFulfillmentSnapshot() const {
    std::shared_lock<std::shared_mutex> lock(fulfillmentMutex_);
    return {fulfillment_.getOperationIds(), fulfillment_.getDependencies()};
}

ImportReport SmartLogixService::importWarehouses(const std::string& path) {
    const PreparedCsvImport loaded = CsvManager::prepareWarehouses(path);
    std::unique_lock<std::mutex> writerLock(networkWriterMutex_);
    std::unique_lock<std::shared_mutex> lock(networkMutex_);
    return CsvManager::applyWarehouses(loaded, warehouses_, routes_);
}

ImportReport SmartLogixService::importRoutes(const std::string& path) {
    const PreparedCsvImport loaded = CsvManager::prepareRoutes(path);
    std::unique_lock<std::mutex> writerLock(networkWriterMutex_);
    std::unique_lock<std::shared_mutex> lock(networkMutex_);
    return CsvManager::applyRoutes(loaded, warehouses_, routes_);
}

ImportReport SmartLogixService::importOperations(const std::string& path) {
    const PreparedCsvImport loaded = CsvManager::prepareOperations(path);
    std::unique_lock<std::mutex> writerLock(fulfillmentWriterMutex_);
    std::unique_lock<std::shared_mutex> lock(fulfillmentMutex_);
    return CsvManager::applyOperations(loaded, fulfillment_);
}

ImportReport SmartLogixService::importDependencies(const std::string& path) {
    const PreparedCsvImport loaded = CsvManager::prepareDependencies(path);
    std::unique_lock<std::mutex> writerLock(fulfillmentWriterMutex_);
    std::unique_lock<std::shared_mutex> lock(fulfillmentMutex_);
    return CsvManager::applyDependencies(loaded, fulfillment_);
}

ExportReport SmartLogixService::exportWarehouses(const std::string& path,
                                                 bool allowOverwrite) const {
    std::vector<Warehouse> records;
    {
        std::shared_lock<std::shared_mutex> lock(networkMutex_);
        records = warehouses_.getAllWarehouses();
    }
    return CsvManager::exportWarehouses(path, records, allowOverwrite);
}

ExportReport SmartLogixService::exportRoutes(const std::string& path,
                                             bool allowOverwrite) const {
    std::vector<Route> records;
    {
        std::shared_lock<std::shared_mutex> lock(networkMutex_);
        records = routes_.getAllRoutes();
    }
    return CsvManager::exportRoutes(path, records, allowOverwrite);
}

ExportReport SmartLogixService::exportOperations(const std::string& path,
                                                 bool allowOverwrite) const {
    std::vector<std::string> records;
    {
        std::shared_lock<std::shared_mutex> lock(fulfillmentMutex_);
        records = fulfillment_.getOperationIds();
    }
    return CsvManager::exportOperations(path, records, allowOverwrite);
}

ExportReport SmartLogixService::exportDependencies(const std::string& path,
                                                  bool allowOverwrite) const {
    std::vector<FulfillmentDependency> records;
    {
        std::shared_lock<std::shared_mutex> lock(fulfillmentMutex_);
        records = fulfillment_.getDependencies();
    }
    return CsvManager::exportDependencies(path, records, allowOverwrite);
}
