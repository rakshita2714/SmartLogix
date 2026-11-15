#include "fulfillment.h"

#include <cctype>
#include <limits>
#include <ostream>
#include <queue>

namespace {
bool containsNonWhitespace(const std::string& text) {
    for (unsigned char ch : text) {
        if (!std::isspace(ch)) {
            return true;
        }
    }
    return false;
}
}  // namespace

FulfillmentGraph::FulfillmentGraph(int initialHashCapacity)
    : operations(), operationIdToIndex(initialHashCapacity) {}

bool FulfillmentGraph::findOperationIndex(const std::string& operationId,
                                          std::size_t& index) const {
    if (operationId.empty()) {
        return false;
    }

    const int storedIndex = operationIdToIndex.search(operationId);
    if (storedIndex < 0 || static_cast<std::size_t>(storedIndex) >= operations.size()) {
        return false;
    }

    index = static_cast<std::size_t>(storedIndex);
    return operations[index].id == operationId;
}

bool FulfillmentGraph::addOperation(const std::string& operationId) {
    if (!containsNonWhitespace(operationId) ||
        operationIdToIndex.search(operationId) != -1 ||
        operations.size() >= static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return false;
    }

    const int index = static_cast<int>(operations.size());
    operations.push_back({operationId, {}});
    operationIdToIndex.insert(operationId, index);
    return true;
}

bool FulfillmentGraph::addDependency(const std::string& prerequisiteId,
                                     const std::string& dependentId) {
    std::size_t prerequisiteIndex = 0;
    std::size_t dependentIndex = 0;
    if (!findOperationIndex(prerequisiteId, prerequisiteIndex) ||
        !findOperationIndex(dependentId, dependentIndex)) {
        return false;
    }

    // Self-dependencies are stored as self-loops and reported by Kahn's algorithm.
    std::vector<std::size_t>& dependents = operations[prerequisiteIndex].dependents;
    for (std::size_t existingIndex : dependents) {
        if (existingIndex == dependentIndex) {
            return false;
        }
    }

    dependents.push_back(dependentIndex);
    return true;
}

TopologicalSortResult FulfillmentGraph::runKahn() const {
    std::vector<std::size_t> indegree(operations.size(), 0);
    for (const Operation& operation : operations) {
        for (std::size_t dependentIndex : operation.dependents) {
            ++indegree[dependentIndex];
        }
    }

    std::queue<std::size_t> ready;
    // Enqueue initial zero-indegree operations in registration order.
    for (std::size_t i = 0; i < operations.size(); ++i) {
        if (indegree[i] == 0) {
            ready.push(i);
        }
    }

    std::vector<std::string> order;
    order.reserve(operations.size());
    while (!ready.empty()) {
        const std::size_t currentIndex = ready.front();
        ready.pop();
        order.push_back(operations[currentIndex].id);

        // Dependents are processed in dependency-edge insertion order.
        for (std::size_t dependentIndex : operations[currentIndex].dependents) {
            --indegree[dependentIndex];
            if (indegree[dependentIndex] == 0) {
                ready.push(dependentIndex);
            }
        }
    }

    if (order.size() != operations.size()) {
        // A partial order is deliberately discarded when any cycle remains.
        return {TopologicalSortStatus::CycleDetected, {}};
    }
    return {TopologicalSortStatus::Success, order};
}

bool FulfillmentGraph::hasCycle() const {
    return runKahn().status == TopologicalSortStatus::CycleDetected;
}

TopologicalSortResult FulfillmentGraph::topologicalSort() const {
    return runKahn();
}

void FulfillmentGraph::displayDependencies(std::ostream& output) const {
    if (operations.empty()) {
        output << "No fulfillment operations found.\n";
        return;
    }

    output << "Fulfillment dependencies (prerequisite -> dependent):\n";
    for (const Operation& operation : operations) {
        output << operation.id << " -> ";
        if (operation.dependents.empty()) {
            output << "(no dependents)\n";
            continue;
        }

        for (std::size_t i = 0; i < operation.dependents.size(); ++i) {
            if (i != 0) {
                output << ", ";
            }
            output << operations[operation.dependents[i]].id;
        }
        output << '\n';
    }
}

std::vector<std::string> FulfillmentGraph::getOperationIds() const {
    std::vector<std::string> operationIds;
    operationIds.reserve(operations.size());
    for (const Operation& operation : operations) {
        operationIds.push_back(operation.id);
    }
    return operationIds;
}

std::vector<FulfillmentDependency> FulfillmentGraph::getDependencies() const {
    std::vector<FulfillmentDependency> dependencies;
    std::size_t count = 0;
    for (const Operation& operation : operations) {
        count += operation.dependents.size();
    }
    dependencies.reserve(count);

    for (const Operation& operation : operations) {
        for (std::size_t dependentIndex : operation.dependents) {
            dependencies.push_back({operation.id, operations[dependentIndex].id});
        }
    }
    return dependencies;
}
