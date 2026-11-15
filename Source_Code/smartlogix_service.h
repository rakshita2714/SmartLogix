#ifndef SMARTLOGIX_SERVICE_H
#define SMARTLOGIX_SERVICE_H

#include "algorithms.h"
#include "csv_manager.h"
#include "fulfillment.h"
#include "graph.h"
#include "warehouse_manager.h"

#include <cstddef>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <vector>

struct FulfillmentSnapshot {
    std::vector<std::string> operationIds;
    std::vector<FulfillmentDependency> dependencies;
};

// Owns the application managers and exposes value-based operations to callers.
// Stage 2 keeps the existing single-threaded behavior; synchronization is added
// in the following stage.
class SmartLogixService {
public:
    explicit SmartLogixService(int initialHashCapacity = 10);

    bool addWarehouse(const Warehouse& warehouse);
    std::optional<Warehouse> searchWarehouse(const std::string& id) const;
    bool updateWarehouse(const std::string& id,
                         const std::string& name,
                         const std::string& location,
                         int capacity);
    bool deleteWarehouse(const std::string& id);
    std::vector<Warehouse> getAllWarehouses() const;

    bool addRoute(const std::string& fromWarehouseId,
                  const std::string& toWarehouseId,
                  double distanceKm);
    bool removeRoute(const std::string& fromWarehouseId,
                     const std::string& toWarehouseId);
    std::optional<std::vector<Route>> getOutgoingRoutes(
        const std::string& warehouseId) const;
    std::vector<Route> getAllRoutes() const;
    std::size_t vertexCount() const;
    std::size_t routeCount() const;

    ShortestPathResult shortestPath(const std::string& sourceWarehouseId,
                                   const std::string& destinationWarehouseId) const;
    TraversalResult breadthFirstSearch(const std::string& startWarehouseId) const;
    TraversalResult depthFirstSearch(const std::string& startWarehouseId) const;

    bool addOperation(const std::string& operationId);
    bool addDependency(const std::string& prerequisiteId,
                       const std::string& dependentId);
    bool hasFulfillmentCycle() const;
    TopologicalSortResult topologicalSort() const;
    std::vector<std::string> getOperationIds() const;
    std::vector<FulfillmentDependency> getDependencies() const;
    FulfillmentSnapshot getFulfillmentSnapshot() const;

    ImportReport importWarehouses(const std::string& path);
    ImportReport importRoutes(const std::string& path);
    ImportReport importOperations(const std::string& path);
    ImportReport importDependencies(const std::string& path);

    ExportReport exportWarehouses(const std::string& path,
                                  bool allowOverwrite = false) const;
    ExportReport exportRoutes(const std::string& path,
                              bool allowOverwrite = false) const;
    ExportReport exportOperations(const std::string& path,
                                  bool allowOverwrite = false) const;
    ExportReport exportDependencies(const std::string& path,
                                    bool allowOverwrite = false) const;

private:
    mutable std::shared_mutex networkMutex_;
    mutable std::shared_mutex fulfillmentMutex_;
    // Keep concurrent writers from simultaneously entering the UCRT64
    // pthread-backed shared_mutex implementation; readers remain concurrent.
    mutable std::mutex networkWriterMutex_;
    mutable std::mutex fulfillmentWriterMutex_;

    // TransportationGraph stores a reference to warehouses_, so it must be
    // declared after warehouses_ and constructed after it.
    WarehouseManager warehouses_;
    TransportationGraph routes_;
    FulfillmentGraph fulfillment_;
};

#endif
