#include "hash_table.h"

#include <exception>
#include <iostream>
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

TestResult run(const std::string& name, void (*test)()) {
    try {
        test();
        return {name, true};
    } catch (const std::exception& error) {
        std::cout << "  Failure detail: " << error.what() << '\n';
        return {name, false};
    }
}

void normalInsertionAndSearch() {
    HashTable table(10);
    table.insert("WH001", 10);
    table.insert("WH003", 30);
    expect(table.search("WH001") == 10, "WH001 value was not found");
    expect(table.search("WH003") == 30, "WH003 value was not found");
    expect(table.size() == 2, "entry count should be two");
}

void collisionHandling() {
    HashTable table(10);
    table.insert("WH002", 2);
    table.insert("WH011", 11);
    expect(table.search("WH002") == 2, "first colliding key was lost");
    expect(table.search("WH011") == 11, "second colliding key was lost");
    expect(table.size() == 2, "both colliding keys should be stored");
}

void duplicateKeyUpdate() {
    HashTable table(10);
    table.insert("WH002", 2);
    table.insert("WH002", 99);
    expect(table.search("WH002") == 99, "duplicate insert did not update value");
    expect(table.size() == 1, "duplicate key created an extra entry");
}

void missingSearch() {
    HashTable table(10);
    expect(table.search("missing") == -1, "missing key should return -1");
}

void removal() {
    HashTable table(10);
    table.insert("WH002", 2);
    table.insert("WH011", 11);
    table.remove("WH002");
    expect(table.search("WH002") == -1, "removed key is still present");
    expect(table.search("WH011") == 11, "removal broke the other collision entry");
    expect(table.size() == 1, "removing a key did not update size");
    table.remove("not-present");
    expect(table.size() == 1, "removing a missing key changed size");
}

void invalidCapacity() {
    HashTable zeroCapacity(0);
    HashTable negativeCapacity(-5);
    expect(zeroCapacity.capacity() == 1, "zero capacity was not normalized");
    expect(negativeCapacity.capacity() == 1, "negative capacity was not normalized");
    zeroCapacity.insert("safe", 7);
    negativeCapacity.insert("safe", 8);
    expect(zeroCapacity.search("safe") == 7, "zero-capacity table failed to operate");
    expect(negativeCapacity.search("safe") == 8, "negative-capacity table failed to operate");
}

void automaticResizeAndPreservation() {
    HashTable table(4);
    table.insert("WH001", 1);
    table.insert("WH002", 2);
    table.insert("WH011", 11);
    expect(table.capacity() == 4, "table resized before exceeding 0.75 load factor");
    table.insert("WH004", 4);
    expect(table.capacity() == 8, "table did not double after exceeding threshold");
    expect(table.loadFactor() == 0.5, "load factor is incorrect after rehash");
    expect(table.size() == 4, "rehash changed the number of entries");
    expect(table.search("WH001") == 1, "rehash lost WH001");
    expect(table.search("WH002") == 2, "rehash lost WH002");
    expect(table.search("WH011") == 11, "rehash lost WH011");
    expect(table.search("WH004") == 4, "rehash lost WH004");
    table.remove("WH011");
    expect(table.search("WH011") == -1, "remove failed after resizing");
    expect(table.search("WH002") == 2, "search failed after removal following resize");
}
}  // namespace

int main() {
    const std::vector<TestResult> results = {
        run("Normal insertion and search", normalInsertionAndSearch),
        run("Collision handling (WH002 / WH011)", collisionHandling),
        run("Duplicate key update", duplicateKeyUpdate),
        run("Searching for a missing key", missingSearch),
        run("Removing existing and missing keys", removal),
        run("Zero and negative constructor capacity", invalidCapacity),
        run("Automatic resizing and entry preservation", automaticResizeAndPreservation),
    };

    int passed = 0;
    for (const TestResult& result : results) {
        std::cout << (result.passed ? "[PASS] " : "[FAIL] ") << result.name << '\n';
        passed += result.passed ? 1 : 0;
    }
    std::cout << passed << '/' << results.size() << " tests passed\n";
    return passed == static_cast<int>(results.size()) ? 0 : 1;
}
