#ifndef ALGORITHMS_H
#define ALGORITHMS_H

#include "graph.h"

#include <string>
#include <vector>

enum class ShortestPathStatus {
    Found,
    InvalidSource,
    InvalidDestination,
    Unreachable
};

struct ShortestPathResult {
    ShortestPathStatus status;
    std::vector<std::string> warehouseIds;
    double totalDistanceKm;
};

struct TraversalResult {
    bool validStart;
    std::vector<std::string> warehouseIds;
};

// Read-only graph algorithms implemented over TransportationGraph's adjacency list.
class GraphAlgorithms {
public:
    static ShortestPathResult dijkstra(const TransportationGraph& graph,
                                       const std::string& sourceWarehouseId,
                                       const std::string& destinationWarehouseId);

    static TraversalResult breadthFirstSearch(const TransportationGraph& graph,
                                              const std::string& startWarehouseId);

    static TraversalResult depthFirstSearch(const TransportationGraph& graph,
                                            const std::string& startWarehouseId);
};

#endif
