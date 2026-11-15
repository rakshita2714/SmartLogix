#ifndef FULFILLMENT_H
#define FULFILLMENT_H

#include "hash_table.h"

#include <cstddef>
#include <iosfwd>
#include <string>
#include <vector>

enum class TopologicalSortStatus {
    Success,
    CycleDetected
};

struct TopologicalSortResult {
    TopologicalSortStatus status;
    std::vector<std::string> operationIds;
};

struct FulfillmentDependency {
    std::string prerequisiteId;
    std::string dependentId;
};

// Directed graph describing which fulfillment operation must precede another.
class FulfillmentGraph {
private:
    struct Operation {
        std::string id;
        std::vector<std::size_t> dependents;
    };

    std::vector<Operation> operations;
    HashTable operationIdToIndex;

    bool findOperationIndex(const std::string& operationId,
                            std::size_t& index) const;
    TopologicalSortResult runKahn() const;

public:
    explicit FulfillmentGraph(int initialHashCapacity = 10);

    bool addOperation(const std::string& operationId);
    bool addDependency(const std::string& prerequisiteId,
                       const std::string& dependentId);
    bool hasCycle() const;
    TopologicalSortResult topologicalSort() const;
    void displayDependencies(std::ostream& output) const;
    std::vector<std::string> getOperationIds() const;
    std::vector<FulfillmentDependency> getDependencies() const;
};

#endif
