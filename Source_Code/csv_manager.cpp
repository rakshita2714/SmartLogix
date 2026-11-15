#include "csv_manager.h"

#include <cmath>
#include <cctype>
#include <cerrno>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <locale>
#include <queue>
#include <set>
#include <sstream>
#include <sys/stat.h>
#include <utility>

namespace {
using CsvRecord = CsvPreparedRow;
using CsvLoadResult = PreparedCsvImport;

enum class ParserState {
    FieldStart,
    UnquotedField,
    QuotedField,
    AfterQuotedField
};

struct CsvParseResult {
    bool success = false;
    std::string error;
    std::vector<CsvRecord> records;
};

CsvParseResult parseCsv(const std::string& contents) {
    CsvParseResult result;
    std::vector<std::string> fields;
    std::string field;
    ParserState state = ParserState::FieldStart;
    std::size_t lineNumber = 1;
    std::size_t recordStartLine = 1;
    bool recordTouched = false;

    auto finishRecord = [&]() {
        fields.push_back(field);
        result.records.push_back({std::move(fields), recordStartLine});
        fields.clear();
        field.clear();
        state = ParserState::FieldStart;
        recordTouched = false;
    };

    auto finishLine = [&](std::size_t& index) {
        if (contents[index] == '\r' && index + 1 < contents.size() &&
            contents[index + 1] == '\n') {
            ++index;
        }
        ++lineNumber;
        finishRecord();
        recordStartLine = lineNumber;
    };

    for (std::size_t i = 0; i < contents.size(); ++i) {
        const char ch = contents[i];

        if (state == ParserState::QuotedField) {
            if (ch == '"') {
                if (i + 1 < contents.size() && contents[i + 1] == '"') {
                    field.push_back('"');
                    ++i;
                } else {
                    state = ParserState::AfterQuotedField;
                }
            } else if (ch == '\r') {
                field.push_back('\r');
                if (i + 1 < contents.size() && contents[i + 1] == '\n') {
                    field.push_back('\n');
                    ++i;
                }
                ++lineNumber;
            } else {
                field.push_back(ch);
                if (ch == '\n') {
                    ++lineNumber;
                }
            }
            continue;
        }

        if (state == ParserState::AfterQuotedField) {
            if (ch == ',') {
                fields.push_back(field);
                field.clear();
                state = ParserState::FieldStart;
                recordTouched = true;
            } else if (ch == '\r' || ch == '\n') {
                finishLine(i);
            } else {
                result.error = "Unexpected character after a closing quote at line " +
                               std::to_string(lineNumber) + ".";
                return result;
            }
            continue;
        }

        if (state == ParserState::FieldStart) {
            if (ch == '"') {
                state = ParserState::QuotedField;
                recordTouched = true;
            } else if (ch == ',') {
                fields.emplace_back();
                recordTouched = true;
            } else if (ch == '\r' || ch == '\n') {
                finishLine(i);
            } else {
                field.push_back(ch);
                state = ParserState::UnquotedField;
                recordTouched = true;
            }
            continue;
        }

        // UnquotedField: quotes are only legal at the beginning of a field.
        if (ch == ',') {
            fields.push_back(field);
            field.clear();
            state = ParserState::FieldStart;
            recordTouched = true;
        } else if (ch == '\r' || ch == '\n') {
            finishLine(i);
        } else if (ch == '"') {
            result.error = "Quote inside an unquoted field at line " +
                           std::to_string(lineNumber) + ".";
            return result;
        } else {
            field.push_back(ch);
        }
    }

    if (state == ParserState::QuotedField) {
        result.error = "Unterminated quoted field beginning at line " +
                       std::to_string(recordStartLine) + ".";
        return result;
    }

    if (recordTouched || !fields.empty() || !field.empty() ||
        state == ParserState::UnquotedField || state == ParserState::AfterQuotedField) {
        finishRecord();
    }

    result.success = true;
    return result;
}

CsvLoadResult loadCsv(const std::string& path,
                      const std::vector<std::string>& expectedHeader) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        return {false, "Could not open input file: " + path, {}};
    }

    const std::string contents((std::istreambuf_iterator<char>(input)),
                               std::istreambuf_iterator<char>());
    if (input.bad()) {
        return {false, "Error while reading input file: " + path, {}};
    }

    CsvParseResult parsed = parseCsv(contents);
    if (!parsed.success) {
        return {false, parsed.error, {}};
    }
    if (parsed.records.empty()) {
        return {false, "Input file is empty; expected a CSV header.", {}};
    }

    // Accept an optional UTF-8 BOM at the start of the first header field.
    std::string& firstHeaderField = parsed.records.front().fields.front();
    if (firstHeaderField.size() >= 3 &&
        static_cast<unsigned char>(firstHeaderField[0]) == 0xEF &&
        static_cast<unsigned char>(firstHeaderField[1]) == 0xBB &&
        static_cast<unsigned char>(firstHeaderField[2]) == 0xBF) {
        firstHeaderField.erase(0, 3);
    }

    if (parsed.records.front().fields != expectedHeader) {
        return {false, "CSV header does not match the required format.", {}};
    }

    for (std::size_t i = 1; i < parsed.records.size(); ++i) {
        if (parsed.records[i].fields.size() != expectedHeader.size()) {
            return {false,
                    "Incorrect field count on CSV line " +
                        std::to_string(parsed.records[i].lineNumber) + ".",
                    {}};
        }
    }

    CsvLoadResult loaded;
    loaded.success = true;
    loaded.dataRows.reserve(parsed.records.size() - 1);
    for (std::size_t i = 1; i < parsed.records.size(); ++i) {
        loaded.dataRows.push_back(std::move(parsed.records[i]));
    }
    return loaded;
}

