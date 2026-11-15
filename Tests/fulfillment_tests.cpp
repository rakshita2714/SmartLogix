#include "fulfillment.h"

#include <exception>
#include <iostream>
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

void expectOrder(const FulfillmentGraph& graph,
                 const std::vector<std::string>& expected) {
    const TopologicalSortResult result = graph.topologicalSort();
    expect(result.status == TopologicalSortStatus::Success,
           "expected an acyclic topological ordering");
    expect(result.operationIds == expected, "topological order is incorrect");
    expect(!graph.hasCycle(), "acyclic graph was reported as cyclic");
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

void emptyGraph() {
    FulfillmentGraph graph;
    const TopologicalSortResult result = graph.topologicalSort();
    expect(result.status == TopologicalSortStatus::Success,
           "empty graph should have a successful empty ordering");
    expect(result.operationIds.empty(), "empty graph returned operations");
    expect(!graph.hasCycle(), "empty graph should not have a cycle");

    std::ostringstream display;
    graph.displayDependencies(display);
    expect(display.str().find("No fulfillment operations found") != std::string::npos,
           "empty graph display message is missing");
}

void singleOperation() {
    FulfillmentGraph graph;
    expect(graph.addOperation("Receive"), "single operation was rejected");
    expectOrder(graph, {"Receive"});
}

void independentOperationsUseRegistrationOrder() {
    FulfillmentGraph graph;
    expect(graph.addOperation("Receive"), "Receive was rejected");
    expect(graph.addOperation("Inspect"), "Inspect was rejected");
    expect(graph.addOperation("Dispatch"), "Dispatch was rejected");
    expectOrder(graph, {"Receive", "Inspect", "Dispatch"});
}

void validDependencyChain() {
    FulfillmentGraph graph;
    expect(graph.addOperation("Receive") && graph.addOperation("Inspect") &&
               graph.addOperation("Allocate") && graph.addOperation("Dispatch"),
           "could not add chain operations");
    expect(graph.addDependency("Receive", "Inspect") &&
               graph.addDependency("Inspect", "Allocate") &&
               graph.addDependency("Allocate", "Dispatch"),
           "could not add chain dependencies");
    expectOrder(graph, {"Receive", "Inspect", "Allocate", "Dispatch"});
}

void multipleValidOrderingsReturnDeterministicOne() {
    FulfillmentGraph graph;
    expect(graph.addOperation("A") && graph.addOperation("B") && graph.addOperation("C"),
           "could not add test operations");
    expect(graph.addDependency("A", "C") && graph.addDependency("B", "C"),
           "could not add test dependencies");

    // B,A,C would also be valid; FIFO queue order is based on registration order.
    expectOrder(graph, {"A", "B", "C"});
}

void branchingDependencies() {
    FulfillmentGraph graph;
    expect(graph.addOperation("Receive") && graph.addOperation("Inspect") &&
               graph.addOperation("Allocate") && graph.addOperation("Dispatch"),
           "could not add branching operations");
    expect(graph.addDependency("Receive", "Inspect") &&
               graph.addDependency("Receive", "Allocate") &&
               graph.addDependency("Inspect", "Dispatch") &&
               graph.addDependency("Allocate", "Dispatch"),
           "could not add branching dependencies");
    expectOrder(graph, {"Receive", "Inspect", "Allocate", "Dispatch"});
}

void multiOperationCycleReturnsNoPartialOrder() {
    FulfillmentGraph graph;
    expect(graph.addOperation("A") && graph.addOperation("B") && graph.addOperation("C"),
           "could not add cyclic operations");
    expect(graph.addDependency("A", "B") && graph.addDependency("B", "C") &&
               graph.addDependency("C", "A"),
           "could not add cycle dependencies");
    expect(graph.hasCycle(), "multi-operation cycle was not detected");

    const TopologicalSortResult result = graph.topologicalSort();
    expect(result.status == TopologicalSortStatus::CycleDetected,
           "cyclic graph did not return CycleDetected");
    expect(result.operationIds.empty(), "cyclic graph returned a partial ordering");
}

void cycleWithIndependentOperationReturnsNoPartialOrder() {
    FulfillmentGraph graph;
    expect(graph.addOperation("A") && graph.addOperation("B") &&
               graph.addOperation("Independent"),
           "could not add test operations");
    expect(graph.addDependency("A", "B") && graph.addDependency("B", "A"),
           "could not add cycle dependencies");
    expect(graph.hasCycle(), "cycle alongside independent node was not detected");
    const TopologicalSortResult result = graph.topologicalSort();
    expect(result.status == TopologicalSortStatus::CycleDetected,
           "cycle alongside independent node was not reported");
    expect(result.operationIds.empty(), "cyclic graph exposed a partial order");
}

void selfDependencyIsAcceptedAndDetected() {
    FulfillmentGraph graph;
    expect(graph.addOperation("Inspect"), "operation was rejected");
    expect(graph.addDependency("Inspect", "Inspect"), "self-dependency was rejected");
    expect(!graph.addDependency("Inspect", "Inspect"),
           "duplicate self-dependency was accepted");
    expect(graph.hasCycle(), "self-dependency was not detected as a cycle");
    const TopologicalSortResult result = graph.topologicalSort();
    expect(result.status == TopologicalSortStatus::CycleDetected && result.operationIds.empty(),
           "self-dependent graph returned an ordering");
}

void duplicateOperationIdIsRejectedWithoutMutation() {
    FulfillmentGraph graph;
    expect(graph.addOperation("Inspect"), "first operation was rejected");
    expect(!graph.addOperation("Inspect"), "duplicate operation ID was accepted");
    expectOrder(graph, {"Inspect"});
}

void duplicateDependencyIsRejectedWithoutMutation() {
    FulfillmentGraph graph;
    expect(graph.addOperation("A") && graph.addOperation("B"),
           "could not add test operations");
    expect(graph.addDependency("A", "B"), "first dependency was rejected");
    expect(!graph.addDependency("A", "B"), "duplicate dependency was accepted");
    expectOrder(graph, {"A", "B"});
}

void missingDependencyEndpointsAreRejectedWithoutMutation() {
    FulfillmentGraph graph;
    expect(graph.addOperation("A"), "operation was rejected");
    expect(!graph.addDependency("Missing", "A"), "missing prerequisite was accepted");
    expect(!graph.addDependency("A", "Missing"), "missing dependent was accepted");
    expect(!graph.addDependency("", "A"), "empty prerequisite was accepted");
    expectOrder(graph, {"A"});
}

void emptyAndWhitespaceOnlyOperationIdsAreRejected() {
    FulfillmentGraph graph;
    expect(!graph.addOperation(""), "empty operation ID was accepted");
    expect(!graph.addOperation("   \t"), "whitespace-only operation ID was accepted");
    const TopologicalSortResult result = graph.topologicalSort();
    expect(result.status == TopologicalSortStatus::Success && result.operationIds.empty(),
           "rejected operations changed the graph");
}

void displayEmptyAndPopulatedDependencies() {
    FulfillmentGraph emptyGraph;
    std::ostringstream emptyDisplay;
    emptyGraph.displayDependencies(emptyDisplay);
    expect(emptyDisplay.str().find("No fulfillment operations found") != std::string::npos,
           "empty graph display is incorrect");

    FulfillmentGraph graph;
    expect(graph.addOperation("Receive") && graph.addOperation("Inspect") &&
               graph.addOperation("Dispatch"),
           "could not add display operations");
    expect(graph.addDependency("Receive", "Inspect"), "dependency was rejected");
    std::ostringstream display;
    graph.displayDependencies(display);
    expect(display.str().find("Receive -> Inspect") != std::string::npos,
           "display omitted dependency direction");
    expect(display.str().find("Dispatch -> (no dependents)") != std::string::npos,
           "display omitted operation with no dependents");
}

void hashLookupRemainsCorrectAfterResizing() {
    FulfillmentGraph graph(1);
    for (int i = 0; i < 20; ++i) {
        expect(graph.addOperation("OP" + std::to_string(i)),
               "operation insertion failed while hash table resized");
    }
    for (int i = 0; i < 19; ++i) {
        expect(graph.addDependency("OP" + std::to_string(i),
                                   "OP" + std::to_string(i + 1)),
               "dependency lookup failed after hash table resizing");
    }

    std::vector<std::string> expected;
    for (int i = 0; i < 20; ++i) {
        expected.push_back("OP" + std::to_string(i));
    }
    expectOrder(graph, expected);
}
}  // namespace

