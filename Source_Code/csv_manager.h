#ifndef CSV_MANAGER_H
#define CSV_MANAGER_H

#include "fulfillment.h"
#include "graph.h"
#include "warehouse_manager.h"

#include <cstddef>
#include <string>
#include <vector>

enum class CsvImportStatus {
    Complete,
    Partial,
    Failed
};

struct CsvRowError {
    std::size_t lineNumber;
    std::string message;
};

struct ImportReport {
    CsvImportStatus status;
    std::size_t importedRecords;
    std::size_t rejectedRecords;
    std::string fileError;
    std::vector<CsvRowError> rowErrors;
};

struct ExportReport {
    bool success;
    bool overwriteRequired;
    std::string error;
};

struct CsvPreparedRow {
    std::vector<std::string> fields;
    std::size_t lineNumber;
};

// Parsed and header-validated input, ready for state-dependent validation.
struct PreparedCsvImport {
    bool success = false;
    std::string error;
    std::vector<CsvPreparedRow> dataRows;
};

// Reads and writes the four CSV datasets without exposing mutable graph storage.
class CsvManager {
public:
    static PreparedCsvImport prepareWarehouses(const std::string& path);
    static PreparedCsvImport prepareRoutes(const std::string& path);
    static PreparedCsvImport prepareOperations(const std::string& path);
    static PreparedCsvImport prepareDependencies(const std::string& path);

    static ImportReport applyWarehouses(const PreparedCsvImport& loaded,
                                        WarehouseManager& warehouses,
                                        TransportationGraph& routes);
    static ImportReport applyRoutes(const PreparedCsvImport& loaded,
                                    const WarehouseManager& warehouses,
                                    TransportationGraph& routes);
    static ImportReport applyOperations(const PreparedCsvImport& loaded,
                                        FulfillmentGraph& fulfillment);
    static ImportReport applyDependencies(const PreparedCsvImport& loaded,
                                          FulfillmentGraph& fulfillment);

    static ImportReport importWarehouses(const std::string& path,
                                         WarehouseManager& warehouses,
                                         TransportationGraph& routes);
    static ImportReport importRoutes(const std::string& path,
                                     const WarehouseManager& warehouses,
                                     TransportationGraph& routes);
    static ImportReport importOperations(const std::string& path,
                                         FulfillmentGraph& fulfillment);
    static ImportReport importDependencies(const std::string& path,
                                           FulfillmentGraph& fulfillment);

    // Existing files are never overwritten unless allowOverwrite is true.
    static ExportReport exportWarehouses(const std::string& path,
                                         const WarehouseManager& warehouses,
                                         bool allowOverwrite = false);
    static ExportReport exportRoutes(const std::string& path,
                                     const TransportationGraph& routes,
                                     bool allowOverwrite = false);
    static ExportReport exportOperations(const std::string& path,
                                         const FulfillmentGraph& fulfillment,
                                         bool allowOverwrite = false);
    static ExportReport exportDependencies(const std::string& path,
                                           const FulfillmentGraph& fulfillment,
                                           bool allowOverwrite = false);

    // Snapshot overloads let callers release store locks before filesystem I/O.
    static ExportReport exportWarehouses(const std::string& path,
                                         const std::vector<Warehouse>& records,
                                         bool allowOverwrite = false);
    static ExportReport exportRoutes(const std::string& path,
                                     const std::vector<Route>& records,
                                     bool allowOverwrite = false);
    static ExportReport exportOperations(const std::string& path,
                                         const std::vector<std::string>& records,
                                         bool allowOverwrite = false);
    static ExportReport exportDependencies(
        const std::string& path,
        const std::vector<FulfillmentDependency>& records,
        bool allowOverwrite = false);
};

#endif
