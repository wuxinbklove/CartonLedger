#pragma once

#include "core/AppSettings.h"
#include "data/DatabaseManager.h"
#include "data/EntryRepository.h"

#include <QMainWindow>
#include <QSet>
#include <QUndoStack>
#include <QVector>

#include <memory>

class QAction;
class QEvent;
class QLabel;
class QLineEdit;
class QModelIndex;
class QPoint;
class QTableView;
class QTabBar;
class QToolButton;

namespace cartonledger {

class EntryTableModel;
class SpecificationDelegate;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    void setupUi();
    void openDatabase();
    bool reloadSheets(qint64 preferredSheetId = -1, bool forceLoadEntries = true);
    bool loadEntries();
    void refreshRecentSpecificationTemplates();
    void updateSummary();
    void refreshSheetTabs();
    int currentSheetTabIndex() const;
    QString currentSheetName() const;
    bool sheetNameExists(const QString &name, qint64 ignoredSheetId = -1) const;
    QString nextSheetName() const;
    bool switchToSheet(qint64 sheetId, const QString &actionText);
    QModelIndex pasteAnchorIndex() const;
    int insertionRowForNewEntry() const;
    StatementEntry defaultEntryForInsertion() const;
    void insertNewRows(int count, bool appendToEnd = false);
    void ensureRowsForPaste(int requiredRowCount);
    QString selectedCellsAsTabularText() const;
    void selectRow(int row);
    bool validateEntries(QString *errorMessage) const;
    bool maybeSavePendingChanges(const QString &actionText);

    void onAddRow();
    void onAddMultipleRows();
    void onAddSheet();
    void insertSheetAt(int position);
    void onRenameCurrentSheet();
    void onDeleteCurrentSheet();
    void onCurrentSheetChanged(int tabIndex);
    void onSheetTabDoubleClicked(int tabIndex);
    void onSheetTabContextMenuRequested(const QPoint &position);
    void onSheetTabMoved(int from, int to);
    void onCopySelection();
    void onPasteSelection();
    void onDeleteSelectedRows();
    void onSave();
    void onReload();
    void onExportExcel();
    void onExportDatabase();
    void onImportDatabase();
    void onShowSearchBar();
    void onHideSearchBar();
    void onSearchTextChanged(const QString &text);
    void onSearchNext();
    void onSearchPrev();
    void navigateToSearchMatch(int matchIndex);
    void updateSearchCountLabel();

    AppSettings m_settings;
    DatabaseManager m_databaseManager;
    std::unique_ptr<EntryRepository> m_repository;
    EntryTableModel *m_model = nullptr;
    SpecificationDelegate *m_specificationDelegate = nullptr;
    QTableView *m_tableView = nullptr;
    QTabBar *m_sheetTabBar = nullptr;
    QToolButton *m_addSheetButton = nullptr;
    QAction *m_addSheetAction = nullptr;
    QAction *m_renameSheetAction = nullptr;
    QAction *m_deleteSheetAction = nullptr;
    QLabel *m_formulaHintLabel = nullptr;
    QLabel *m_summaryLabel = nullptr;
    QWidget *m_searchBar = nullptr;
    QLineEdit *m_searchEdit = nullptr;
    QLabel *m_searchCountLabel = nullptr;
    QVector<int> m_searchMatchRows;
    int m_searchCurrentMatch = -1;
    QVector<SheetInfo> m_sheets;
    qint64 m_currentSheetId = -1;
    int m_totalEntryCount = 0;
    QUndoStack *m_undoStack = nullptr;
};

} // namespace cartonledger