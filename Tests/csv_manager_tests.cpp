#include "csv_manager.h"

#include <chrono>
#include <cmath>
#include <exception>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#if defined(__has_include)
#if __has_include(<filesystem>)
#include <filesystem>
#define SMARTLOGIX_TEST_HAS_FILESYSTEM 1
#endif
#endif

#if !defined(SMARTLOGIX_TEST_HAS_FILESYSTEM)
#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif
#endif

namespace {
struct TestResult {
    std::string name;
    bool passed;
};

#if defined(SMARTLOGIX_TEST_HAS_FILESYSTEM)
using TestPath = std::filesystem::path;
#else
class TestPath {
public:
    TestPath() = default;
    explicit TestPath(std::string value) : value(std::move(value)) {}

    std::string string() const { return value; }
    TestPath operator/(const std::string& child) const {
#if defined(_WIN32)
        const char separator = '\\';
#else
        const char separator = '/';
#endif
        if (!value.empty() && (value.back() == '/' || value.back() == '\\')) {
            return TestPath(value + child);
        }
        return TestPath(value + separator + child);
    }

private:
    std::string value;
};
#endif

class TemporaryDirectory {
public:
    TemporaryDirectory() {
#if defined(SMARTLOGIX_TEST_HAS_FILESYSTEM)
        const auto stamp = std::chrono::high_resolution_clock::now()
                               .time_since_epoch()
                               .count();
        directory = std::filesystem::temp_directory_path() /
                    ("SmartLogixCsvTests_" + std::to_string(stamp));
        std::filesystem::create_directories(directory);
#elif defined(_WIN32)
        char tempPath[MAX_PATH + 1] = {};
        const DWORD length = GetTempPathA(MAX_PATH, tempPath);
        if (length == 0 || length > MAX_PATH) {
            throw std::runtime_error("could not locate Windows temporary directory");
        }
        char tempName[MAX_PATH + 1] = {};
        if (GetTempFileNameA(tempPath, "SLX", 0, tempName) == 0) {
            throw std::runtime_error("could not create a temporary test path");
        }
        DeleteFileA(tempName);
        if (!CreateDirectoryA(tempName, nullptr)) {
            throw std::runtime_error("could not create a temporary test directory");
        }
        directory = TestPath(tempName);
#else
        char tempTemplate[] = "/tmp/SmartLogixCsvTestsXXXXXX";
        char* createdPath = mkdtemp(tempTemplate);
        if (createdPath == nullptr) {
            throw std::runtime_error("could not create a temporary test directory");
        }
        directory = TestPath(createdPath);
#endif
    }

    ~TemporaryDirectory() {
#if defined(SMARTLOGIX_TEST_HAS_FILESYSTEM)
        std::error_code error;
        std::filesystem::remove_all(directory, error);
#elif defined(_WIN32)
        for (const std::string& file : files) {
            std::remove(file.c_str());
        }
        RemoveDirectoryA(directory.string().c_str());
#else
        for (const std::string& file : files) {
            std::remove(file.c_str());
        }
        rmdir(directory.string().c_str());
#endif
    }

    TestPath file(const std::string& name) const {
#if !defined(SMARTLOGIX_TEST_HAS_FILESYSTEM)
        files.push_back((directory / name).string());
#endif
        return directory / name;
    }

    const TestPath& path() const {
        return directory;
    }

private:
    TestPath directory;
#if !defined(SMARTLOGIX_TEST_HAS_FILESYSTEM)
    mutable std::vector<std::string> files;
#endif
};

struct TestSystem {
    WarehouseManager warehouses;
    TransportationGraph routes;
    FulfillmentGraph fulfillment;

