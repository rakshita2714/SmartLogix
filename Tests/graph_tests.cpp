#include "graph.h"

#include <exception>
#include <iostream>
#include <limits>
#include <sstream>
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

bool addWarehouse(WarehouseManager& manager,
                  TransportationGraph& graph,
                  const std::string& id) {
    const Warehouse warehouse{id, "Depot " + id, "Test location", 100};
    return manager.addWarehouse(warehouse) && graph.registerWarehouseVertex(id);
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

void registerVertices() {
    WarehouseManager manager;
    TransportationGraph graph(manager);
    expect(manager.addWarehouse({"WH001", "Depot 1", "North", 100}),
           "manager rejected valid warehouse");
    expect(graph.registerWarehouseVertex("WH001"), "existing warehouse vertex was rejected");
    expect(!graph.registerWarehouseVertex("WH001"), "duplicate vertex was accepted");
    expect(!graph.registerWarehouseVertex("MISSING"), "missing warehouse vertex was accepted");
    expect(graph.vertexCount() == 1, "vertex count is incorrect");
}

void addAndRetrieveDirectedRoutes() {
    WarehouseManager manager;
    TransportationGraph graph(manager);
    expect(addWarehouse(manager, graph, "WH001"), "could not add WH001");
    expect(addWarehouse(manager, graph, "WH002"), "could not add WH002");
    expect(graph.addRoute("WH001", "WH002", 42.5), "valid route was rejected");

    std::vector<Route> outgoing;
    expect(graph.getOutgoingRoutes("WH001", outgoing), "source warehouse was not found");
    expect(outgoing.size() == 1, "source should have exactly one outgoing route");
    expect(outgoing[0].fromWarehouseId == "WH001" &&
               outgoing[0].toWarehouseId == "WH002" && outgoing[0].distanceKm == 42.5,
           "outgoing route details are incorrect");
    expect(graph.getOutgoingRoutes("WH002", outgoing), "destination warehouse was not found");
    expect(outgoing.empty(), "directed route incorrectly appeared as an outgoing reverse route");
    expect(graph.routeCount() == 1, "route count is incorrect");
}

void rejectInvalidAndDuplicateRoutes() {
    WarehouseManager manager;
    TransportationGraph graph(manager);
    expect(addWarehouse(manager, graph, "WH001"), "could not add WH001");
    expect(addWarehouse(manager, graph, "WH002"), "could not add WH002");

    expect(!graph.addRoute("MISSING", "WH002", 10), "missing source was accepted");
    expect(!graph.addRoute("WH001", "MISSING", 10), "missing destination was accepted");
    expect(!graph.addRoute("", "WH002", 10), "empty source ID was accepted");
    expect(!graph.addRoute("WH001", "WH002", -0.1), "negative distance was accepted");
    expect(!graph.addRoute("WH001", "WH002",
                           std::numeric_limits<double>::infinity()),
           "infinite distance was accepted");
    expect(!graph.addRoute("WH001", "WH002",
                           std::numeric_limits<double>::quiet_NaN()),
           "NaN distance was accepted");
    expect(graph.addRoute("WH001", "WH002", 10), "first valid route was rejected");
    expect(!graph.addRoute("WH001", "WH002", 20), "duplicate directed route was accepted");
    expect(graph.routeCount() == 1, "invalid route attempts changed the graph");
}

void allowZeroDistanceAndSelfLoop() {
    WarehouseManager manager;
    TransportationGraph graph(manager);
    expect(addWarehouse(manager, graph, "WH001"), "could not add WH001");
    expect(graph.addRoute("WH001", "WH001", 0.0), "zero-distance self-loop was rejected");

    std::vector<Route> outgoing;
    expect(graph.getOutgoingRoutes("WH001", outgoing), "self-loop source was not found");
    expect(outgoing.size() == 1 && outgoing[0].toWarehouseId == "WH001" &&
               outgoing[0].distanceKm == 0.0,
           "self-loop route was stored incorrectly");
}

void removeOnlyRequestedDirection() {
    WarehouseManager manager;
    TransportationGraph graph(manager);
    expect(addWarehouse(manager, graph, "WH001"), "could not add WH001");
    expect(addWarehouse(manager, graph, "WH002"), "could not add WH002");
    expect(graph.addRoute("WH001", "WH002", 10), "forward route was rejected");
    expect(graph.addRoute("WH002", "WH001", 12), "reverse route was rejected");
    expect(graph.removeRoute("WH001", "WH002"), "existing route was not removed");
    expect(!graph.removeRoute("WH001", "WH002"), "missing route removal reported success");

    std::vector<Route> outgoing;
    expect(graph.getOutgoingRoutes("WH002", outgoing), "WH002 was not found");
    expect(outgoing.size() == 1 && outgoing[0].toWarehouseId == "WH001",
           "removal affected the reverse route");
}

void distinguishMissingVertexFromNoOutgoingRoutes() {
    WarehouseManager manager;
    TransportationGraph graph(manager);
    expect(addWarehouse(manager, graph, "WH001"), "could not add WH001");

    std::vector<Route> outgoing = {{"old", "data", 1.0}};
    expect(graph.getOutgoingRoutes("WH001", outgoing), "registered warehouse was not found");
    expect(outgoing.empty(), "warehouse with no edges should return an empty list");
    expect(!graph.getOutgoingRoutes("MISSING", outgoing), "missing warehouse was reported as found");
    expect(outgoing.empty(), "failed lookup did not clear the output list");
}

void displayRoutes() {
    WarehouseManager manager;
    TransportationGraph graph(manager);
    std::ostringstream emptyOutput;
    graph.displayRoutes(emptyOutput);
    expect(emptyOutput.str().find("No routes found") != std::string::npos,
           "empty graph display did not report no routes");

    expect(addWarehouse(manager, graph, "WH001"), "could not add WH001");
    expect(addWarehouse(manager, graph, "WH002"), "could not add WH002");
    expect(graph.addRoute("WH001", "WH002", 75.25), "route was rejected");
    std::ostringstream output;
    graph.displayRoutes(output);
    expect(output.str().find("WH001 -> WH002") != std::string::npos,
           "display omitted route endpoints");
    expect(output.str().find("75.25 km") != std::string::npos,
           "display omitted route distance");
}

void deleteWarehouseAndKeepGraphConsistent() {
    WarehouseManager manager;
    TransportationGraph graph(manager, 2);
    expect(addWarehouse(manager, graph, "WH101"), "could not add WH101");
    expect(addWarehouse(manager, graph, "WH102"), "could not add WH102");
    expect(addWarehouse(manager, graph, "WH103"), "could not add WH103");
    expect(graph.addRoute("WH101", "WH102", 10), "route from WH101 was rejected");
    expect(graph.addRoute("WH102", "WH103", 20), "route from WH102 was rejected");
    expect(graph.addRoute("WH103", "WH101", 30), "incoming route to WH101 was rejected");
    expect(graph.addRoute("WH103", "WH102", 35), "second route to WH102 was rejected");

    // Match CLI coordination: remove graph references and then delete the record.
    expect(graph.removeWarehouseVertex("WH101"), "graph vertex was not removed");
    expect(manager.deleteWarehouse("WH101"), "warehouse record was not deleted");
    expect(graph.vertexCount() == 2, "vertex deletion did not reduce vertex count");
    expect(graph.routeCount() == 2, "vertex deletion did not remove all incident routes");
    expect(!graph.addRoute("WH101", "WH102", 99), "deleted warehouse endpoint was accepted");

    std::vector<Route> outgoing;
    expect(!graph.getOutgoingRoutes("WH101", outgoing), "deleted warehouse remained in graph");
    expect(graph.getOutgoingRoutes("WH102", outgoing), "shifted warehouse WH102 was lost");
    expect(outgoing.size() == 1 && outgoing[0].toWarehouseId == "WH103",
           "route from WH102 changed after deleting earlier warehouse");
    expect(graph.getOutgoingRoutes("WH103", outgoing), "shifted warehouse WH103 was lost");
    expect(outgoing.size() == 1 && outgoing[0].toWarehouseId == "WH102",
           "incoming routes to deleted warehouse were not removed");

    expect(manager.addWarehouse({"WH104", "Depot WH104", "Test", 100}),
           "could not add replacement warehouse after deletion");
    expect(graph.registerWarehouseVertex("WH104"), "could not register new graph vertex");
    expect(graph.addRoute("WH104", "WH102", 40), "route from new vertex was rejected");
    expect(graph.getOutgoingRoutes("WH104", outgoing) && outgoing.size() == 1 &&
               outgoing[0].toWarehouseId == "WH102",
           "new graph mapping is incorrect after deletion");
}

void warehouseDetailUpdateDoesNotAffectRoutes() {
    WarehouseManager manager;
    TransportationGraph graph(manager);
    expect(addWarehouse(manager, graph, "WH001"), "could not add WH001");
    expect(addWarehouse(manager, graph, "WH002"), "could not add WH002");
    expect(graph.addRoute("WH001", "WH002", 18), "route was rejected");
    expect(manager.updateWarehouse("WH002", "Renamed", "West", 150),
           "warehouse update failed");
    std::vector<Route> outgoing;
    expect(graph.getOutgoingRoutes("WH001", outgoing) && outgoing.size() == 1 &&
               outgoing[0].toWarehouseId == "WH002" && outgoing[0].distanceKm == 18,
           "non-ID warehouse update broke its route");
}
}  // namespace

int main() {
    const std::vector<TestResult> results = {
        run("Register existing and reject duplicate or missing vertices", registerVertices),
        run("Add and retrieve directed routes", addAndRetrieveDirectedRoutes),
        run("Reject invalid endpoints, distances, and duplicate routes", rejectInvalidAndDuplicateRoutes),
        run("Allow zero-distance routes and self-loops", allowZeroDistanceAndSelfLoop),
        run("Remove only the requested directed route", removeOnlyRequestedDirection),
        run("Distinguish missing vertex from no outgoing routes", distinguishMissingVertexFromNoOutgoingRoutes),
        run("Display empty and populated route lists", displayRoutes),
        run("Delete warehouse and preserve graph consistency", deleteWarehouseAndKeepGraphConsistent),
        run("Warehouse detail updates preserve graph routes", warehouseDetailUpdateDoesNotAffectRoutes),
    };

    int passed = 0;
    for (const TestResult& result : results) {
        std::cout << (result.passed ? "[PASS] " : "[FAIL] ") << result.name << '\n';
        passed += result.passed ? 1 : 0;
    }
    std::cout << passed << '/' << results.size() << " tests passed\n";
    return passed == static_cast<int>(results.size()) ? 0 : 1;
}
