find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Test)

add_executable(test_calculation
    test_calculation.cpp
    ${PROJECT_SOURCE_DIR}/src/core/StatementEntry.h
    ${PROJECT_SOURCE_DIR}/src/core/CalculationService.h
    ${PROJECT_SOURCE_DIR}/src/core/CalculationService.cpp
)

add_executable(test_entry_table_model
    test_entry_table_model.cpp
    ${PROJECT_SOURCE_DIR}/src/core/StatementEntry.h
    ${PROJECT_SOURCE_DIR}/src/core/CalculationService.h
    ${PROJECT_SOURCE_DIR}/src/core/CalculationService.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/PasteColumnResolver.h
    ${PROJECT_SOURCE_DIR}/src/ui/PasteColumnResolver.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/commands/ModelStateCommand.h
    ${PROJECT_SOURCE_DIR}/src/ui/commands/ModelStateCommand.cpp
    ${PROJECT_SOURCE_DIR}/src/ui/models/EntryTableModel.h
    ${PROJECT_SOURCE_DIR}/src/ui/models/EntryTableModel.cpp
)

add_executable(test_exporters
    test_exporters.cpp
    ${PROJECT_SOURCE_DIR}/src/core/StatementEntry.h
    ${PROJECT_SOURCE_DIR}/src/core/CalculationService.h
    ${PROJECT_SOURCE_DIR}/src/core/CalculationService.cpp
    ${PROJECT_SOURCE_DIR}/src/export/EntryExportFormatter.h
    ${PROJECT_SOURCE_DIR}/src/export/EntryExportFormatter.cpp
    ${PROJECT_SOURCE_DIR}/src/export/ExcelExporter.h
    ${PROJECT_SOURCE_DIR}/src/export/ExcelExporter.cpp
)

add_executable(test_entry_repository
    test_entry_repository.cpp
    ${PROJECT_SOURCE_DIR}/src/core/StatementEntry.h
    ${PROJECT_SOURCE_DIR}/src/core/CalculationService.h
    ${PROJECT_SOURCE_DIR}/src/core/CalculationService.cpp
    ${PROJECT_SOURCE_DIR}/src/data/DatabaseManager.h
    ${PROJECT_SOURCE_DIR}/src/data/DatabaseManager.cpp
    ${PROJECT_SOURCE_DIR}/src/data/EntryRepository.h
    ${PROJECT_SOURCE_DIR}/src/data/EntryRepository.cpp
)

target_include_directories(test_calculation
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(test_calculation
    PRIVATE
        Qt${QT_VERSION_MAJOR}::Core
        Qt${QT_VERSION_MAJOR}::Test
)

target_include_directories(test_entry_table_model
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(test_entry_table_model
    PRIVATE
        Qt${QT_VERSION_MAJOR}::Core
        Qt${QT_VERSION_MAJOR}::Gui
        Qt${QT_VERSION_MAJOR}::Widgets
        Qt${QT_VERSION_MAJOR}::Test
)

target_include_directories(test_exporters
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(test_exporters
    PRIVATE
        Qt${QT_VERSION_MAJOR}::Core
        Qt${QT_VERSION_MAJOR}::Test
)

target_include_directories(test_entry_repository
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(test_entry_repository
    PRIVATE
        Qt${QT_VERSION_MAJOR}::Core
        Qt${QT_VERSION_MAJOR}::Sql
        Qt${QT_VERSION_MAJOR}::Test
)

add_test(NAME calculation COMMAND test_calculation)
add_test(NAME entry_table_model COMMAND test_entry_table_model)
add_test(NAME exporters COMMAND test_exporters)
add_test(NAME entry_repository COMMAND test_entry_repository)