bool containsNonWhitespace(const std::string& value) {
    for (unsigned char ch : value) {
        if (!std::isspace(ch)) {
            return true;
        }
    }
    return false;
}

bool parseInteger(const std::string& text, int& value) {
    std::istringstream input(text);
    input.imbue(std::locale::classic());
    int parsed = 0;
    if (!(input >> parsed)) {
        return false;
    }
    input >> std::ws;
    if (!input.eof()) {
        return false;
    }
    value = parsed;
    return true;
}

bool parseDistance(const std::string& text, double& value) {
    std::istringstream input(text);
    input.imbue(std::locale::classic());
    double parsed = 0.0;
    if (!(input >> parsed)) {
        return false;
    }
    input >> std::ws;
    if (!input.eof() || !std::isfinite(parsed) || parsed < 0.0) {
        return false;
    }
    value = parsed;
    return true;
}

ImportReport failedImport(const std::string& error) {
    return {CsvImportStatus::Failed, 0, 0, error, {}};
}

void rejectRow(ImportReport& report, std::size_t lineNumber, const std::string& message) {
    ++report.rejectedRecords;
    report.rowErrors.push_back({lineNumber, message});
}

void finishImport(ImportReport& report) {
    report.status = report.rejectedRecords == 0 ? CsvImportStatus::Complete
                                                : CsvImportStatus::Partial;
}

std::string escapeField(const std::string& field) {
    const bool needsQuotes = field.find_first_of(",\"\r\n") != std::string::npos;
    if (!needsQuotes) {
        return field;
    }

    std::string escaped;
    escaped.reserve(field.size() + 2);
    escaped.push_back('"');
    for (char ch : field) {
        if (ch == '"') {
            escaped.push_back('"');
        }
        escaped.push_back(ch);
    }
    escaped.push_back('"');
    return escaped;
}

void writeCsvRecord(std::ostream& output, const std::vector<std::string>& fields) {
    for (std::size_t i = 0; i < fields.size(); ++i) {
        if (i != 0) {
            output.put(',');
        }
        output << escapeField(fields[i]);
    }
    output << "\r\n";
}