    TestSystem() : warehouses(), routes(warehouses), fulfillment() {}
};

void expect(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void writeText(const TestPath& path, const std::string& text) {
    std::ofstream output(path.string(), std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("could not create test CSV file");
    }
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    output.close();
    if (output.fail()) {
        throw std::runtime_error("could not finish writing test CSV file");
    }
}

std::string readText(const TestPath& path) {
    std::ifstream input(path.string(), std::ios::binary);
    if (!input) {
        throw std::runtime_error("could not read test CSV file");
    }
    return std::string(std::istreambuf_iterator<char>(input),
                       std::istreambuf_iterator<char>());
}

bool addWarehouse(TestSystem& system, const Warehouse& warehouse) {
    if (!system.warehouses.addWarehouse(warehouse)) {
        return false;
    }
    if (!system.routes.registerWarehouseVertex(warehouse.id)) {
        system.warehouses.deleteWarehouse(warehouse.id);
        return false;
    }
    return true;
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

void successfulImportsAndQuotedFields() {
    TemporaryDirectory temporary;
    TestSystem system;
    const auto warehousesFile = temporary.file("warehouses.csv");
    const auto routesFile = temporary.file("routes.csv");
    const auto operationsFile = temporary.file("operations.csv");
    const auto dependenciesFile = temporary.file("dependencies.csv");

    writeText(warehousesFile,
              "id,name,location,capacity\r\n"
              "WH001,\"Central, East Depot\",\"Mumbai \"\"Harbor\"\"\r\nDock\",500\r\n"
              "WH002,North Hub,Delhi,300\r\n");
    writeText(routesFile,
              "from_warehouse_id,to_warehouse_id,distance_km\r\n"
              "WH001,WH002,42.5\r\n");
    writeText(operationsFile,
              "operation_id\r\n"
              "RECEIVE_GOODS\r\n"
              "QUALITY_INSPECTION\r\n");
    writeText(dependenciesFile,
              "prerequisite_operation_id,dependent_operation_id\r\n"
              "RECEIVE_GOODS,QUALITY_INSPECTION\r\n");

    const ImportReport warehouseReport =
        CsvManager::importWarehouses(warehousesFile.string(), system.warehouses, system.routes);
    expect(warehouseReport.status == CsvImportStatus::Complete &&
               warehouseReport.importedRecords == 2,
           "warehouse CSV import did not complete");
    const std::vector<Warehouse> warehouses = system.warehouses.getAllWarehouses();
    expect(warehouses.size() == 2 && warehouses[0].name == "Central, East Depot" &&
               warehouses[0].location == "Mumbai \"Harbor\"\r\nDock" &&
               system.routes.vertexCount() == 2,
           "quoted warehouse fields or graph registration are incorrect");

    const ImportReport routeReport =
        CsvManager::importRoutes(routesFile.string(), system.warehouses, system.routes);
    expect(routeReport.status == CsvImportStatus::Complete &&
               routeReport.importedRecords == 1 && system.routes.routeCount() == 1,
           "transportation route import did not complete");

    const ImportReport operationReport =
        CsvManager::importOperations(operationsFile.string(), system.fulfillment);
    expect(operationReport.status == CsvImportStatus::Complete &&
               operationReport.importedRecords == 2,
           "fulfillment operation import did not complete");
    const ImportReport dependencyReport =
        CsvManager::importDependencies(dependenciesFile.string(), system.fulfillment);
    expect(dependencyReport.status == CsvImportStatus::Complete &&
               dependencyReport.importedRecords == 1,
           "fulfillment dependency import did not complete");
}

void fileLevelErrorsDoNotMutateState() {
    TemporaryDirectory temporary;
    TestSystem system;
    const auto emptyFile = temporary.file("empty.csv");
    const auto wrongHeader = temporary.file("wrong_header.csv");
    const auto missingFile = temporary.file("missing.csv");
    writeText(emptyFile, "");
    writeText(wrongHeader, "warehouse_id,name,location,capacity\n");

    const ImportReport missing = CsvManager::importWarehouses(
        missingFile.string(), system.warehouses, system.routes);
    const ImportReport empty = CsvManager::importWarehouses(
        emptyFile.string(), system.warehouses, system.routes);
    const ImportReport header = CsvManager::importWarehouses(
        wrongHeader.string(), system.warehouses, system.routes);
    expect(missing.status == CsvImportStatus::Failed &&
               empty.status == CsvImportStatus::Failed &&
               header.status == CsvImportStatus::Failed,
           "missing, empty, or wrong-header file was not rejected");
    expect(system.warehouses.size() == 0 && system.routes.vertexCount() == 0,
           "file-level import error changed application state");
}

void headerOnlyFilesImportZeroRows() {
    TemporaryDirectory temporary;
    TestSystem system;
    const auto warehousesFile = temporary.file("warehouses.csv");
    const auto routesFile = temporary.file("routes.csv");
    const auto operationsFile = temporary.file("operations.csv");
    const auto dependenciesFile = temporary.file("dependencies.csv");
    writeText(warehousesFile, "id,name,location,capacity\n");
    writeText(routesFile, "from_warehouse_id,to_warehouse_id,distance_km\n");
    writeText(operationsFile, "operation_id\n");
    writeText(dependenciesFile,
              "prerequisite_operation_id,dependent_operation_id\n");

    expect(CsvManager::importWarehouses(warehousesFile.string(), system.warehouses,
                                        system.routes).status == CsvImportStatus::Complete,
           "header-only warehouse file was not accepted");
    expect(CsvManager::importRoutes(routesFile.string(), system.warehouses,
                                    system.routes).status == CsvImportStatus::Complete,
           "header-only route file was not accepted");
    expect(CsvManager::importOperations(operationsFile.string(), system.fulfillment).status ==
               CsvImportStatus::Complete,
           "header-only operation file was not accepted");
    expect(CsvManager::importDependencies(dependenciesFile.string(), system.fulfillment).status ==
               CsvImportStatus::Complete,
           "header-only dependency file was not accepted");
}

void malformedCsvNearEndAppliesNoEarlierRows() {
    TemporaryDirectory temporary;
    TestSystem system;
    const auto file = temporary.file("malformed.csv");
    writeText(file,
              "id,name,location,capacity\n"
              "WH001,Depot,Mumbai,100\n"
              "WH002,\"unterminated,Mumbai,200\n");

    const ImportReport report =
        CsvManager::importWarehouses(file.string(), system.warehouses, system.routes);
    expect(report.status == CsvImportStatus::Failed,
           "malformed CSV syntax was not a file-level failure");
    expect(system.warehouses.size() == 0 && system.routes.vertexCount() == 0,
           "valid rows before malformed syntax were partially applied");
}

void mixedWarehouseRowsReportPartialAndStayConsistent() {
    TemporaryDirectory temporary;
    TestSystem system;
    const auto file = temporary.file("mixed_warehouses.csv");
    writeText(file,
              "id,name,location,capacity\n"
              "WH001,Main Depot,Mumbai,500\n"
              "WH001,Duplicate,Mumbai,10\n"
              "WH002,Invalid Capacity,Delhi,-1\n"
              "WH004,North Hub,Delhi,300\n");

    const ImportReport report =
        CsvManager::importWarehouses(file.string(), system.warehouses, system.routes);
    expect(report.status == CsvImportStatus::Partial && report.importedRecords == 2 &&
               report.rejectedRecords == 2 && report.rowErrors.size() == 2,
           "mixed warehouse import report is incorrect");
    expect(system.warehouses.size() == 2 && system.routes.vertexCount() == 2,
           "warehouse and transportation graph became inconsistent");
}

void overflowingCapacityIsRejected() {
    TemporaryDirectory temporary;
    TestSystem system;
    const auto file = temporary.file("capacity_overflow.csv");
    writeText(file,
              "id,name,location,capacity\n"
              "WH001,Too Large,Somewhere,2147483648\n"
              "WH002,Not Numeric,Somewhere,abc\n"
              "WH003,Fraction,Somewhere,1.5\n");
    const ImportReport report =
        CsvManager::importWarehouses(file.string(), system.warehouses, system.routes);
    expect(report.status == CsvImportStatus::Partial && report.importedRecords == 0 &&
               report.rejectedRecords == 3,
           "invalid, fractional, or overflowing capacities were not rejected");
}

void emptyRequiredFieldsAreRejected() {
    TemporaryDirectory temporary;
    TestSystem system;
    const auto warehouseFile = temporary.file("empty_warehouse_fields.csv");
    writeText(warehouseFile,
              "id,name,location,capacity\n"
              ",Depot,North,10\n"
              "WH001,,North,10\n"
              "WH002,Depot,   ,10\n"
              "WH003,Depot,South,10\n");
    const ImportReport warehouseReport = CsvManager::importWarehouses(
        warehouseFile.string(), system.warehouses, system.routes);
    expect(warehouseReport.status == CsvImportStatus::Partial &&
               warehouseReport.importedRecords == 1 && warehouseReport.rejectedRecords == 3,
           "empty required warehouse fields were not rejected");

    const auto operationFile = temporary.file("empty_operation_id.csv");
    writeText(operationFile, "operation_id\n\" \t \"\n");
    const ImportReport operationReport =
        CsvManager::importOperations(operationFile.string(), system.fulfillment);
    expect(operationReport.status == CsvImportStatus::Partial &&
               operationReport.importedRecords == 0 && operationReport.rejectedRecords == 1,
           "whitespace-only operation ID was not rejected");

    const auto dependencyFile = temporary.file("empty_dependency_ids.csv");
    expect(system.fulfillment.addOperation("A"), "could not seed operation A");
    writeText(dependencyFile,
              "prerequisite_operation_id,dependent_operation_id\n"
              ",A\n"
              "A,\n");
    const ImportReport dependencyReport = CsvManager::importDependencies(
        dependencyFile.string(), system.fulfillment);
    expect(dependencyReport.status == CsvImportStatus::Partial &&
               dependencyReport.rejectedRecords == 2 &&
               system.fulfillment.getDependencies().empty(),
           "empty dependency endpoints were not rejected cleanly");
}

void routeRowsValidateReferencesDistancesAndDuplicates() {
    TemporaryDirectory temporary;
    TestSystem system;
    expect(addWarehouse(system, {"A", "A Depot", "North", 10}) &&
               addWarehouse(system, {"B", "B Depot", "South", 10}),
           "could not seed route import warehouses");
    expect(system.routes.addRoute("A", "B", 1), "could not seed duplicate route");

    const auto file = temporary.file("mixed_routes.csv");
    writeText(file,
              "from_warehouse_id,to_warehouse_id,distance_km\n"
              "B,A,0\n"
              "B,A,1\n"
              "A,B,9\n"
              "MISSING,B,1\n"
              "A,MISSING,1\n"
              "B,A,-1\n"
              "B,A,nan\n"
              "B,A,inf\n"
              "B,A,1e9999\n");
    const ImportReport report =
        CsvManager::importRoutes(file.string(), system.warehouses, system.routes);
    expect(report.status == CsvImportStatus::Partial && report.importedRecords == 1 &&
               report.rejectedRecords == 8,
           "route partial-import counts are incorrect");
    expect(system.routes.routeCount() == 2,
           "invalid or duplicate route rows changed graph state");
}

void operationRowsRejectDuplicatesAndInvalidIds() {
    TemporaryDirectory temporary;
    FulfillmentGraph graph;
    expect(graph.addOperation("EXISTING"), "could not seed operation");
    const auto file = temporary.file("mixed_operations.csv");
    writeText(file,
              "operation_id\n"
              "RECEIVE\n"
              "RECEIVE\n"
              "EXISTING\n"
              "\"   \"\n"
              "\"QUALITY, CHECK\"\n");

    const ImportReport report = CsvManager::importOperations(file.string(), graph);
    expect(report.status == CsvImportStatus::Partial && report.importedRecords == 2 &&
               report.rejectedRecords == 3,
           "operation duplicate/invalid-row report is incorrect");
    expect(graph.getOperationIds() ==
               std::vector<std::string>({"EXISTING", "RECEIVE", "QUALITY, CHECK"}),
           "operation import changed or lost an ID");
}

void dependencyRowsValidateReferencesDuplicatesAndSelfLoops() {
    TemporaryDirectory temporary;
    FulfillmentGraph graph;
    expect(graph.addOperation("A") && graph.addOperation("B"),
           "could not seed fulfillment operations");
    expect(graph.addDependency("A", "B"), "could not seed dependency");
    const auto file = temporary.file("mixed_dependencies.csv");
    writeText(file,
              "prerequisite_operation_id,dependent_operation_id\n"
              "B,A\n"
              "B,A\n"
              "A,B\n"
              "MISSING,A\n"
              "A,MISSING\n"
              "A,A\n");

    const ImportReport report = CsvManager::importDependencies(file.string(), graph);
    expect(report.status == CsvImportStatus::Partial && report.importedRecords == 2 &&
               report.rejectedRecords == 4,
           "dependency partial-import counts are incorrect");
    expect(graph.getDependencies().size() == 3,
           "invalid or duplicate dependencies changed the graph");
    expect(graph.hasCycle(), "imported self-dependency was not detected as a cycle");
}

void incorrectColumnCountIsFileLevelAndAtomic() {
    TemporaryDirectory temporary;
    FulfillmentGraph graph;
    const auto file = temporary.file("column_count.csv");
    writeText(file,
              "operation_id\n"
              "A\n"
              "B,EXTRA\n");
    const ImportReport report = CsvManager::importOperations(file.string(), graph);
    expect(report.status == CsvImportStatus::Failed && report.importedRecords == 0 &&
               report.rejectedRecords == 0,
           "incorrect column count was not treated as a structural error");
    expect(graph.getOperationIds().empty(),
           "valid rows before incorrect column count were partially imported");
}

void exportAndReimportPreservesAllDatasets() {
    TemporaryDirectory temporary;
    TestSystem original;
    const Warehouse first{"WH001", "Central, \"East\"\r\nDepot", "Mumbai, MH", 500};
    const Warehouse second{"WH002", "North Hub", "Delhi", 300};
    expect(addWarehouse(original, first) && addWarehouse(original, second),
           "could not seed warehouse round-trip data");
    expect(original.routes.addRoute("WH001", "WH002", 0.1),
           "could not seed route round-trip data");
    const std::string operationA = "RECEIVE, \"A\"\nGOODS";
    expect(original.fulfillment.addOperation(operationA) &&
               original.fulfillment.addOperation("INSPECT"),
           "could not seed operation round-trip data");
    expect(original.fulfillment.addDependency(operationA, "INSPECT"),
           "could not seed dependency round-trip data");

    const auto warehouseFile = temporary.file("roundtrip_warehouses.csv");
    const auto routeFile = temporary.file("roundtrip_routes.csv");
    const auto operationFile = temporary.file("roundtrip_operations.csv");
    const auto dependencyFile = temporary.file("roundtrip_dependencies.csv");
    expect(CsvManager::exportWarehouses(warehouseFile.string(), original.warehouses).success,
           "warehouse export failed");
    expect(CsvManager::exportRoutes(routeFile.string(), original.routes).success,
           "route export failed");
    expect(CsvManager::exportOperations(operationFile.string(), original.fulfillment).success,
           "operation export failed");
    expect(CsvManager::exportDependencies(dependencyFile.string(), original.fulfillment).success,
           "dependency export failed");

    TestSystem restored;
    expect(CsvManager::importWarehouses(warehouseFile.string(), restored.warehouses,
                                        restored.routes).status == CsvImportStatus::Complete,
           "warehouse reimport failed");
    expect(CsvManager::importRoutes(routeFile.string(), restored.warehouses,
                                    restored.routes).status == CsvImportStatus::Complete,
           "route reimport failed");
    expect(CsvManager::importOperations(operationFile.string(), restored.fulfillment).status ==
               CsvImportStatus::Complete,
           "operation reimport failed");
    expect(CsvManager::importDependencies(dependencyFile.string(), restored.fulfillment).status ==
               CsvImportStatus::Complete,
           "dependency reimport failed");

    const auto originalWarehouses = original.warehouses.getAllWarehouses();
    const auto restoredWarehouses = restored.warehouses.getAllWarehouses();
    expect(originalWarehouses.size() == restoredWarehouses.size(),
           "warehouse round-trip count changed");
    for (std::size_t i = 0; i < originalWarehouses.size(); ++i) {
        expect(originalWarehouses[i].id == restoredWarehouses[i].id &&
                   originalWarehouses[i].name == restoredWarehouses[i].name &&
                   originalWarehouses[i].location == restoredWarehouses[i].location &&
                   originalWarehouses[i].capacity == restoredWarehouses[i].capacity,
               "warehouse round-trip data changed");
    }
    const auto originalRoutes = original.routes.getAllRoutes();
    const auto restoredRoutes = restored.routes.getAllRoutes();
    expect(originalRoutes.size() == restoredRoutes.size() &&
               originalRoutes[0].fromWarehouseId == restoredRoutes[0].fromWarehouseId &&
               originalRoutes[0].toWarehouseId == restoredRoutes[0].toWarehouseId &&
               originalRoutes[0].distanceKm == restoredRoutes[0].distanceKm,
           "route round-trip data changed");
    expect(original.fulfillment.getOperationIds() == restored.fulfillment.getOperationIds(),
           "operation IDs changed during round-trip");
    const auto originalDependencies = original.fulfillment.getDependencies();
    const auto restoredDependencies = restored.fulfillment.getDependencies();
    expect(originalDependencies.size() == restoredDependencies.size() &&
               originalDependencies[0].prerequisiteId == restoredDependencies[0].prerequisiteId &&
               originalDependencies[0].dependentId == restoredDependencies[0].dependentId,
           "dependency relationships changed during round-trip");
}

void emptyExportsStillContainHeaders() {
    TemporaryDirectory temporary;
    TestSystem system;
    const auto warehousesFile = temporary.file("empty_warehouses.csv");
    const auto routesFile = temporary.file("empty_routes.csv");
    const auto operationsFile = temporary.file("empty_operations.csv");
    const auto dependenciesFile = temporary.file("empty_dependencies.csv");

    expect(CsvManager::exportWarehouses(warehousesFile.string(), system.warehouses).success &&
               CsvManager::exportRoutes(routesFile.string(), system.routes).success &&
               CsvManager::exportOperations(operationsFile.string(), system.fulfillment).success &&
               CsvManager::exportDependencies(dependenciesFile.string(), system.fulfillment).success,
           "one or more empty exports failed");
    expect(readText(warehousesFile) == "id,name,location,capacity\r\n",
           "empty warehouse export omitted header");
    expect(readText(routesFile) ==
               "from_warehouse_id,to_warehouse_id,distance_km\r\n",
           "empty route export omitted header");
    expect(readText(operationsFile) == "operation_id\r\n",
           "empty operation export omitted header");
    expect(readText(dependenciesFile) ==
               "prerequisite_operation_id,dependent_operation_id\r\n",
           "empty dependency export omitted header");
}

void overwriteMustBeConfirmedAndCanBeAllowed() {
    TemporaryDirectory temporary;
    WarehouseManager warehouses;
    expect(warehouses.addWarehouse({"WH001", "Depot", "North", 10}),
           "could not seed export warehouse");
    const auto file = temporary.file("existing.csv");
    writeText(file, "keep existing content\n");

    const ExportReport declined = CsvManager::exportWarehouses(file.string(), warehouses);
    expect(!declined.success && declined.overwriteRequired,
           "existing file was not protected from silent overwrite");
    expect(readText(file) == "keep existing content\n",
           "declined overwrite changed the file");

    const ExportReport confirmed = CsvManager::exportWarehouses(file.string(), warehouses, true);
    expect(confirmed.success && readText(file).find("WH001,Depot,North,10") != std::string::npos,
           "confirmed overwrite did not replace file contents");
}

void exportWriteFailuresAreReported() {
    TemporaryDirectory temporary;
    WarehouseManager warehouses;
    const auto invalidPath = temporary.path() / "missing_parent" / "output.csv";
    const ExportReport report = CsvManager::exportWarehouses(invalidPath.string(), warehouses);
    expect(!report.success && !report.error.empty(),
           "unwritable output path did not return a useful error");
}

void duplicateWarehouseImportDoesNotChangeGraph() {
    TemporaryDirectory temporary;
    TestSystem system;
    expect(addWarehouse(system, {"WH001", "Existing", "North", 10}),
           "could not seed warehouse");
    const auto file = temporary.file("duplicate_warehouse.csv");
    writeText(file,
              "id,name,location,capacity\n"
              "WH001,New Name,South,20\n"
              "WH002,New Warehouse,East,30\n");
    const ImportReport report =
        CsvManager::importWarehouses(file.string(), system.warehouses, system.routes);
    expect(report.status == CsvImportStatus::Partial && report.importedRecords == 1 &&
               report.rejectedRecords == 1,
           "duplicate warehouse report is incorrect");
    expect(system.warehouses.size() == 2 && system.routes.vertexCount() == 2,
           "duplicate warehouse caused manager/graph inconsistency");
    expect(system.warehouses.searchWarehouse("WH001")->name == "Existing",
           "duplicate import overwrote existing warehouse");
}
}  // namespace

int main() {
    const std::vector<TestResult> results = {
        run("Successful imports and quoted CSV fields", successfulImportsAndQuotedFields),
        run("File-level errors do not mutate state", fileLevelErrorsDoNotMutateState),
        run("Header-only files import zero rows", headerOnlyFilesImportZeroRows),
        run("Malformed CSV near EOF applies no earlier rows", malformedCsvNearEndAppliesNoEarlierRows),
        run("Mixed warehouse rows report partial and remain consistent", mixedWarehouseRowsReportPartialAndStayConsistent),
        run("Overflowing capacity is rejected", overflowingCapacityIsRejected),
        run("Empty required fields are rejected", emptyRequiredFieldsAreRejected),
        run("Route references, distances, and duplicates are validated", routeRowsValidateReferencesDistancesAndDuplicates),
        run("Operation duplicates and invalid IDs are rejected", operationRowsRejectDuplicatesAndInvalidIds),
        run("Dependency references, duplicates, and self-loops are handled", dependencyRowsValidateReferencesDuplicatesAndSelfLoops),
        run("Incorrect column count fails before any mutation", incorrectColumnCountIsFileLevelAndAtomic),
        run("All four datasets round-trip", exportAndReimportPreservesAllDatasets),
        run("Empty exports include headers", emptyExportsStillContainHeaders),
        run("Overwrite requires confirmation", overwriteMustBeConfirmedAndCanBeAllowed),
        run("Export write failure is reported", exportWriteFailuresAreReported),
        run("Duplicate warehouse import preserves graph consistency", duplicateWarehouseImportDoesNotChangeGraph),
    };

    int passed = 0;
    for (const TestResult& result : results) {
        std::cout << (result.passed ? "[PASS] " : "[FAIL] ") << result.name << '\n';
        passed += result.passed ? 1 : 0;
    }
    std::cout << passed << '/' << results.size() << " tests passed\n";
    return passed == static_cast<int>(results.size()) ? 0 : 1;
}
