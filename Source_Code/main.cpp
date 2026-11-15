#include "smartlogix_service.h"

#include <cmath>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {
bool parseInteger(const std::string& text, int& value) {
    std::istringstream input(text);
    int parsed = 0;
    if (!(input >> parsed)) {
        return false;
    }

    char extra = '\0';
    if (input >> extra) {
        return false;
    }
    if (!input.eof()) {
        return false;
    }

    value = parsed;
    return true;
}

bool parseDistance(const std::string& text, double& value) {
    std::istringstream input(text);
    double parsed = 0.0;
    if (!(input >> parsed)) {
        return false;
    }

    char extra = '\0';
    if (input >> extra) {
        return false;
    }
    if (!input.eof() || !std::isfinite(parsed) || parsed < 0.0) {
        return false;
    }

    value = parsed;
    return true;
}

std::string readLine(const std::string& prompt) {
    std::cout << prompt;
    std::string value;
    std::getline(std::cin, value);
    return value;
}

bool readCapacity(int& capacity) {
    const std::string text = readLine("Capacity (non-negative integer): ");
    return parseInteger(text, capacity) && capacity >= 0;
}

bool readDistance(double& distanceKm) {
    const std::string text = readLine("Distance in kilometres (non-negative): ");
    return parseDistance(text, distanceKm);
}

void printMenu() {
    std::cout << "\nSMARTLOGIX WAREHOUSE MANAGEMENT\n"
              << "1. Add warehouse\n"
              << "2. Search warehouse\n"
              << "3. Update warehouse\n"
              << "4. Delete warehouse\n"
              << "5. Display all warehouses\n"
              << "6. Add route\n"
              << "7. Remove route\n"
              << "8. Display all routes\n"
              << "9. Show outgoing routes for a warehouse\n"
              << "10. Find shortest route\n"
              << "11. BFS traversal\n"
              << "12. DFS traversal\n"
              << "13. Add fulfillment operation\n"
              << "14. Add fulfillment dependency\n"
              << "15. Display fulfillment dependencies\n"
              << "16. Detect fulfillment dependency cycle\n"
              << "17. Topologically sort fulfillment operations\n"
              << "18. Import warehouses from CSV\n"
              << "19. Export warehouses to CSV\n"
              << "20. Import transportation routes from CSV\n"
              << "21. Export transportation routes to CSV\n"
              << "22. Import fulfillment operations from CSV\n"
              << "23. Export fulfillment operations to CSV\n"
              << "24. Import fulfillment dependencies from CSV\n"
              << "25. Export fulfillment dependencies to CSV\n"
              << "0. Exit\n";
}

void displayWarehouses(const std::vector<Warehouse>& warehouses) {
    if (warehouses.empty()) {
        std::cout << "No warehouses found.\n";
        return;
    }
    std::cout << "Warehouses:\n";
    for (const Warehouse& warehouse : warehouses) {
        std::cout << "ID: " << warehouse.id
                  << " | Name: " << warehouse.name
                  << " | Location: " << warehouse.location
                  << " | Capacity: " << warehouse.capacity << '\n';
    }
}

void displayRoutes(const std::vector<Route>& routes) {
    if (routes.empty()) {
        std::cout << "No routes found.\n";
        return;
    }
    for (const Route& route : routes) {
        std::cout << route.fromWarehouseId << " -> " << route.toWarehouseId
                  << " : " << route.distanceKm << " km\n";
    }
}

void displayDependencies(const std::vector<std::string>& operations,
                         const std::vector<FulfillmentDependency>& dependencies) {
    if (operations.empty()) {
        std::cout << "No fulfillment operations found.\n";
        return;
    }
    std::cout << "Fulfillment dependencies (prerequisite -> dependent):\n";
    for (const std::string& operation : operations) {
        std::cout << operation << " -> ";
        bool foundDependent = false;
        for (const FulfillmentDependency& dependency : dependencies) {
            if (dependency.prerequisiteId != operation) {
                continue;
            }
            if (foundDependent) {
                std::cout << ", ";
            }
            std::cout << dependency.dependentId;
            foundDependent = true;
        }
        if (!foundDependent) {
            std::cout << "(no dependents)";
        }
        std::cout << '\n';
    }
}

void displayTraversal(const std::string& name, const TraversalResult& result) {
    if (!result.validStart) {
        std::cout << "Invalid starting warehouse ID.\n";
        return;
    }

    std::cout << name << " order: ";
    for (std::size_t i = 0; i < result.warehouseIds.size(); ++i) {
        if (i != 0) {
            std::cout << " -> ";
        }
        std::cout << result.warehouseIds[i];
    }
    std::cout << '\n';
}