template <typename Writer>
ExportReport writeCsvFile(const std::string& path,
                          bool allowOverwrite,
                          Writer writer) {
    if (path.empty()) {
        return {false, false, "Output path is empty."};
    }

    struct stat pathInformation {};
    errno = 0;
    const bool exists = stat(path.c_str(), &pathInformation) == 0;
    if (!exists && errno != ENOENT && errno != ENOTDIR) {
        return {false, false, "Could not inspect output path."};
    }
    if (exists && !allowOverwrite) {
        return {false, true, "Output file already exists."};
    }

    std::ofstream output(path, std::ios::binary | std::ios::out | std::ios::trunc);
    if (!output.is_open()) {
        return {false, false, "Could not open output file: " + path};
    }
    output.imbue(std::locale::classic());

    writer(output);
    output.flush();
    if (!output) {
        output.close();
        return {false, false, "Error while writing output file: " + path};
    }
    output.close();
    if (output.fail()) {
        return {false, false, "Error while closing output file: " + path};
    }
    return {true, false, {}};
}

using Relationship = std::pair<std::string, std::string>;
}  // namespace

ImportReport CsvManager::importWarehouses(const std::string& path,
                                          WarehouseManager& warehouses,
                                          TransportationGraph& routes) {
    return applyWarehouses(prepareWarehouses(path), warehouses, routes);
}

PreparedCsvImport CsvManager::prepareWarehouses(const std::string& path) {
    return loadCsv(path, {"id", "name", "location", "capacity"});
}

PreparedCsvImport CsvManager::prepareRoutes(const std::string& path) {
    return loadCsv(path,
                   {"from_warehouse_id", "to_warehouse_id", "distance_km"});
}

PreparedCsvImport CsvManager::prepareOperations(const std::string& path) {
    return loadCsv(path, {"operation_id"});
}

PreparedCsvImport CsvManager::prepareDependencies(const std::string& path) {
    return loadCsv(path,
                   {"prerequisite_operation_id", "dependent_operation_id"});
}

ImportReport CsvManager::applyWarehouses(const PreparedCsvImport& loaded,
                                         WarehouseManager& warehouses,
                                         TransportationGraph& routes) {
    if (!loaded.success) {
        return failedImport(loaded.error);
    }

    ImportReport report{CsvImportStatus::Complete, 0, 0, {}, {}};
    for (const CsvRecord& row : loaded.dataRows) {
        if (!containsNonWhitespace(row.fields[0]) ||
            !containsNonWhitespace(row.fields[1]) ||
            !containsNonWhitespace(row.fields[2])) {
            rejectRow(report, row.lineNumber,
                      "Warehouse ID, name, and location must not be empty.");
            continue;
        }

        int capacity = 0;
        if (!parseInteger(row.fields[3], capacity) || capacity < 0) {
            rejectRow(report, row.lineNumber,
                      "Capacity must be a non-negative integer in range.");
            continue;
        }

        const Warehouse warehouse{row.fields[0], row.fields[1], row.fields[2], capacity};
        if (warehouses.searchWarehouse(warehouse.id) != nullptr) {
            rejectRow(report, row.lineNumber, "Warehouse ID already exists.");
            continue;
        }
        if (!warehouses.addWarehouse(warehouse)) {
            rejectRow(report, row.lineNumber, "Warehouse record was rejected by validation.");
            continue;
        }
        if (!routes.registerWarehouseVertex(warehouse.id)) {
            warehouses.deleteWarehouse(warehouse.id);
            rejectRow(report, row.lineNumber,
                      "Graph vertex registration failed; warehouse addition was rolled back.");
            continue;
        }
        ++report.importedRecords;
    }

    finishImport(report);
    return report;
}

ImportReport CsvManager::importRoutes(const std::string& path,
                                      const WarehouseManager& warehouses,
                                      TransportationGraph& routes) {
    return applyRoutes(prepareRoutes(path), warehouses, routes);
}

