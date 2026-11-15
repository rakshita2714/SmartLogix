#include "algorithms.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
struct TestResult {
    std::string name;
    bool passed;
};

void expect(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void expectPath(const ShortestPathResult& result,
                const std::vector<std::string>& expectedPath,
                double expectedDistance) {
    expect(result.status == ShortestPathStatus::Found, "expected a found shortest path");
    expect(result.warehouseIds == expectedPath, "shortest path IDs are incorrect");
    expect(std::abs(result.totalDistanceKm - expectedDistance) <=
               std::max(1.0, std::abs(expectedDistance)) * 1e-12,
           "shortest path distance is incorrect");
}

bool addVertex(WarehouseManager& manager,
               TransportationGraph& graph,
               const std::string& id) {
    return manager.addWarehouse({id, "Depot " + id, "Test location", 100}) &&
           graph.registerWarehouseVertex(id);
}

TestResult run(const std::string& name, void (*test)()) {
    try {
        test();
        return {name, true};
    } catch (const std::exception& error) {
        std::cout << "  Failure detail: " << error.what() << '\n';
        return {name, false};
    }
}

void directShortestRoute() {
    WarehouseManager manager;
    TransportationGraph graph(manager);
    expect(addVertex(manager, graph, "A") && addVertex(manager, graph, "B"),
           "could not register test vertices");
    expect(graph.addRoute("A", "B", 8.0), "direct route was rejected");
    expectPath(GraphAlgorithms::dijkstra(graph, "A", "B"), {"A", "B"}, 8.0);
}

void shortestRouteWithIntermediateWarehouses() {
    WarehouseManager manager;
    TransportationGraph graph(manager);
    for (const std::string& id : {"A", "B", "C", "D"}) {
        expect(addVertex(manager, graph, id), "could not register test vertex");
    }
    expect(graph.addRoute("A", "B", 3), "route A-B was rejected");
    expect(graph.addRoute("B", "C", 4), "route B-C was rejected");
    expect(graph.addRoute("C", "D", 5), "route C-D was rejected");
    expect(graph.addRoute("A", "D", 20), "route A-D was rejected");
    expectPath(GraphAlgorithms::dijkstra(graph, "A", "D"),
               {"A", "B", "C", "D"}, 12.0);
}

void shortestRouteCanUseMoreEdges() {
    WarehouseManager manager;
    TransportationGraph graph(manager);
    for (const std::string& id : {"A", "B", "C", "D"}) {
        expect(addVertex(manager, graph, id), "could not register test vertex");
    }
    expect(graph.addRoute("A", "D", 100), "direct route was rejected");
    expect(graph.addRoute("A", "B", 1), "route A-B was rejected");
    expect(graph.addRoute("B", "C", 1), "route B-C was rejected");
    expect(graph.addRoute("C", "D", 1), "route C-D was rejected");
    expectPath(GraphAlgorithms::dijkstra(graph, "A", "D"),
               {"A", "B", "C", "D"}, 3.0);
}

void unreachableDestination() {
    WarehouseManager manager;
    TransportationGraph graph(manager);
    expect(addVertex(manager, graph, "A") && addVertex(manager, graph, "B") &&
               addVertex(manager, graph, "C"),
           "could not register test vertices");
    expect(graph.addRoute("A", "B", 2), "route A-B was rejected");
    const ShortestPathResult result = GraphAlgorithms::dijkstra(graph, "A", "C");
    expect(result.status == ShortestPathStatus::Unreachable,
           "disconnected destination was not reported unreachable");
    expect(result.warehouseIds.empty(), "unreachable result should not contain a path");
}

void invalidWarehouseIds() {
    WarehouseManager manager;
    TransportationGraph graph(manager);
    expect(addVertex(manager, graph, "A"), "could not register test vertex");

    expect(GraphAlgorithms::dijkstra(graph, "MISSING", "A").status ==
               ShortestPathStatus::InvalidSource,
           "invalid source ID was not reported");
    expect(GraphAlgorithms::dijkstra(graph, "A", "MISSING").status ==
               ShortestPathStatus::InvalidDestination,
           "invalid destination ID was not reported");
    expect(!GraphAlgorithms::breadthFirstSearch(graph, "MISSING").validStart,
           "BFS accepted an invalid start ID");
    expect(!GraphAlgorithms::depthFirstSearch(graph, "MISSING").validStart,
           "DFS accepted an invalid start ID");
}

void sourceEqualsDestination() {
    WarehouseManager manager;
    TransportationGraph graph(manager);
    expect(addVertex(manager, graph, "A"), "could not register test vertex");
    expectPath(GraphAlgorithms::dijkstra(graph, "A", "A"), {"A"}, 0.0);
}

void bfsPreservesInsertionOrder() {
    WarehouseManager manager;
    TransportationGraph graph(manager);
    for (const std::string& id : {"S", "A", "B", "C", "D", "E"}) {
        expect(addVertex(manager, graph, id), "could not register test vertex");
    }
    expect(graph.addRoute("S", "A", 1) && graph.addRoute("S", "B", 1) &&
               graph.addRoute("A", "C", 1) && graph.addRoute("A", "D", 1) &&
               graph.addRoute("B", "E", 1),
           "could not construct BFS test graph");

    const TraversalResult result = GraphAlgorithms::breadthFirstSearch(graph, "S");
    expect(result.validStart, "BFS rejected a valid start ID");
    expect(result.warehouseIds == std::vector<std::string>({"S", "A", "B", "C", "D", "E"}),
           "BFS order is incorrect");
}

void dfsPreservesRecursiveInsertionOrder() {
    WarehouseManager manager;
    TransportationGraph graph(manager);
    for (const std::string& id : {"S", "A", "B", "C", "D", "E"}) {
        expect(addVertex(manager, graph, id), "could not register test vertex");
    }
    expect(graph.addRoute("S", "A", 1) && graph.addRoute("S", "B", 1) &&
               graph.addRoute("A", "C", 1) && graph.addRoute("A", "D", 1) &&
               graph.addRoute("B", "E", 1),
           "could not construct DFS test graph");

    const TraversalResult result = GraphAlgorithms::depthFirstSearch(graph, "S");
    expect(result.validStart, "DFS rejected a valid start ID");
    expect(result.warehouseIds == std::vector<std::string>({"S", "A", "C", "D", "B", "E"}),
           "DFS order is incorrect");
}

void traversalsStayWithinDisconnectedComponent() {
    WarehouseManager manager;
    TransportationGraph graph(manager);
    for (const std::string& id : {"A", "B", "C", "D"}) {
        expect(addVertex(manager, graph, id), "could not register test vertex");
    }
    expect(graph.addRoute("A", "B", 1) && graph.addRoute("C", "D", 1),
           "could not construct disconnected graph");

    const std::vector<std::string> expected{"A", "B"};
    expect(GraphAlgorithms::breadthFirstSearch(graph, "A").warehouseIds == expected,
           "BFS crossed into a disconnected component");
    expect(GraphAlgorithms::depthFirstSearch(graph, "A").warehouseIds == expected,
           "DFS crossed into a disconnected component");
}

void traversalsTerminateOnCyclesAndVisitOnce() {
    WarehouseManager manager;
    TransportationGraph graph(manager);
    for (const std::string& id : {"A", "B", "C", "D"}) {
        expect(addVertex(manager, graph, id), "could not register test vertex");
    }
    expect(graph.addRoute("A", "B", 1) && graph.addRoute("B", "C", 1) &&
               graph.addRoute("C", "A", 1) && graph.addRoute("C", "D", 1),
           "could not construct cyclic graph");

    const std::vector<std::string> expected{"A", "B", "C", "D"};
    expect(GraphAlgorithms::breadthFirstSearch(graph, "A").warehouseIds == expected,
           "BFS did not visit each cyclic-graph vertex exactly once");
    expect(GraphAlgorithms::depthFirstSearch(graph, "A").warehouseIds == expected,
           "DFS did not visit each cyclic-graph vertex exactly once");
}

void singleVertexGraph() {
    WarehouseManager manager;
    TransportationGraph graph(manager);
    expect(addVertex(manager, graph, "A"), "could not register single vertex");

    expectPath(GraphAlgorithms::dijkstra(graph, "A", "A"), {"A"}, 0.0);
    expect(GraphAlgorithms::breadthFirstSearch(graph, "A").warehouseIds ==
               std::vector<std::string>({"A"}),
           "BFS failed on a single-vertex graph");
    expect(GraphAlgorithms::depthFirstSearch(graph, "A").warehouseIds ==
               std::vector<std::string>({"A"}),
           "DFS failed on a single-vertex graph");
}

void overflowedDistanceDoesNotCorruptShortestPath() {
    WarehouseManager manager;
    TransportationGraph graph(manager);
    expect(addVertex(manager, graph, "S") && addVertex(manager, graph, "A") &&
               addVertex(manager, graph, "D"),
           "could not register test vertices");
    const double largeFiniteDistance = 1e308;
    expect(graph.addRoute("S", "A", largeFiniteDistance) &&
               graph.addRoute("A", "D", largeFiniteDistance) &&
               graph.addRoute("S", "D", largeFiniteDistance),
           "could not construct overflow test graph");

    const ShortestPathResult result = GraphAlgorithms::dijkstra(graph, "S", "D");
    expectPath(result, {"S", "D"}, largeFiniteDistance);
}
}  // namespace