void displayImportReport(const ImportReport& report) {
    if (report.status == CsvImportStatus::Failed) {
        std::cout << "Import failed: " << report.fileError << '\n';
        return;
    }

    if (report.status == CsvImportStatus::Partial) {
        std::cout << "Import partially completed: " << report.importedRecords
                  << " imported, " << report.rejectedRecords << " rejected.\n";
    } else {
        std::cout << "Import complete: " << report.importedRecords
                  << " imported, 0 rejected.\n";
    }
    for (const CsvRowError& error : report.rowErrors) {
        std::cout << "  Line " << error.lineNumber << ": " << error.message << '\n';
    }
}

template <typename ExportFunction>
void runExport(const std::string& path, ExportFunction exportFunction) {
    ExportReport report = exportFunction(path, false);
    if (report.overwriteRequired) {
        const std::string answer = readLine("File exists. Overwrite it? (y/N): ");
        if (answer != "y" && answer != "Y") {
            std::cout << "Export cancelled; existing file was not changed.\n";
            return;
        }
        report = exportFunction(path, true);
    }

    if (report.success) {
        std::cout << "Export completed successfully.\n";
    } else {
        std::cout << "Export failed: " << report.error << '\n';
    }
}
}  // namespace

int main() {
    SmartLogixService service;

    while (true) {
        printMenu();
        const std::string choiceText = readLine("Choose an option: ");
        if (std::cin.fail()) {
            std::cout << "Input closed. Exiting SmartLogix.\n";
            return 0;
        }
        int choice = -1;
        if (!parseInteger(choiceText, choice)) {
            std::cout << "Invalid option. Enter a menu number.\n";
            continue;
        }

        if (choice == 0) {
            std::cout << "Exiting SmartLogix.\n";
            return 0;
        }

        if (choice == 1) {
            Warehouse warehouse;
            warehouse.id = readLine("Warehouse ID: ");
            warehouse.name = readLine("Warehouse name: ");
            warehouse.location = readLine("Warehouse location: ");
            if (!readCapacity(warehouse.capacity)) {
                std::cout << "Invalid capacity. Warehouse was not added.\n";
            } else if (service.addWarehouse(warehouse)) {
                std::cout << "Warehouse added.\n";
            } else {
                std::cout << "Warehouse was not added. Check the fields and ID uniqueness.\n";
            }
        } else if (choice == 2) {
            const std::string id = readLine("Warehouse ID to search: ");
            const std::optional<Warehouse> warehouse = service.searchWarehouse(id);
            if (!warehouse) {
                std::cout << "Warehouse not found.\n";
            } else {
                std::cout << "ID: " << warehouse->id
                          << " | Name: " << warehouse->name
                          << " | Location: " << warehouse->location
                          << " | Capacity: " << warehouse->capacity << '\n';
            }
        } else if (choice == 3) {
            const std::string id = readLine("Warehouse ID to update: ");
            const std::string name = readLine("New warehouse name: ");
            const std::string location = readLine("New warehouse location: ");
            int capacity = -1;
            if (!readCapacity(capacity)) {
                std::cout << "Invalid capacity. Warehouse was not updated.\n";
            } else if (service.updateWarehouse(id, name, location, capacity)) {
                std::cout << "Warehouse updated.\n";
            } else {
                std::cout << "Warehouse was not updated. Check the ID and fields.\n";
            }
        } else if (choice == 4) {
            const std::string id = readLine("Warehouse ID to delete: ");
            if (!service.searchWarehouse(id)) {
                std::cout << "Warehouse not found; nothing was deleted.\n";
            } else if (service.deleteWarehouse(id)) {
                std::cout << "Warehouse and its routes deleted.\n";
            } else {
                std::cout << "Warehouse graph vertex is missing; warehouse was not deleted.\n";
            }
        } else if (choice == 5) {
            displayWarehouses(service.getAllWarehouses());
        } else if (choice == 6) {
            const std::string fromId = readLine("From warehouse ID: ");
            const std::string toId = readLine("To warehouse ID: ");
            double distanceKm = 0.0;
            if (!readDistance(distanceKm)) {
                std::cout << "Invalid distance. Route was not added.\n";
            } else if (service.addRoute(fromId, toId, distanceKm)) {
                std::cout << "Route added.\n";
            } else {
                std::cout << "Route was not added. Check the endpoints, distance, and duplicates.\n";
            }
        } else if (choice == 7) {
            const std::string fromId = readLine("From warehouse ID: ");
            const std::string toId = readLine("To warehouse ID: ");
            if (service.removeRoute(fromId, toId)) {
                std::cout << "Route removed.\n";
            } else {
                std::cout << "Route not found; nothing was removed.\n";
            }
        } else if (choice == 8) {
            displayRoutes(service.getAllRoutes());
        } else if (choice == 9) {
            const std::string id = readLine("Warehouse ID: ");
            const std::optional<std::vector<Route>> result = service.getOutgoingRoutes(id);
            if (!result) {
                std::cout << "Warehouse not found in the transportation graph.\n";
            } else if (result->empty()) {
                std::cout << "No outgoing routes for " << id << ".\n";
            } else {
                for (const Route& route : *result) {
                    std::cout << route.fromWarehouseId << " -> " << route.toWarehouseId
                              << " : " << route.distanceKm << " km\n";
                }
            }
        } else if (choice == 10) {
            const std::string sourceId = readLine("Source warehouse ID: ");
            const std::string destinationId = readLine("Destination warehouse ID: ");
            const ShortestPathResult result =
                service.shortestPath(sourceId, destinationId);

            if (result.status == ShortestPathStatus::InvalidSource) {
                std::cout << "Invalid source warehouse ID.\n";
            } else if (result.status == ShortestPathStatus::InvalidDestination) {
                std::cout << "Invalid destination warehouse ID.\n";
            } else if (result.status == ShortestPathStatus::Unreachable) {
                std::cout << "Destination is unreachable from the source.\n";
            } else {
                std::cout << "Shortest route: ";
                for (std::size_t i = 0; i < result.warehouseIds.size(); ++i) {
                    if (i != 0) {
                        std::cout << " -> ";
                    }
                    std::cout << result.warehouseIds[i];
                }
                std::cout << "\nTotal distance: " << result.totalDistanceKm << " km\n";
            }
        } else if (choice == 11) {
            const std::string startId = readLine("Starting warehouse ID: ");
            displayTraversal("BFS", service.breadthFirstSearch(startId));
        } else if (choice == 12) {
            const std::string startId = readLine("Starting warehouse ID: ");
            displayTraversal("DFS", service.depthFirstSearch(startId));
        } else if (choice == 13) {
            const std::string operationId = readLine("Fulfillment operation ID: ");
            if (service.addOperation(operationId)) {
                std::cout << "Fulfillment operation added.\n";
            } else {
                std::cout << "Operation was not added. Enter a non-empty unique ID.\n";
            }
        } else if (choice == 14) {
            const std::string prerequisiteId = readLine("Prerequisite operation ID: ");
            const std::string dependentId = readLine("Dependent operation ID: ");
            if (service.addDependency(prerequisiteId, dependentId)) {
                std::cout << "Fulfillment dependency added.\n";
            } else {
                std::cout << "Dependency was not added. Check both IDs and duplicates.\n";
            }
        } else if (choice == 15) {
            const FulfillmentSnapshot snapshot = service.getFulfillmentSnapshot();
            displayDependencies(snapshot.operationIds, snapshot.dependencies);
        } else if (choice == 16) {
            if (service.hasFulfillmentCycle()) {
                std::cout << "Cycle detected in fulfillment dependencies.\n";
            } else {
                std::cout << "No cycle detected in fulfillment dependencies.\n";
            }
        } else if (choice == 17) {
            const TopologicalSortResult result = service.topologicalSort();
            if (result.status == TopologicalSortStatus::CycleDetected) {
                std::cout << "Cycle detected; no valid topological ordering can be produced.\n";
            } else if (result.operationIds.empty()) {
                std::cout << "No fulfillment operations to sort.\n";
            } else {
                std::cout << "Topological order: ";
                for (std::size_t i = 0; i < result.operationIds.size(); ++i) {
                    if (i != 0) {
                        std::cout << " -> ";
                    }
                    std::cout << result.operationIds[i];
                }
                std::cout << '\n';
            }
        } else if (choice == 18) {
            const std::string path = readLine("Warehouse CSV file path: ");
            displayImportReport(service.importWarehouses(path));
        } else if (choice == 19) {
            const std::string path = readLine("Warehouse CSV output path: ");
            runExport(path, [&service](const std::string& outputPath, bool overwrite) {
                return service.exportWarehouses(outputPath, overwrite);
            });
        } else if (choice == 20) {
            const std::string path = readLine("Transportation routes CSV file path: ");
            displayImportReport(service.importRoutes(path));
        } else if (choice == 21) {
            const std::string path = readLine("Transportation routes CSV output path: ");
            runExport(path, [&service](const std::string& outputPath, bool overwrite) {
                return service.exportRoutes(outputPath, overwrite);
            });
        } else if (choice == 22) {
            const std::string path = readLine("Fulfillment operations CSV file path: ");
            displayImportReport(service.importOperations(path));
        } else if (choice == 23) {
            const std::string path = readLine("Fulfillment operations CSV output path: ");
            runExport(path, [&service](const std::string& outputPath, bool overwrite) {
                return service.exportOperations(outputPath, overwrite);
            });
        } else if (choice == 24) {
            const std::string path = readLine("Fulfillment dependencies CSV file path: ");
            displayImportReport(service.importDependencies(path));
        } else if (choice == 25) {
            const std::string path = readLine("Fulfillment dependencies CSV output path: ");
            runExport(path, [&service](const std::string& outputPath, bool overwrite) {
                return service.exportDependencies(outputPath, overwrite);
            });
        } else {
            std::cout << "Unknown option. Choose 0 through 25.\n";
        }
    }
}