ImportReport CsvManager::applyRoutes(const PreparedCsvImport& loaded,
                                     const WarehouseManager& warehouses,
                                     TransportationGraph& routes) {
    if (!loaded.success) {
        return failedImport(loaded.error);
    }

    std::set<Relationship> knownRoutes;
    for (const Route& route : routes.getAllRoutes()) {
        knownRoutes.insert({route.fromWarehouseId, route.toWarehouseId});
    }

    ImportReport report{CsvImportStatus::Complete, 0, 0, {}, {}};
    for (const CsvRecord& row : loaded.dataRows) {
        const std::string& fromId = row.fields[0];
        const std::string& toId = row.fields[1];
        if (!containsNonWhitespace(fromId) || !containsNonWhitespace(toId)) {
            rejectRow(report, row.lineNumber, "Route endpoint IDs must not be empty.");
            continue;
        }
        if (!warehouses.searchWarehouse(fromId)) {
            rejectRow(report, row.lineNumber, "Source warehouse does not exist.");
            continue;
        }
        if (!warehouses.searchWarehouse(toId)) {
            rejectRow(report, row.lineNumber, "Destination warehouse does not exist.");
            continue;
        }

        double distanceKm = 0.0;
        if (!parseDistance(row.fields[2], distanceKm)) {
            rejectRow(report, row.lineNumber,
                      "Distance must be a finite, non-negative number.");
            continue;
        }

        const Relationship relationship{fromId, toId};
        if (knownRoutes.find(relationship) != knownRoutes.end()) {
            rejectRow(report, row.lineNumber, "Directed route already exists.");
            continue;
        }

        if (!routes.addRoute(fromId, toId, distanceKm)) {
            rejectRow(report, row.lineNumber,
                      "Transportation graph rejected the route; check graph registration.");
            continue;
        }

        knownRoutes.insert(relationship);
        ++report.importedRecords;
    }

    finishImport(report);
    return report;
}

ImportReport CsvManager::importOperations(const std::string& path,
                                          FulfillmentGraph& fulfillment) {
    return applyOperations(prepareOperations(path), fulfillment);
}

ImportReport CsvManager::applyOperations(const PreparedCsvImport& loaded,
                                         FulfillmentGraph& fulfillment) {
    if (!loaded.success) {
        return failedImport(loaded.error);
    }

    std::set<std::string> knownOperations;
    for (const std::string& id : fulfillment.getOperationIds()) {
        knownOperations.insert(id);
    }

    ImportReport report{CsvImportStatus::Complete, 0, 0, {}, {}};
    for (const CsvRecord& row : loaded.dataRows) {
        const std::string& id = row.fields[0];
        if (!containsNonWhitespace(id)) {
            rejectRow(report, row.lineNumber, "Operation ID must not be empty.");
            continue;
        }
        if (knownOperations.find(id) != knownOperations.end()) {
            rejectRow(report, row.lineNumber, "Operation ID already exists.");
            continue;
        }
        if (!fulfillment.addOperation(id)) {
            rejectRow(report, row.lineNumber, "Fulfillment graph rejected the operation.");
            continue;
        }

        knownOperations.insert(id);
        ++report.importedRecords;
    }

    finishImport(report);
    return report;
}

ImportReport CsvManager::importDependencies(const std::string& path,
                                            FulfillmentGraph& fulfillment) {
    return applyDependencies(prepareDependencies(path), fulfillment);
}

ImportReport CsvManager::applyDependencies(const PreparedCsvImport& loaded,
                                           FulfillmentGraph& fulfillment) {
    if (!loaded.success) {
        return failedImport(loaded.error);
    }

    std::set<std::string> knownOperations;
    for (const std::string& id : fulfillment.getOperationIds()) {
        knownOperations.insert(id);
    }
    std::set<Relationship> knownDependencies;
    for (const FulfillmentDependency& dependency : fulfillment.getDependencies()) {
        knownDependencies.insert({dependency.prerequisiteId, dependency.dependentId});
    }

    ImportReport report{CsvImportStatus::Complete, 0, 0, {}, {}};
    for (const CsvRecord& row : loaded.dataRows) {
        const std::string& prerequisiteId = row.fields[0];
        const std::string& dependentId = row.fields[1];
        if (!containsNonWhitespace(prerequisiteId) ||
            !containsNonWhitespace(dependentId)) {
            rejectRow(report, row.lineNumber, "Dependency operation IDs must not be empty.");
            continue;
        }
        if (knownOperations.find(prerequisiteId) == knownOperations.end()) {
            rejectRow(report, row.lineNumber, "Prerequisite operation does not exist.");
            continue;
        }
        if (knownOperations.find(dependentId) == knownOperations.end()) {
            rejectRow(report, row.lineNumber, "Dependent operation does not exist.");
            continue;
        }

        const Relationship relationship{prerequisiteId, dependentId};
        if (knownDependencies.find(relationship) != knownDependencies.end()) {
            rejectRow(report, row.lineNumber, "Dependency already exists.");
            continue;
        }
        if (!fulfillment.addDependency(prerequisiteId, dependentId)) {
            rejectRow(report, row.lineNumber, "Fulfillment graph rejected the dependency.");
            continue;
        }

        knownDependencies.insert(relationship);
        ++report.importedRecords;
    }

    finishImport(report);
    return report;
}

