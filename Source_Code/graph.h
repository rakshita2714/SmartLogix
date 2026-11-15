#ifndef GRAPH_H
#define GRAPH_H

#include "hash_table.h"
#include "warehouse_manager.h"

#include <cstddef>
#include <iosfwd>
#include <string>
#include <vector>

struct Route {
    std::string fromWarehouseId;
    std::string toWarehouseId;
    double distanceKm;
};

class GraphAlgorithms;

// Directed weighted adjacency-list graph for the warehouses in a manager.
class TransportationGraph {
private:
    friend class GraphAlgorithms;

    struct Edge {
        std::string destinationWarehouseId;
        double distanceKm;
    };

    struct Vertex {
        std::string warehouseId;
        std::vector<Edge> outgoingRoutes;
    };

    const WarehouseManager& warehouseManager;
    std::vector<Vertex> vertices;
    HashTable idToVertexIndex;

    bool findVertexIndex(const std::string& warehouseId, std::size_t& index) const;

public:
    explicit TransportationGraph(const WarehouseManager& manager,
                                 int initialHashCapacity = 10);

    bool registerWarehouseVertex(const std::string& warehouseId);
    bool addRoute(const std::string& fromWarehouseId,
                  const std::string& toWarehouseId,
                  double distanceKm);
    bool removeRoute(const std::string& fromWarehouseId,
                     const std::string& toWarehouseId);
    bool removeWarehouseVertex(const std::string& warehouseId);
    void displayRoutes(std::ostream& output) const;
    bool getOutgoingRoutes(const std::string& warehouseId,
                           std::vector<Route>& routes) const;
    std::vector<Route> getAllRoutes() const;

    std::size_t vertexCount() const noexcept;
    std::size_t routeCount() const noexcept;
};

#endif
