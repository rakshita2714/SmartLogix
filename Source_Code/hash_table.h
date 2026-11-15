#ifndef HASH_TABLE_H
#define HASH_TABLE_H

#include <cstddef>
#include <iostream>
#include <list>
#include <string>
#include <utility>
#include <vector>

// A separate-chaining hash table for warehouse IDs and integer values.
class HashTable {
private:
    using Entry = std::pair<std::string, int>;

    static constexpr double kMaxLoadFactor = 0.75;

    std::vector<std::list<Entry>> table;
    std::size_t entryCount;

    std::size_t hashFunction(const std::string& key) const;
    void rehash(std::size_t newCapacity);

public:
    // Non-positive capacities are safely normalized to one bucket.
    explicit HashTable(int size = 10);

    void insert(const std::string& key, int value);
    int search(const std::string& key) const;
    void remove(const std::string& key);
    void display() const;

    std::size_t size() const noexcept;
    std::size_t capacity() const noexcept;
    double loadFactor() const noexcept;
    static constexpr double maxLoadFactor() noexcept { return kMaxLoadFactor; }
};

#endif
