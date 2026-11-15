#include "warehouse_manager.h"

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

Warehouse makeWarehouse(const std::string& id,
                        const std::string& name = "Central Depot",
                        const std::string& location = "North",
                        int capacity = 100) {
    return {id, name, location, capacity};
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

void addAndSearchWarehouse() {
    WarehouseManager manager;
    expect(manager.addWarehouse(makeWarehouse("WH001")), "valid warehouse was rejected");
    const Warehouse* found = manager.searchWarehouse("WH001");
    expect(found != nullptr, "added warehouse was not found");
    expect(found->id == "WH001" && found->name == "Central Depot" &&
               found->location == "North" && found->capacity == 100,
           "search returned incorrect warehouse details");
    expect(manager.size() == 1, "warehouse count should be one");
}

void duplicateIdIsRejected() {
    WarehouseManager manager;
    expect(manager.addWarehouse(makeWarehouse("WH001", "First")), "first insert failed");
    expect(!manager.addWarehouse(makeWarehouse("WH001", "Duplicate")),
           "duplicate warehouse ID was accepted");
    const Warehouse* found = manager.searchWarehouse("WH001");
    expect(found != nullptr && found->name == "First", "duplicate changed the original record");
    expect(manager.size() == 1, "duplicate increased warehouse count");
}

void updateWarehouseDetails() {
    WarehouseManager manager;
    manager.addWarehouse(makeWarehouse("WH001"));
    expect(manager.updateWarehouse("WH001", "Updated Depot", "South", 250),
           "existing warehouse was not updated");
    const Warehouse* found = manager.searchWarehouse("WH001");
    expect(found != nullptr && found->name == "Updated Depot" &&
               found->location == "South" && found->capacity == 250,
           "updated details were not saved");
    expect(found->id == "WH001", "update changed the unique identifier");
}

void invalidSearchAndDeletionAreSafe() {
    WarehouseManager manager;
    manager.addWarehouse(makeWarehouse("WH001"));
    expect(manager.searchWarehouse("MISSING") == nullptr, "missing ID should return nullptr");
    expect(manager.searchWarehouse("") == nullptr, "empty ID should return nullptr");
    expect(!manager.deleteWarehouse("MISSING"), "missing warehouse deletion should fail safely");
    expect(!manager.deleteWarehouse(""), "empty ID deletion should fail safely");
    expect(manager.size() == 1, "invalid deletion changed warehouse count");
    expect(manager.searchWarehouse("WH001") != nullptr, "invalid deletion lost existing warehouse");
}

void deleteAndRepairIndices() {
    WarehouseManager manager(2);
    for (int i = 0; i < 12; ++i) {
        const std::string id = "WH" + std::to_string(100 + i);
        expect(manager.addWarehouse(makeWarehouse(id, "Depot " + std::to_string(i))),
               "warehouse insert failed before deletion");
    }

    expect(manager.deleteWarehouse("WH103"), "existing warehouse was not deleted");
    expect(manager.searchWarehouse("WH103") == nullptr, "deleted warehouse is still searchable");
    expect(manager.size() == 11, "deletion did not reduce warehouse count");
    for (int i = 0; i < 12; ++i) {
        const std::string id = "WH" + std::to_string(100 + i);
        if (i == 3) {
            continue;
        }
        const Warehouse* found = manager.searchWarehouse(id);
        expect(found != nullptr, "deletion left a later ID mapped to a wrong vector index");
        expect(found->name == "Depot " + std::to_string(i), "deletion changed a later record");
    }
}

void invalidWarehouseDataIsRejected() {
    WarehouseManager manager;
    expect(!manager.addWarehouse(makeWarehouse("", "Depot", "North", 1)),
           "empty ID was accepted");
    expect(!manager.addWarehouse(makeWarehouse("WH001", "", "North", 1)),
           "empty name was accepted");
    expect(!manager.addWarehouse(makeWarehouse("WH002", "Depot", "", 1)),
           "empty location was accepted");
    expect(!manager.addWarehouse(makeWarehouse("WH003", "Depot", "North", -1)),
           "negative capacity was accepted");
    expect(!manager.addWarehouse(makeWarehouse("   ", "Depot", "North", 1)),
           "whitespace-only ID was accepted");
    expect(!manager.addWarehouse(makeWarehouse("WH004", "Depot", "   ", 1)),
           "whitespace-only location was accepted");
    expect(manager.size() == 0, "invalid records changed warehouse count");
}

void invalidUpdateIsRejectedWithoutChangingRecord() {
    WarehouseManager manager;
    manager.addWarehouse(makeWarehouse("WH001"));
    expect(!manager.updateWarehouse("MISSING", "Other", "South", 10),
           "update of missing ID should fail");
    expect(!manager.updateWarehouse("WH001", "", "South", 10),
           "update with empty name should fail");
    expect(!manager.updateWarehouse("WH001", "   ", "South", 10),
           "update with whitespace-only name should fail");
    expect(!manager.updateWarehouse("WH001", "Other", "South", -1),
           "update with negative capacity should fail");
    const Warehouse* found = manager.searchWarehouse("WH001");
    expect(found != nullptr && found->name == "Central Depot" && found->capacity == 100,
           "failed update changed existing record");
}

void displayWarehouses() {
    WarehouseManager manager;
    std::ostringstream emptyOutput;
    manager.displayWarehouses(emptyOutput);
    expect(emptyOutput.str().find("No warehouses found") != std::string::npos,
           "empty display did not explain there are no records");

    manager.addWarehouse(makeWarehouse("WH001", "Main Depot", "East", 500));
    std::ostringstream output;
    manager.displayWarehouses(output);
    const std::string text = output.str();
    expect(text.find("WH001") != std::string::npos, "display omitted warehouse ID");
    expect(text.find("Main Depot") != std::string::npos, "display omitted warehouse name");
    expect(text.find("East") != std::string::npos, "display omitted warehouse location");
    expect(text.find("500") != std::string::npos, "display omitted warehouse capacity");
}
}  // namespace

int main() {
    const std::vector<TestResult> results = {
        run("Add and search warehouse", addAndSearchWarehouse),
        run("Reject duplicate warehouse ID", duplicateIdIsRejected),
        run("Update warehouse details", updateWarehouseDetails),
        run("Handle invalid search and deletion safely", invalidSearchAndDeletionAreSafe),
        run("Delete and repair ID-to-index mappings", deleteAndRepairIndices),
        run("Reject invalid warehouse data", invalidWarehouseDataIsRejected),
        run("Reject invalid updates without changing records", invalidUpdateIsRejectedWithoutChangingRecord),
        run("Display empty and populated warehouse lists", displayWarehouses),
    };

    int passed = 0;
    for (const TestResult& result : results) {
        std::cout << (result.passed ? "[PASS] " : "[FAIL] ") << result.name << '\n';
        passed += result.passed ? 1 : 0;
    }
    std::cout << passed << '/' << results.size() << " tests passed\n";
    return passed == static_cast<int>(results.size()) ? 0 : 1;
}
