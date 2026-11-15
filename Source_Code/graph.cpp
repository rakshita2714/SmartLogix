#include "graph.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <ostream>

TransportationGraph::TransportationGraph(const WarehouseManager& manager,
                                         int initialHashCapacity)
    : warehouseManager(manager), vertices(), idToVertexIndex(initialHashCapacity) {}

bool TransportationGraph::findVertexIndex(const std::string& warehouseId,
                                          std::size_t& index) const {
    if (warehouseId.empty()) {
        return false;
    }

    const int storedIndex = idToVertexIndex.search(warehouseId);
    if (storedIndex < 0 || static_cast<std::size_t>(storedIndex) >= vertices.size()) {
        return false;
    }

    index = static_cast<std::size_t>(storedIndex);
    return vertices[index].warehouseId == warehouseId;
}

bool TransportationGraph::registerWarehouseVertex(const std::string& warehouseId) {
    if (warehouseManager.searchWarehouse(warehouseId) == nullptr ||
        idToVertexIndex.search(warehouseId) != -1 ||
        vertices.size() >= static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return false;
    }

    const int index = static_cast<int>(vertices.size());
    vertices.push_back({warehouseId, {}});
    idToVertexIndex.insert(warehouseId, index);
    return true;
}

bool TransportationGraph::addRoute(const std::string& fromWarehouseId,
                                   const std::string& toWarehouseId,
                                   double distanceKm) {
    if (!std::isfinite(distanceKm) || distanceKm < 0.0 ||
        warehouseManager.searchWarehouse(fromWarehouseId) == nullptr ||
        warehouseManager.searchWarehouse(toWarehouseId) == nullptr) {
        return false;
    }

    std::size_t sourceIndex = 0;
    if (!findVertexIndex(fromWarehouseId, sourceIndex)) {
        return false;
    }
    std::size_t destinationIndex = 0;
    if (!findVertexIndex(toWarehouseId, destinationIndex)) {
        return false;
    }
    if (vertices[destinationIndex].warehouseId != toWarehouseId) {
        return false;
    }

    // One directed route per ordered pair keeps its distance unambiguous.
    std::vector<Edge>& outgoing = vertices[sourceIndex].outgoingRoutes;
    for (const Edge& edge : outgoing) {
        if (edge.destinationWarehouseId == toWarehouseId) {
            return false;
        }
    }

    outgoing.push_back({toWarehouseId, distanceKm});
    return true;
}

bool TransportationGraph::removeRoute(const std::string& fromWarehouseId,
                                      const std::string& toWarehouseId) {
    std::size_t sourceIndex = 0;
    if (!findVertexIndex(fromWarehouseId, sourceIndex)) {
        return false;
    }

    std::vector<Edge>& outgoing = vertices[sourceIndex].outgoingRoutes;
    for (auto it = outgoing.begin(); it != outgoing.end(); ++it) {
        if (it->destinationWarehouseId == toWarehouseId) {
            outgoing.erase(it);
            return true;
        }
    }
    return false;
}

bool TransportationGraph::removeWarehouseVertex(const std::string& warehouseId) {
    std::size_t removedIndex = 0;
    if (!findVertexIndex(warehouseId, removedIndex)) {
        return false;
    }

    // Remove every incoming edge. The erased vertex carries away its outgoing edges.
    for (Vertex& vertex : vertices) {
        auto& outgoing = vertex.outgoingRoutes;
        outgoing.erase(
            std::remove_if(outgoing.begin(), outgoing.end(),
                           [&warehouseId](const Edge& edge) {
                               return edge.destinationWarehouseId == warehouseId;
                           }),
            outgoing.end());
    }

    idToVertexIndex.remove(warehouseId);
    vertices.erase(vertices.begin() + static_cast<std::ptrdiff_t>(removedIndex));

    // Graph vector positions may shift; edge references remain stable warehouse IDs.
    for (std::size_t i = removedIndex; i < vertices.size(); ++i) {
        idToVertexIndex.insert(vertices[i].warehouseId, static_cast<int>(i));
    }
    return true;
}

void TransportationGraph::displayRoutes(std::ostream& output) const {
    bool foundRoute = false;
    for (const Vertex& vertex : vertices) {
        for (const Edge& edge : vertex.outgoingRoutes) {
            output << vertex.warehouseId << " -> " << edge.destinationWarehouseId
                   << " : " << edge.distanceKm << " km\n";
            foundRoute = true;
        }
    }

    if (!foundRoute) {
        output << "No routes found.\n";
    }
}

bool TransportationGraph::getOutgoingRoutes(const std::string& warehouseId,
                                            std::vector<Route>& routes) const {
    routes.clear();
    std::size_t sourceIndex = 0;
    if (!findVertexIndex(warehouseId, sourceIndex) ||
        warehouseManager.searchWarehouse(warehouseId) == nullptr) {
        return false;
    }

    const Vertex& source = vertices[sourceIndex];
    routes.reserve(source.outgoingRoutes.size());
    for (const Edge& edge : source.outgoingRoutes) {
        routes.push_back({source.warehouseId, edge.destinationWarehouseId, edge.distanceKm});
    }
    return true;
}

std::vector<Route> TransportationGraph::getAllRoutes() const {
    std::vector<Route> routes;
    routes.reserve(routeCount());
    for (const Vertex& vertex : vertices) {
        for (const Edge& edge : vertex.outgoingRoutes) {
            routes.push_back({vertex.warehouseId, edge.destinationWarehouseId, edge.distanceKm});
        }
    }
    return routes;
}

std::size_t TransportationGraph::vertexCount() const noexcept {
    return vertices.size();
}

std::size_t TransportationGraph::routeCount() const noexcept {
    std::size_t count = 0;
    for (const Vertex& vertex : vertices) {
        count += vertex.outgoingRoutes.size();
    }
    return count;
}