int main() {
    const std::vector<TestResult> results = {
        run("Direct shortest route", directShortestRoute),
        run("Shortest route with intermediate warehouses", shortestRouteWithIntermediateWarehouses),
        run("Shortest path may use more edges", shortestRouteCanUseMoreEdges),
        run("Unreachable destination", unreachableDestination),
        run("Invalid warehouse IDs", invalidWarehouseIds),
        run("Source equals destination", sourceEqualsDestination),
        run("BFS insertion-order traversal", bfsPreservesInsertionOrder),
        run("DFS recursive-style insertion order", dfsPreservesRecursiveInsertionOrder),
        run("Traversal of disconnected graph", traversalsStayWithinDisconnectedComponent),
        run("Traversals terminate on cycles", traversalsTerminateOnCyclesAndVisitOnce),
        run("Single-vertex graph", singleVertexGraph),
        run("Overflowed distance is ignored safely", overflowedDistanceDoesNotCorruptShortestPath),
    };

    int passed = 0;
    for (const TestResult& result : results) {
        std::cout << (result.passed ? "[PASS] " : "[FAIL] ") << result.name << '\n';
        passed += result.passed ? 1 : 0;
    }
    std::cout << passed << '/' << results.size() << " tests passed\n";
    return passed == static_cast<int>(results.size()) ? 0 : 1;
}
