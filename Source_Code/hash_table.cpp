#include "hash_table.h"

#include <limits>
#include <stdexcept>

HashTable::HashTable(int size)
    : table(static_cast<std::size_t>(size > 0 ? size : 1)), entryCount(0) {}

// Convert a key into a valid bucket index. Capacity is always at least one.
std::size_t HashTable::hashFunction(const std::string& key) const {
    std::size_t hash = 0;
    for (unsigned char ch : key) {
        hash = (hash * 31 + ch) % table.size();
    }
    return hash;
}

void HashTable::insert(const std::string& key, int value) {
    const std::size_t index = hashFunction(key);

    // Updating an existing key does not change the number of stored entries.
    for (Entry& entry : table[index]) {
        if (entry.first == key) {
            entry.second = value;
            return;
        }
    }

    table[index].push_back({key, value});
    ++entryCount;

    if (loadFactor() > kMaxLoadFactor) {
        if (table.size() > std::numeric_limits<std::size_t>::max() / 2) {
            throw std::length_error("HashTable capacity cannot be doubled");
        }
        rehash(table.size() * 2);
    }
}

int HashTable::search(const std::string& key) const {
    const std::size_t index = hashFunction(key);
    for (const Entry& entry : table[index]) {
        if (entry.first == key) {
            return entry.second;
        }
    }
    return -1;
}

void HashTable::remove(const std::string& key) {
    const std::size_t index = hashFunction(key);
    std::list<Entry>& bucket = table[index];

    for (auto it = bucket.begin(); it != bucket.end(); ++it) {
        if (it->first == key) {
            bucket.erase(it);
            --entryCount;
            return;
        }
    }
}

void HashTable::display() const {
    for (std::size_t i = 0; i < table.size(); ++i) {
        std::cout << "Bucket " << i << ": ";
        for (const Entry& entry : table[i]) {
            std::cout << "[" << entry.first << " -> " << entry.second << "] ";
        }
        std::cout << '\n';
    }
}

std::size_t HashTable::size() const noexcept {
    return entryCount;
}

std::size_t HashTable::capacity() const noexcept {
    return table.size();
}

double HashTable::loadFactor() const noexcept {
    return static_cast<double>(entryCount) / table.size();
}

void HashTable::rehash(std::size_t newCapacity) {
    std::vector<std::list<Entry>> newTable(newCapacity);

    // Recalculate every bucket using the new capacity, preserving each pair.
    for (const std::list<Entry>& bucket : table) {
        for (const Entry& entry : bucket) {
            std::size_t hash = 0;
            for (unsigned char ch : entry.first) {
                hash = (hash * 31 + ch) % newCapacity;
            }
            newTable[hash].push_back(entry);
        }
    }

    table.swap(newTable);
}