ExportReport CsvManager::exportWarehouses(const std::string& path,
                                          const WarehouseManager& warehouses,
                                          bool allowOverwrite) {
    const std::vector<Warehouse> records = warehouses.getAllWarehouses();
    return exportWarehouses(path, records, allowOverwrite);
}

ExportReport CsvManager::exportWarehouses(const std::string& path,
                                          const std::vector<Warehouse>& records,
                                          bool allowOverwrite) {
    return writeCsvFile(path, allowOverwrite, [&records](std::ostream& output) {
        writeCsvRecord(output, {"id", "name", "location", "capacity"});
        for (const Warehouse& warehouse : records) {
            writeCsvRecord(output, {warehouse.id, warehouse.name, warehouse.location,
                                   std::to_string(warehouse.capacity)});
        }
    });
}

ExportReport CsvManager::exportRoutes(const std::string& path,
                                      const TransportationGraph& routes,
                                      bool allowOverwrite) {
    const std::vector<Route> records = routes.getAllRoutes();
    return exportRoutes(path, records, allowOverwrite);
}

ExportReport CsvManager::exportRoutes(const std::string& path,
                                      const std::vector<Route>& records,
                                      bool allowOverwrite) {
    return writeCsvFile(path, allowOverwrite, [&records](std::ostream& output) {
        writeCsvRecord(output,
                       {"from_warehouse_id", "to_warehouse_id", "distance_km"});
        for (const Route& route : records) {
            std::ostringstream distance;
            distance.imbue(std::locale::classic());
            distance << std::setprecision(std::numeric_limits<double>::max_digits10)
                     << route.distanceKm;
            writeCsvRecord(output,
                           {route.fromWarehouseId, route.toWarehouseId, distance.str()});
        }
    });
}

ExportReport CsvManager::exportOperations(const std::string& path,
                                           const FulfillmentGraph& fulfillment,
                                           bool allowOverwrite) {
    const std::vector<std::string> records = fulfillment.getOperationIds();
    return exportOperations(path, records, allowOverwrite);
}

ExportReport CsvManager::exportOperations(const std::string& path,
                                           const std::vector<std::string>& records,
                                           bool allowOverwrite) {
    return writeCsvFile(path, allowOverwrite, [&records](std::ostream& output) {
        writeCsvRecord(output, {"operation_id"});
        for (const std::string& id : records) {
            writeCsvRecord(output, {id});
        }
    });
}

ExportReport CsvManager::exportDependencies(const std::string& path,
                                             const FulfillmentGraph& fulfillment,
                                             bool allowOverwrite) {
    const std::vector<FulfillmentDependency> records = fulfillment.getDependencies();
    return exportDependencies(path, records, allowOverwrite);
}

ExportReport CsvManager::exportDependencies(
    const std::string& path,
    const std::vector<FulfillmentDependency>& records,
    bool allowOverwrite) {
    return writeCsvFile(path, allowOverwrite, [&records](std::ostream& output) {
        writeCsvRecord(output,
                       {"prerequisite_operation_id", "dependent_operation_id"});
        for (const FulfillmentDependency& dependency : records) {
            writeCsvRecord(output, {dependency.prerequisiteId, dependency.dependentId});
        }
    });
}