int main() {
    const std::vector<TestResult> results = {
        run("Empty dependency graph", emptyGraph),
        run("Single operation", singleOperation),
        run("Independent operations", independentOperationsUseRegistrationOrder),
        run("Valid dependency chain", validDependencyChain),
        run("Multiple valid orders return a deterministic ordering", multipleValidOrderingsReturnDeterministicOne),
        run("Branching dependencies", branchingDependencies),
        run("Multi-operation cycle returns no partial order", multiOperationCycleReturnsNoPartialOrder),
        run("Cycle with independent operation returns no partial order", cycleWithIndependentOperationReturnsNoPartialOrder),
        run("Self-dependency is accepted and detected", selfDependencyIsAcceptedAndDetected),
        run("Duplicate operation ID is rejected", duplicateOperationIdIsRejectedWithoutMutation),
        run("Duplicate dependency is rejected", duplicateDependencyIsRejectedWithoutMutation),
        run("Missing dependency endpoints are rejected", missingDependencyEndpointsAreRejectedWithoutMutation),
        run("Empty and whitespace-only IDs are rejected", emptyAndWhitespaceOnlyOperationIdsAreRejected),
        run("Display empty and populated dependencies", displayEmptyAndPopulatedDependencies),
        run("Custom hash lookup after resizing", hashLookupRemainsCorrectAfterResizing),
    };

    int passed = 0;
    for (const TestResult& result : results) {
        std::cout << (result.passed ? "[PASS] " : "[FAIL] ") << result.name << '\n';
        passed += result.passed ? 1 : 0;
    }
    std::cout << passed << '/' << results.size() << " tests passed\n";
    return passed == static_cast<int>(results.size()) ? 0 : 1;
}
