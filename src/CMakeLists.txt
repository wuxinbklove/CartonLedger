add_executable(carton_ledger
    main.cpp
    app/MainWindow.h
    app/MainWindow.cpp
    app/SettingsDialog.h
    app/SettingsDialog.cpp
    core/StatementEntry.h
    core/CalculationService.h
    core/CalculationService.cpp
    core/AppSettings.h
    core/AppSettings.cpp
    data/DatabaseManager.h
    data/DatabaseManager.cpp
    data/EntryRepository.h
    data/EntryRepository.cpp
    export/EntryExportFormatter.h
    export/EntryExportFormatter.cpp
    export/ExcelExporter.h
    export/ExcelExporter.cpp
    ui/PasteColumnResolver.h
    ui/PasteColumnResolver.cpp
    ui/commands/ModelStateCommand.h
    ui/commands/ModelStateCommand.cpp
    ui/models/EntryTableModel.h
    ui/models/EntryTableModel.cpp
    ui/delegates/SpecificationDelegate.h
    ui/delegates/SpecificationDelegate.cpp
    ui/delegates/FormulaTypeDelegate.h
    ui/delegates/FormulaTypeDelegate.cpp
)

set_target_properties(
  carton_ledger
  PROPERTIES
  WIN32_EXECUTABLE TRUE)

target_include_directories(carton_ledger
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(carton_ledger
    PRIVATE
        Qt${QT_VERSION_MAJOR}::Core
        Qt${QT_VERSION_MAJOR}::Gui
        Qt${QT_VERSION_MAJOR}::Widgets
        Qt${QT_VERSION_MAJOR}::Sql
)
