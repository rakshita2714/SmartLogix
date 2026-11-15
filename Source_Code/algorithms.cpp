#include "algorithms.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <queue>
#include <utility>

ShortestPathResult GraphAlgorithms::dijkstra(
    const TransportationGraph& graph,
    const std::string& sourceWarehouseId,
    const std::string& destinationWarehouseId) {
    std::size_t sourceIndex = 0;
    if (graph.warehouseManager.searchWarehouse(sourceWarehouseId) == nullptr ||
        !graph.findVertexIndex(sourceWarehouseId, sourceIndex)) {
        return {ShortestPathStatus::InvalidSource, {}, 0.0};
    }

    std::size_t destinationIndex = 0;
    if (graph.warehouseManager.searchWarehouse(destinationWarehouseId) == nullptr ||
        !graph.findVertexIndex(destinationWarehouseId, destinationIndex)) {
        return {ShortestPathStatus::InvalidDestination, {}, 0.0};
    }

    if (sourceIndex == destinationIndex) {
        return {ShortestPathStatus::Found, {sourceWarehouseId}, 0.0};
    }

    const std::size_t vertexCount = graph.vertices.size();
    const double infinity = std::numeric_limits<double>::infinity();
    std::vector<double> distances(vertexCount, infinity);
    std::vector<std::size_t> predecessor(vertexCount, vertexCount);

    using QueueEntry = std::pair<double, std::size_t>;
    std::priority_queue<QueueEntry,
                        std::vector<QueueEntry>,
                        std::greater<QueueEntry>>
        minQueue;

    distances[sourceIndex] = 0.0;
    minQueue.push({0.0, sourceIndex});

    while (!minQueue.empty()) {
        const double currentDistance = minQueue.top().first;
        const std::size_t currentIndex = minQueue.top().second;
        minQueue.pop();

        // A shorter route may have been found after this queue entry was added.
        if (currentDistance != distances[currentIndex]) {
            continue;
        }
        if (currentIndex == destinationIndex) {
            break;
        }

        const TransportationGraph::Vertex& currentVertex = graph.vertices[currentIndex];
        for (const TransportationGraph::Edge& edge : currentVertex.outgoingRoutes) {
            std::size_t neighborIndex = 0;
            if (!graph.findVertexIndex(edge.destinationWarehouseId, neighborIndex) ||
                graph.warehouseManager.searchWarehouse(edge.destinationWarehouseId) == nullptr ||
                !std::isfinite(edge.distanceKm) || edge.distanceKm < 0.0) {
                continue;
            }

            const double candidateDistance = currentDistance + edge.distanceKm;
            // Ignore sums that overflow to infinity instead of corrupting comparisons.
            if (!std::isfinite(candidateDistance)) {
                continue;
            }

            if (candidateDistance < distances[neighborIndex]) {
                distances[neighborIndex] = candidateDistance;
                predecessor[neighborIndex] = currentIndex;
                minQueue.push({candidateDistance, neighborIndex});
            }
        }
    }

    if (!std::isfinite(distances[destinationIndex])) {
        return {ShortestPathStatus::Unreachable, {}, 0.0};
    }

    std::vector<std::string> path;
    for (std::size_t index = destinationIndex; index != vertexCount;
         index = predecessor[index]) {
        path.push_back(graph.vertices[index].warehouseId);
        if (index == sourceIndex) {
            break;
        }
    }

    if (path.empty() || path.back() != sourceWarehouseId) {
        return {ShortestPathStatus::Unreachable, {}, 0.0};
    }

    std::reverse(path.begin(), path.end());
    return {ShortestPathStatus::Found, path, distances[destinationIndex]};
}

TraversalResult GraphAlgorithms::breadthFirstSearch(
    const TransportationGraph& graph,
    const std::string& startWarehouseId) {
    std::size_t startIndex = 0;
    if (graph.warehouseManager.searchWarehouse(startWarehouseId) == nullptr ||
        !graph.findVertexIndex(startWarehouseId, startIndex)) {
        return {false, {}};
    }

    std::vector<bool> visited(graph.vertices.size(), false);
    std::queue<std::size_t> pending;
    TraversalResult result{true, {}};

    visited[startIndex] = true;
    pending.push(startIndex);

    while (!pending.empty()) {
        const std::size_t currentIndex = pending.front();
        pending.pop();
        result.warehouseIds.push_back(graph.vertices[currentIndex].warehouseId);

        for (const TransportationGraph::Edge& edge :
             graph.vertices[currentIndex].outgoingRoutes) {
            std::size_t neighborIndex = 0;
            if (!graph.findVertexIndex(edge.destinationWarehouseId, neighborIndex) ||
                graph.warehouseManager.searchWarehouse(edge.destinationWarehouseId) == nullptr ||
                visited[neighborIndex]) {
                continue;
            }

            // Mark when enqueued so cycles and converging routes do not duplicate visits.
            visited[neighborIndex] = true;
            pending.push(neighborIndex);
        }
    }

    return result;
}

TraversalResult GraphAlgorithms::depthFirstSearch(
    const TransportationGraph& graph,
    const std::string& startWarehouseId) {
    std::size_t startIndex = 0;
    if (graph.warehouseManager.searchWarehouse(startWarehouseId) == nullptr ||
        !graph.findVertexIndex(startWarehouseId, startIndex)) {
        return {false, {}};
    }

    struct Frame {
        std::size_t vertexIndex;
        std::size_t nextOutgoingIndex;
    };

    std::vector<bool> visited(graph.vertices.size(), false);
    std::vector<Frame> stack;
    TraversalResult result{true, {}};

    visited[startIndex] = true;
    result.warehouseIds.push_back(graph.vertices[startIndex].warehouseId);
    stack.push_back({startIndex, 0});

    while (!stack.empty()) {
        Frame& frame = stack.back();
        const auto& outgoing = graph.vertices[frame.vertexIndex].outgoingRoutes;
        if (frame.nextOutgoingIndex >= outgoing.size()) {
            stack.pop_back();
            continue;
        }

        const TransportationGraph::Edge& edge = outgoing[frame.nextOutgoingIndex];
        ++frame.nextOutgoingIndex;

        std::size_t neighborIndex = 0;
        if (!graph.findVertexIndex(edge.destinationWarehouseId, neighborIndex) ||
            graph.warehouseManager.searchWarehouse(edge.destinationWarehouseId) == nullptr ||
            visited[neighborIndex]) {
            continue;
        }

        // Descend immediately, matching recursive DFS and preserving edge insertion order.
        visited[neighborIndex] = true;
        result.warehouseIds.push_back(graph.vertices[neighborIndex].warehouseId);
        stack.push_back({neighborIndex, 0});
    }

    return result;
}
