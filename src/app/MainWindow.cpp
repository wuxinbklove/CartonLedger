#include "app/MainWindow.h"

#include "core/CalculationService.h"
#include "export/ExcelExporter.h"
#include "ui/PasteColumnResolver.h"
#include "ui/commands/ModelStateCommand.h"
#include "ui/delegates/FormulaTypeDelegate.h"
#include "ui/delegates/SpecificationDelegate.h"
#include "ui/models/EntryTableModel.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QColor>
#include <QColorDialog>
#include <QDate>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QTabBar>
#include <QTableView>
#include <QToolBar>
#include <QToolButton>
#include <QUndoStack>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <set>

namespace cartonledger {

namespace {

QString ensureFileSuffix(const QString &filePath, const QString &suffix)
{
    if (filePath.isEmpty() || !QFileInfo(filePath).suffix().isEmpty()) {
        return filePath;
    }

    return filePath + QStringLiteral(".") + suffix;
}

QString safeFileBaseName(QString baseName)
{
    baseName = baseName.trimmed();
    if (baseName.isEmpty()) {
        baseName = QStringLiteral("对账单");
    }

    baseName.replace(QRegularExpression(QStringLiteral(R"([\\/:*?"<>|])")), QStringLiteral("_"));
    return baseName;
}

QVector<QStringList> parseClipboardTable(const QString &text)
{
    QVector<QStringList> rows;
    if (text.isEmpty()) {
        return rows;
    }

    const QStringList rawRows = text.split(QRegularExpression(QStringLiteral("\r\n|\n|\r")), Qt::KeepEmptyParts);
    rows.reserve(rawRows.size());
    for (const QString &rawRow : rawRows) {
        rows.append(rawRow.split(QChar('\t'), Qt::KeepEmptyParts));
    }

    while (!rows.isEmpty()) {
        const QStringList &lastRow = rows.constLast();
        const bool allEmpty = std::all_of(lastRow.cbegin(), lastRow.cend(), [](const QString &cell) {
            return cell.trimmed().isEmpty();
        });
        if (!allEmpty) {
            break;
        }

        rows.removeLast();
    }

    return rows;
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setupUi();
    openDatabase();
    reloadSheets();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (maybeSavePendingChanges(QStringLiteral("退出程序"))) {
        event->accept();
        return;
    }

    event->ignore();
}

void MainWindow::setupUi()
{
    setWindowTitle(QStringLiteral("博尔纸箱记账"));
    resize(1180, 760);

    auto *toolBar = addToolBar(QStringLiteral("工具栏"));
    toolBar->setMovable(false);

    auto *addAction = toolBar->addAction(QStringLiteral("新增行"));
    auto *addMultipleAction = toolBar->addAction(QStringLiteral("插入多行"));
    auto *deleteAction = toolBar->addAction(QStringLiteral("删除选中"));
    auto *setRowBackgroundAction = toolBar->addAction(QStringLiteral("设置背景色"));
    auto *clearRowBackgroundAction = toolBar->addAction(QStringLiteral("清除背景色"));
    toolBar->addSeparator();
    m_undoStack = new QUndoStack(this);
    auto *undoAction = m_undoStack->createUndoAction(this, QStringLiteral("撤销"));
    undoAction->setShortcut(QKeySequence::Undo);
    auto *redoAction = m_undoStack->createRedoAction(this, QStringLiteral("重做"));
    redoAction->setShortcut(QKeySequence::Redo);
    toolBar->addAction(undoAction);
    toolBar->addAction(redoAction);
    toolBar->addSeparator();
    m_addSheetAction = toolBar->addAction(QStringLiteral("新建标签页"));
    m_renameSheetAction = toolBar->addAction(QStringLiteral("重命名标签页"));
    m_deleteSheetAction = toolBar->addAction(QStringLiteral("删除标签页"));
    toolBar->addSeparator();
    auto *saveAction = toolBar->addAction(QStringLiteral("保存"));
    auto *reloadAction = toolBar->addAction(QStringLiteral("重新加载"));
    toolBar->addSeparator();
    auto *exportExcelAction = toolBar->addAction(QStringLiteral("导出 Excel"));
    toolBar->addSeparator();
    auto *exportDatabaseAction = toolBar->addAction(QStringLiteral("导出数据库"));
    auto *importDatabaseAction = toolBar->addAction(QStringLiteral("导入数据库"));
    toolBar->addSeparator();
    auto *findAction2 = toolBar->addAction(QStringLiteral("查找"));

    auto *centralWidget = new QWidget(this);
    auto *mainLayout = new QVBoxLayout(centralWidget);

    m_formulaHintLabel = new QLabel(
        QStringLiteral("计算公式提示：\n%1").arg(formulaTypeHintText()),
        centralWidget);
    m_formulaHintLabel->setWordWrap(true);
    m_formulaHintLabel->setStyleSheet(QStringLiteral(
        "color: #6b5a2b;"
        "background-color: #fff7e6;"
        "border: 1px solid #edd9a3;"
        "border-radius: 4px;"
        "padding: 6px 8px;"));
    mainLayout->addWidget(m_formulaHintLabel);

    // 查找栏（默认隐藏）
    m_searchBar = new QWidget(centralWidget);
    m_searchBar->setVisible(false);
    m_searchBar->setStyleSheet(QStringLiteral(
        "background-color: #f0f0f0;"
        "border-bottom: 1px solid #ccc;"));
    auto *searchBarLayout = new QHBoxLayout(m_searchBar);
    searchBarLayout->setContentsMargins(6, 4, 6, 4);
    searchBarLayout->setSpacing(6);
    auto *searchLabel = new QLabel(QStringLiteral("查找："), m_searchBar);
    m_searchEdit = new QLineEdit(m_searchBar);
    m_searchEdit->setPlaceholderText(QStringLiteral("送货日期、订单号、规格…"));
    m_searchEdit->setMinimumWidth(220);
    m_searchEdit->setMaximumWidth(360);
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->installEventFilter(this);
    auto *searchPrevButton = new QPushButton(QStringLiteral("◄ 上一个"), m_searchBar);
    searchPrevButton->setToolTip(QStringLiteral("上一个 (Shift+Enter)"));
    auto *searchNextButton = new QPushButton(QStringLiteral("下一个 ►"), m_searchBar);
    searchNextButton->setToolTip(QStringLiteral("下一个 (Enter)"));
    m_searchCountLabel = new QLabel(m_searchBar);
    m_searchCountLabel->setMinimumWidth(90);
    auto *searchCloseButton = new QPushButton(QStringLiteral("✕"), m_searchBar);
    searchCloseButton->setFlat(true);
    searchCloseButton->setFixedWidth(28);
    searchCloseButton->setToolTip(QStringLiteral("关闭查找 (Esc)"));
    searchBarLayout->addWidget(searchLabel);
    searchBarLayout->addWidget(m_searchEdit);
    searchBarLayout->addWidget(searchPrevButton);
    searchBarLayout->addWidget(searchNextButton);
    searchBarLayout->addWidget(m_searchCountLabel);
    searchBarLayout->addStretch();
    searchBarLayout->addWidget(searchCloseButton);
    mainLayout->addWidget(m_searchBar);

    m_model = new EntryTableModel(this);
    m_model->setUndoStack(m_undoStack);
    m_tableView = new QTableView(centralWidget);
    m_tableView->setModel(m_model);
    m_tableView->setSelectionBehavior(QAbstractItemView::SelectItems);
    m_tableView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tableView->setAlternatingRowColors(true);
    m_tableView->setSortingEnabled(false);
    m_tableView->setContextMenuPolicy(Qt::CustomContextMenu);
    m_specificationDelegate = new SpecificationDelegate(m_tableView);
    m_tableView->setItemDelegateForColumn(EntryTableModel::SpecificationColumn, m_specificationDelegate);
    m_tableView->setItemDelegateForColumn(EntryTableModel::FormulaColumn, new FormulaTypeDelegate(m_tableView));
    m_tableView->horizontalHeader()->setStretchLastSection(true);
    m_tableView->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_tableView->verticalHeader()->setVisible(true);
    m_tableView->verticalHeader()->setDefaultAlignment(Qt::AlignCenter);
    m_tableView->verticalHeader()->setFixedWidth(56);
    m_tableView->setColumnWidth(EntryTableModel::FormulaColumn, 80);
    m_tableView->setColumnWidth(EntryTableModel::DeliveryDateColumn, 110);
    m_tableView->setColumnWidth(EntryTableModel::OrderNumberColumn, 180);
    m_tableView->setColumnWidth(EntryTableModel::SpecificationColumn, 180);
    m_tableView->setColumnWidth(EntryTableModel::QuantityColumn, 90);
    m_tableView->setColumnWidth(EntryTableModel::PricePerSquareMeterColumn, 120);
    m_tableView->setColumnWidth(EntryTableModel::UnitPriceColumn, 110);
    m_tableView->setColumnWidth(EntryTableModel::TotalPriceColumn, 110);

    auto *copyAction = new QAction(m_tableView);
    copyAction->setShortcut(QKeySequence::Copy);
    copyAction->setShortcutContext(Qt::WidgetShortcut);
    m_tableView->addAction(copyAction);

    auto *pasteAction = new QAction(m_tableView);
    pasteAction->setShortcut(QKeySequence::Paste);
    pasteAction->setShortcutContext(Qt::WidgetShortcut);
    m_tableView->addAction(pasteAction);

    mainLayout->addWidget(m_tableView, 1);

    auto *sheetLayout = new QHBoxLayout();
    m_sheetTabBar = new QTabBar(centralWidget);
    m_sheetTabBar->setShape(QTabBar::RoundedSouth);
    m_sheetTabBar->setDocumentMode(true);
    m_sheetTabBar->setDrawBase(false);
    m_sheetTabBar->setExpanding(false);
    m_sheetTabBar->setUsesScrollButtons(true);
    m_sheetTabBar->setElideMode(Qt::ElideRight);
    m_sheetTabBar->setContextMenuPolicy(Qt::CustomContextMenu);
    m_sheetTabBar->setMovable(true);
    m_addSheetButton = new QToolButton(centralWidget);
    m_addSheetButton->setText(QStringLiteral("+"));
    m_addSheetButton->setToolTip(QStringLiteral("新建标签页"));
    m_addSheetButton->setAutoRaise(true);
    sheetLayout->addWidget(m_sheetTabBar, 1);
    sheetLayout->addWidget(m_addSheetButton);
    mainLayout->addLayout(sheetLayout);

    setCentralWidget(centralWidget);

    m_summaryLabel = new QLabel(this);
    statusBar()->addPermanentWidget(m_summaryLabel);
    statusBar()->showMessage(QStringLiteral("准备就绪"));

    connect(addAction, &QAction::triggered, this, &MainWindow::onAddRow);
    connect(addMultipleAction, &QAction::triggered, this, &MainWindow::onAddMultipleRows);
    connect(deleteAction, &QAction::triggered, this, &MainWindow::onDeleteSelectedRows);
    connect(setRowBackgroundAction, &QAction::triggered, this, &MainWindow::onSetCurrentRowBackgroundColor);
    connect(clearRowBackgroundAction, &QAction::triggered, this, &MainWindow::onClearCurrentRowBackgroundColor);
    connect(m_addSheetAction, &QAction::triggered, this, &MainWindow::onAddSheet);
    connect(m_renameSheetAction, &QAction::triggered, this, &MainWindow::onRenameCurrentSheet);
    connect(m_deleteSheetAction, &QAction::triggered, this, &MainWindow::onDeleteCurrentSheet);
    connect(saveAction, &QAction::triggered, this, &MainWindow::onSave);
    connect(reloadAction, &QAction::triggered, this, &MainWindow::onReload);
    connect(exportExcelAction, &QAction::triggered, this, &MainWindow::onExportExcel);
    connect(exportDatabaseAction, &QAction::triggered, this, &MainWindow::onExportDatabase);
    connect(importDatabaseAction, &QAction::triggered, this, &MainWindow::onImportDatabase);
    connect(findAction2, &QAction::triggered, this, &MainWindow::onShowSearchBar);
    auto *findAction = new QAction(this);
    findAction->setShortcut(QKeySequence::Find);
    this->addAction(findAction);
    connect(findAction, &QAction::triggered, this, &MainWindow::onShowSearchBar);
    connect(m_searchEdit, &QLineEdit::textChanged, this, &MainWindow::onSearchTextChanged);
    connect(searchNextButton, &QPushButton::clicked, this, &MainWindow::onSearchNext);
    connect(searchPrevButton, &QPushButton::clicked, this, &MainWindow::onSearchPrev);
    connect(searchCloseButton, &QPushButton::clicked, this, &MainWindow::onHideSearchBar);
    connect(m_addSheetButton, &QToolButton::clicked, this, &MainWindow::onAddSheet);
    connect(m_sheetTabBar, &QTabBar::currentChanged, this, &MainWindow::onCurrentSheetChanged);
    connect(m_sheetTabBar, &QTabBar::tabBarDoubleClicked, this, &MainWindow::onSheetTabDoubleClicked);
    connect(m_sheetTabBar, &QTabBar::customContextMenuRequested, this, &MainWindow::onSheetTabContextMenuRequested);
    connect(m_sheetTabBar, &QTabBar::tabMoved, this, &MainWindow::onSheetTabMoved);
    connect(copyAction, &QAction::triggered, this, &MainWindow::onCopySelection);
    connect(pasteAction, &QAction::triggered, this, &MainWindow::onPasteSelection);
    connect(m_tableView, &QTableView::customContextMenuRequested, this, &MainWindow::onTableContextMenuRequested);
    connect(m_model, &QAbstractItemModel::dataChanged, this, &MainWindow::updateSummary);
    connect(m_model, &QAbstractItemModel::rowsInserted, this, &MainWindow::updateSummary);
    connect(m_model, &QAbstractItemModel::rowsRemoved, this, &MainWindow::updateSummary);
    connect(m_model, &QAbstractItemModel::modelReset, this, &MainWindow::updateSummary);
    connect(m_model, &QAbstractItemModel::modelReset, this, [this]() {
        if (m_searchBar && m_searchBar->isVisible()) {
            onSearchTextChanged(m_searchEdit->text());
        }
    });
    connect(m_model, &QAbstractItemModel::rowsInserted, this, [this](const QModelIndex &, int, int) {
        if (m_searchBar && m_searchBar->isVisible()) {
            onSearchTextChanged(m_searchEdit->text());
        }
    });
    connect(m_model, &QAbstractItemModel::rowsRemoved, this, [this](const QModelIndex &, int, int) {
        if (m_searchBar && m_searchBar->isVisible()) {
            onSearchTextChanged(m_searchEdit->text());
        }
    });

    refreshSheetTabs();
    updateSummary();
}

void MainWindow::openDatabase()
{
    QString errorMessage;
    if (!m_databaseManager.open(&errorMessage)) {
        QMessageBox::critical(this, QStringLiteral("数据库错误"), QStringLiteral("无法打开数据库：%1").arg(errorMessage));
        statusBar()->showMessage(QStringLiteral("数据库打开失败"));
        return;
    }

    m_repository = std::make_unique<EntryRepository>(m_databaseManager.database());
    statusBar()->showMessage(QStringLiteral("数据库已打开：%1").arg(m_databaseManager.databasePath()), 5000);
}

bool MainWindow::reloadSheets(qint64 preferredSheetId, bool forceLoadEntries)
{
    if (!m_repository) {
        return false;
    }

    QString errorMessage;
    QVector<SheetInfo> sheets = m_repository->loadSheets(&errorMessage);
    if (!errorMessage.isEmpty()) {
        QMessageBox::critical(this, QStringLiteral("加载失败"), QStringLiteral("读取标签页失败：%1").arg(errorMessage));
        return false;
    }

    m_sheets = std::move(sheets);
    if (m_sheets.isEmpty()) {
        m_currentSheetId = -1;
        m_totalEntryCount = 0;
        m_model->setEntries({});
        refreshRecentSpecificationTemplates();
        refreshSheetTabs();
        updateSummary();
        return false;
    }

    const auto hasSheet = [this](qint64 sheetId) {
        return std::any_of(m_sheets.cbegin(), m_sheets.cend(), [sheetId](const SheetInfo &sheet) {
            return sheet.id == sheetId;
        });
    };

    const qint64 previousSheetId = m_currentSheetId;
    qint64 targetSheetId = preferredSheetId;
    if (!hasSheet(targetSheetId)) {
        targetSheetId = m_currentSheetId;
    }
    if (!hasSheet(targetSheetId)) {
        targetSheetId = m_settings.lastSheetId();
    }
    if (!hasSheet(targetSheetId)) {
        targetSheetId = m_sheets.constFirst().id;
    }

    m_currentSheetId = targetSheetId;
    m_settings.setLastSheetId(m_currentSheetId);
    refreshSheetTabs();

    if (forceLoadEntries || m_currentSheetId != previousSheetId) {
        if (!loadEntries()) {
            m_currentSheetId = previousSheetId;
            if (m_currentSheetId > 0) {
                m_settings.setLastSheetId(m_currentSheetId);
            }
            refreshSheetTabs();
            if (m_currentSheetId > 0 && m_currentSheetId != targetSheetId) {
                loadEntries();
            }
            return false;
        }
    }

    return true;
}

bool MainWindow::loadEntries()
{
    if (m_undoStack) {
        m_undoStack->clear();
    }

    if (!m_repository || m_currentSheetId <= 0) {
        m_totalEntryCount = 0;
        m_model->setEntries({});
        refreshRecentSpecificationTemplates();
        updateSummary();
        return false;
    }

    QString errorMessage;
    const QVector<StatementEntry> entries = m_repository->loadEntries(m_currentSheetId, &errorMessage);
    if (!errorMessage.isEmpty()) {
        QMessageBox::critical(this, QStringLiteral("加载失败"), QStringLiteral("读取数据失败：%1").arg(errorMessage));
        return false;
    }

    m_model->setEntries(entries);
    m_totalEntryCount = entries.size();
    refreshRecentSpecificationTemplates();
    updateSummary();
    return true;
}

void MainWindow::refreshRecentSpecificationTemplates()
{
    if (!m_repository) {
        m_model->setRecentSpecificationTemplates({});
        return;
    }

    QString errorMessage;
    const QVector<StatementEntry> templates = m_repository->loadRecentSpecificationTemplates(&errorMessage);
    if (!errorMessage.isEmpty()) {
        m_model->setRecentSpecificationTemplates({});
        statusBar()->showMessage(QStringLiteral("读取规格联想数据失败：%1").arg(errorMessage), 5000);
        return;
    }

    m_model->setRecentSpecificationTemplates(templates);
}

void MainWindow::updateSummary()
{
    double totalAmount = 0.0;
    int summaryPrecision = 2;
    for (const StatementEntry &entry : m_model->entries()) {
        totalAmount += CalculationService::calculateTotalPrice(entry);
        summaryPrecision = std::max(summaryPrecision, CalculationService::effectiveAmountPrecision(entry));
    }

    const QString sheetName = currentSheetName();
    const QString visibleSheetName = sheetName.isEmpty() ? QStringLiteral("未选择标签页") : sheetName;
    m_summaryLabel->setText(QStringLiteral("标签页：%1，共 %2 行，当前标签页合计 %3")
        .arg(visibleSheetName)
        .arg(m_totalEntryCount)
        .arg(CalculationService::formatAmount(totalAmount, summaryPrecision)));
}

void MainWindow::refreshSheetTabs()
{
    if (m_sheetTabBar == nullptr) {
        return;
    }

    const QSignalBlocker blocker(m_sheetTabBar);
    while (m_sheetTabBar->count() > 0) {
        m_sheetTabBar->removeTab(0);
    }

    int currentIndex = -1;
    for (int index = 0; index < m_sheets.size(); ++index) {
        const SheetInfo &sheet = m_sheets.at(index);
        const int tabIndex = m_sheetTabBar->addTab(sheet.name);
        m_sheetTabBar->setTabData(tabIndex, sheet.id);
        m_sheetTabBar->setTabToolTip(tabIndex, sheet.name);
        if (sheet.id == m_currentSheetId) {
            currentIndex = tabIndex;
        }
    }

    if (currentIndex >= 0) {
        m_sheetTabBar->setCurrentIndex(currentIndex);
    }

    const bool hasSheet = !m_sheets.isEmpty();
    m_sheetTabBar->setEnabled(hasSheet);
    if (m_addSheetButton != nullptr) {
        m_addSheetButton->setEnabled(m_repository != nullptr);
    }
    if (m_addSheetAction != nullptr) {
        m_addSheetAction->setEnabled(m_repository != nullptr);
    }
    if (m_renameSheetAction != nullptr) {
        m_renameSheetAction->setEnabled(hasSheet);
    }
    if (m_deleteSheetAction != nullptr) {
        m_deleteSheetAction->setEnabled(m_sheets.size() > 1);
    }
}

int MainWindow::currentSheetTabIndex() const
{
    for (int index = 0; index < m_sheets.size(); ++index) {
        if (m_sheets.at(index).id == m_currentSheetId) {
            return index;
        }
    }

    return -1;
}

QString MainWindow::currentSheetName() const
{
    const int index = currentSheetTabIndex();
    if (index < 0) {
        return {};
    }

    return m_sheets.at(index).name;
}

bool MainWindow::sheetNameExists(const QString &name, qint64 ignoredSheetId) const
{
    const QString trimmedName = name.trimmed();
    return std::any_of(m_sheets.cbegin(), m_sheets.cend(), [&](const SheetInfo &sheet) {
        return sheet.id != ignoredSheetId && sheet.name.compare(trimmedName, Qt::CaseInsensitive) == 0;
    });
}

QString MainWindow::nextSheetName() const
{
    int index = 1;
    while (true) {
        const QString candidate = QStringLiteral("Sheet%1").arg(index);
        if (!sheetNameExists(candidate)) {
            return candidate;
        }
        ++index;
    }
}

bool MainWindow::switchToSheet(qint64 sheetId, const QString &actionText)
{
    if (sheetId == m_currentSheetId) {
        return true;
    }

    if (!maybeSavePendingChanges(actionText)) {
        refreshSheetTabs();
        return false;
    }

    const qint64 previousSheetId = m_currentSheetId;
    m_currentSheetId = sheetId;
    m_settings.setLastSheetId(m_currentSheetId);
    refreshSheetTabs();
    if (loadEntries()) {
        statusBar()->showMessage(QStringLiteral("已切换到标签页 %1").arg(currentSheetName()), 3000);
        return true;
    }

    m_currentSheetId = previousSheetId;
    if (m_currentSheetId > 0) {
        m_settings.setLastSheetId(m_currentSheetId);
    }
    refreshSheetTabs();
    if (m_currentSheetId > 0) {
        loadEntries();
    }
    return false;
}

QModelIndex MainWindow::pasteAnchorIndex() const
{
    if (m_tableView == nullptr) {
        return {};
    }

    const QModelIndex currentIndex = m_tableView->currentIndex();
    if (currentIndex.isValid()) {
        return currentIndex;
    }

    if (m_model != nullptr && m_model->rowCount() > 0) {
        return m_model->index(0, EntryTableModel::FormulaColumn);
    }

    return {};
}

int MainWindow::insertionRowForNewEntry() const
{
    if (m_model == nullptr || m_tableView == nullptr) {
        return 0;
    }

    QItemSelectionModel *selectionModel = m_tableView->selectionModel();
    if (selectionModel == nullptr) {
        return m_model->rowCount();
    }

    const QModelIndex currentIndex = selectionModel->currentIndex();
    if (currentIndex.isValid()) {
        return std::min(currentIndex.row() + 1, m_model->rowCount());
    }

    const QModelIndexList selectedRows = selectionModel->selectedRows();
    int maxSelectedRow = -1;
    for (const QModelIndex &index : selectedRows) {
        maxSelectedRow = std::max(maxSelectedRow, index.row());
    }

    if (maxSelectedRow >= 0) {
        return std::min(maxSelectedRow + 1, m_model->rowCount());
    }

    return m_model->rowCount();
}

int MainWindow::currentTableRow() const
{
    if (m_model == nullptr || m_tableView == nullptr) {
        return -1;
    }

    const QModelIndex currentIndex = m_tableView->currentIndex();
    if (!currentIndex.isValid()) {
        return -1;
    }

    const int row = currentIndex.row();
    return row >= 0 && row < m_model->rowCount() ? row : -1;
}

StatementEntry MainWindow::defaultEntryForInsertion() const
{
    StatementEntry entry;
    entry.quantity = 0;
    entry.pricePerSquareMeter = 0.0;
    entry.pricePrecision = 2;
    entry.formulaType = FormulaType::A;
    return entry;
}

void MainWindow::insertNewRows(int count, bool appendToEnd)
{
    if (m_model == nullptr || count <= 0) {
        return;
    }

    const ModelSnapshot before = m_model->captureSnapshot();
    m_model->setSuppressUndo(true);

    const int targetRow = appendToEnd ? m_model->rowCount() : insertionRowForNewEntry();
    for (int offset = 0; offset < count; ++offset) {
        m_model->insertEntry(targetRow + offset, defaultEntryForInsertion());
    }

    m_model->setSuppressUndo(false);
    if (m_undoStack) {
        m_undoStack->push(new ModelStateCommand(m_model, before, m_model->captureSnapshot(),
                                                QStringLiteral("插入 %1 行").arg(count)));
    }

    selectRow(targetRow);
    statusBar()->showMessage(QStringLiteral("已插入 %1 行").arg(count), 3000);
}

void MainWindow::ensureRowsForPaste(int requiredRowCount)
{
    if (m_model == nullptr || requiredRowCount <= m_model->rowCount()) {
        return;
    }

    const int missingRowCount = requiredRowCount - m_model->rowCount();
    for (int index = 0; index < missingRowCount; ++index) {
        m_model->insertEntry(m_model->rowCount(), defaultEntryForInsertion());
    }
}

QString MainWindow::selectedCellsAsTabularText() const
{
    if (m_tableView == nullptr || m_model == nullptr) {
        return {};
    }

    QModelIndexList selectedIndexes;
    if (m_tableView->selectionModel() != nullptr) {
        selectedIndexes = m_tableView->selectionModel()->selectedIndexes();
    }

    if (selectedIndexes.isEmpty()) {
        const QModelIndex currentIndex = m_tableView->currentIndex();
        if (!currentIndex.isValid()) {
            return {};
        }

        selectedIndexes.append(currentIndex);
    }

    QHash<int, QHash<int, QString>> cellTexts;
    std::set<int> rows;
    std::set<int> columns;
    for (const QModelIndex &index : selectedIndexes) {
        rows.insert(index.row());
        columns.insert(index.column());
        cellTexts[index.row()].insert(index.column(), m_model->data(index, Qt::DisplayRole).toString());
    }

    QStringList lines;
    for (const int row : rows) {
        QStringList cells;
        for (const int column : columns) {
            cells.append(cellTexts.value(row).value(column));
        }
        lines.append(cells.join(QChar('\t')));
    }

    return lines.join(QChar('\n'));
}

void MainWindow::selectRow(int row)
{
    if (m_model->rowCount() <= 0 || row < 0 || row >= m_model->rowCount()) {
        return;
    }

    const QModelIndex index = m_model->index(row, EntryTableModel::FormulaColumn);
    m_tableView->scrollTo(index);
    m_tableView->setCurrentIndex(index);
    m_tableView->edit(index);
}

bool MainWindow::validateEntries(QString *errorMessage) const
{
    for (int row = 0; row < m_model->entries().size(); ++row) {
        const StatementEntry &entry = m_model->entries().at(row);
        const QString specification = CalculationService::formatSpecification(entry);
        if (specification.trimmed().isEmpty()) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("第 %1 行的规格不能为空，格式必须是 长*宽 或 长*宽*高").arg(row + 1);
            }
            return false;
        }

        if (!CalculationService::isSpecificationValid(specification)) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("第 %1 行的规格格式无效，请使用 长*宽 或 长*宽*高").arg(row + 1);
            }
            return false;
        }

        if (!CalculationService::isFormulaAllowed(specification, entry.formulaType)) {
            if (errorMessage != nullptr) {
                const QStringList options = CalculationService::allowedFormulaOptions(specification);
                if (options.contains(QStringLiteral("C")) && !options.contains(QStringLiteral("A"))) {
                    *errorMessage = QStringLiteral("第 %1 行规格为 长*宽 时，只能选择计算公式 C 或 手动").arg(row + 1);
                } else {
                    *errorMessage = QStringLiteral("第 %1 行规格为 长*宽*高 时，只能选择计算公式 A、B 或 手动").arg(row + 1);
                }
            }
            return false;
        }
    }

    return true;
}

bool MainWindow::maybeSavePendingChanges(const QString &actionText)
{
    if (!m_model->isDirty()) {
        return true;
    }

    const auto result = QMessageBox::warning(
        this,
        QStringLiteral("存在未保存更改"),
        QStringLiteral("当前有未保存的数据，是否先保存再%1？").arg(actionText),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);

    if (result == QMessageBox::Cancel) {
        return false;
    }

    if (result == QMessageBox::Save) {
        onSave();
        return !m_model->isDirty();
    }

    return true;
}

void MainWindow::onAddRow()
{
    insertNewRows(1, /*appendToEnd=*/true);
}

void MainWindow::onAddMultipleRows()
{
    bool confirmed = false;
    const int count = QInputDialog::getInt(
        this,
        QStringLiteral("插入多行"),
        QStringLiteral("插入行数"),
        5,
        1,
        999,
        1,
        &confirmed);
    if (!confirmed) {
        return;
    }

    insertNewRows(count);
}

void MainWindow::onAddSheet()
{
    if (!m_repository) {
        return;
    }

    if (!maybeSavePendingChanges(QStringLiteral("切换到新标签页"))) {
        refreshSheetTabs();
        return;
    }

    QString errorMessage;
    const SheetInfo sheet = m_repository->createSheet(nextSheetName(), &errorMessage);
    if (sheet.id <= 0) {
        QMessageBox::critical(this, QStringLiteral("新建失败"), QStringLiteral("创建标签页失败：%1").arg(errorMessage));
        return;
    }

    if (reloadSheets(sheet.id, true)) {
        statusBar()->showMessage(QStringLiteral("已新建标签页 %1").arg(sheet.name), 3000);
    }
}

void MainWindow::insertSheetAt(int position)
{
    if (!m_repository) {
        return;
    }

    if (!maybeSavePendingChanges(QStringLiteral("插入新标签页"))) {
        refreshSheetTabs();
        return;
    }

    QString errorMessage;
    const SheetInfo sheet = m_repository->createSheet(nextSheetName(), &errorMessage);
    if (sheet.id <= 0) {
        QMessageBox::critical(this, QStringLiteral("新建失败"), QStringLiteral("创建标签页失败：%1").arg(errorMessage));
        return;
    }

    // Reload to get all sheets including the new one (appended at end)
    if (!reloadSheets(-1, false)) {
        return;
    }

    // Build ordered IDs with the new sheet inserted at the requested position
    const int clampedPos = std::clamp(position, 0, static_cast<int>(m_sheets.size()) - 1);
    QVector<qint64> orderedIds;
    orderedIds.reserve(m_sheets.size());
    bool inserted = false;
    for (int i = 0; i < m_sheets.size(); ++i) {
        if (!inserted && i == clampedPos) {
            orderedIds.append(sheet.id);
            inserted = true;
        }
        if (m_sheets.at(i).id != sheet.id) {
            orderedIds.append(m_sheets.at(i).id);
        }
    }
    if (!inserted) {
        orderedIds.append(sheet.id);
    }

    errorMessage.clear();
    if (!m_repository->reorderSheets(orderedIds, &errorMessage)) {
        QMessageBox::critical(this, QStringLiteral("排序失败"), QStringLiteral("调整标签页顺序失败：%1").arg(errorMessage));
    }

    if (reloadSheets(sheet.id, true)) {
        statusBar()->showMessage(QStringLiteral("已新建标签页 %1").arg(sheet.name), 3000);
    }
}

void MainWindow::onRenameCurrentSheet()
{
    if (!m_repository || m_currentSheetId <= 0) {
        return;
    }

    const QString currentName = currentSheetName();
    bool confirmed = false;
    const QString input = QInputDialog::getText(
        this,
        QStringLiteral("重命名标签页"),
        QStringLiteral("标签页名称"),
        QLineEdit::Normal,
        currentName,
        &confirmed).trimmed();

    if (!confirmed || input.isEmpty() || input == currentName) {
        return;
    }

    if (sheetNameExists(input, m_currentSheetId)) {
        QMessageBox::warning(this, QStringLiteral("重命名失败"), QStringLiteral("标签页名称已存在，请换一个名称。"));
        return;
    }

    QString errorMessage;
    if (!m_repository->renameSheet(m_currentSheetId, input, &errorMessage)) {
        QMessageBox::critical(this, QStringLiteral("重命名失败"), QStringLiteral("重命名标签页失败：%1").arg(errorMessage));
        return;
    }

    for (SheetInfo &sheet : m_sheets) {
        if (sheet.id == m_currentSheetId) {
            sheet.name = input;
            break;
        }
    }

    refreshSheetTabs();
    updateSummary();
    statusBar()->showMessage(QStringLiteral("标签页已重命名为 %1").arg(input), 3000);
}

void MainWindow::onDeleteCurrentSheet()
{
    if (!m_repository || m_currentSheetId <= 0) {
        return;
    }

    if (m_sheets.size() <= 1) {
        QMessageBox::information(this, QStringLiteral("删除标签页"), QStringLiteral("至少保留一个标签页。"));
        return;
    }

    const QString sheetName = currentSheetName();
    QString message = QStringLiteral("删除标签页“%1”后，其中的 %2 行数据将一起移除，且无法撤销。是否继续？")
        .arg(sheetName)
        .arg(m_model->rowCount());
    if (m_model->isDirty()) {
        message += QStringLiteral("\n\n当前标签页还有未保存修改，删除后这些修改也会一并丢弃。");
    }

    const auto result = QMessageBox::warning(
        this,
        QStringLiteral("删除标签页"),
        message,
        QMessageBox::Yes | QMessageBox::Cancel,
        QMessageBox::Cancel);
    if (result != QMessageBox::Yes) {
        return;
    }

    const int currentIndex = currentSheetTabIndex();
    qint64 targetSheetId = -1;
    if (currentIndex > 0) {
        targetSheetId = m_sheets.at(currentIndex - 1).id;
    } else if (currentIndex + 1 < m_sheets.size()) {
        targetSheetId = m_sheets.at(currentIndex + 1).id;
    }

    QString errorMessage;
    if (!m_repository->deleteSheet(m_currentSheetId, &errorMessage)) {
        QMessageBox::critical(this, QStringLiteral("删除失败"), QStringLiteral("删除标签页失败：%1").arg(errorMessage));
        return;
    }

    if (reloadSheets(targetSheetId, true)) {
        statusBar()->showMessage(QStringLiteral("已删除标签页 %1").arg(sheetName), 3000);
    }
}

void MainWindow::onCurrentSheetChanged(int tabIndex)
{
    if (tabIndex < 0 || m_sheetTabBar == nullptr) {
        return;
    }

    const qint64 sheetId = m_sheetTabBar->tabData(tabIndex).toLongLong();
    switchToSheet(sheetId, QStringLiteral("切换标签页"));
}

void MainWindow::onSheetTabDoubleClicked(int tabIndex)
{
    if (tabIndex < 0 || m_sheetTabBar == nullptr) {
        return;
    }

    const qint64 sheetId = m_sheetTabBar->tabData(tabIndex).toLongLong();
    if (sheetId != m_currentSheetId && !switchToSheet(sheetId, QStringLiteral("切换标签页"))) {
        return;
    }

    onRenameCurrentSheet();
}

void MainWindow::onSheetTabMoved(int from, int to)
{
    if (from < 0 || to < 0 || from == to || !m_repository) {
        return;
    }
    if (from >= m_sheets.size() || to >= m_sheets.size()) {
        return;
    }

    // Reorder m_sheets to match the new visual order
    const SheetInfo movedSheet = m_sheets.at(from);
    m_sheets.remove(from);
    m_sheets.insert(to, movedSheet);

    QVector<qint64> orderedIds;
    orderedIds.reserve(m_sheets.size());
    for (const SheetInfo &sheet : std::as_const(m_sheets)) {
        orderedIds.append(sheet.id);
    }

    QString errorMessage;
    if (!m_repository->reorderSheets(orderedIds, &errorMessage)) {
        QMessageBox::critical(this, QStringLiteral("排序失败"), QStringLiteral("调整标签页顺序失败：%1").arg(errorMessage));
        // Revert m_sheets and visual order
        reloadSheets(m_currentSheetId, false);
    }
}

void MainWindow::onSheetTabContextMenuRequested(const QPoint &position)
{
    if (m_sheetTabBar == nullptr) {
        return;
    }

    const int tabIndex = m_sheetTabBar->tabAt(position);
    if (tabIndex < 0) {
        return;
    }

    const qint64 sheetId = m_sheetTabBar->tabData(tabIndex).toLongLong();
    if (sheetId != m_currentSheetId && !switchToSheet(sheetId, QStringLiteral("切换标签页"))) {
        return;
    }

    QMenu menu(this);
    QAction *insertBeforeAction = menu.addAction(QStringLiteral("在此之前插入新标签页"));
    connect(insertBeforeAction, &QAction::triggered, this, [this, tabIndex]() {
        insertSheetAt(tabIndex);
    });
    QAction *insertAfterAction = menu.addAction(QStringLiteral("在此之后插入新标签页"));
    connect(insertAfterAction, &QAction::triggered, this, [this, tabIndex]() {
        insertSheetAt(tabIndex + 1);
    });
    menu.addSeparator();
    menu.addAction(m_renameSheetAction);
    menu.addAction(m_deleteSheetAction);
    menu.exec(m_sheetTabBar->mapToGlobal(position));
}

void MainWindow::onCopySelection()
{
    const QString tabularText = selectedCellsAsTabularText();
    if (tabularText.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("当前没有可复制的内容"), 3000);
        return;
    }

    QApplication::clipboard()->setText(tabularText);
    statusBar()->showMessage(QStringLiteral("已复制选中内容，可直接粘贴到 Excel"), 3000);
}

void MainWindow::onPasteSelection()
{
    if (m_model == nullptr || m_tableView == nullptr) {
        return;
    }

    const QString clipboardText = QApplication::clipboard()->text();
    const QVector<QStringList> rows = parseClipboardTable(clipboardText);
    if (rows.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("剪贴板中没有可粘贴的表格内容"), 3000);
        return;
    }

    const QModelIndex anchorIndex = pasteAnchorIndex();
    const int startRow = anchorIndex.isValid() ? anchorIndex.row() : 0;
    const int startColumn = anchorIndex.isValid() ? anchorIndex.column() : EntryTableModel::FormulaColumn;

    const ModelSnapshot before = m_model->captureSnapshot();
    m_model->setSuppressUndo(true);

    ensureRowsForPaste(startRow + rows.size());

    QStringList errorMessages;
    int extraErrorCount = 0;
    int maxPastedColumns = 0;

    for (int rowOffset = 0; rowOffset < rows.size(); ++rowOffset) {
        const QStringList &cells = rows.at(rowOffset);
        const QVector<int> targetColumns = resolvePasteTargetColumns(startColumn, cells);
        maxPastedColumns = std::max(maxPastedColumns, static_cast<int>(cells.size()));
        const int targetRow = startRow + rowOffset;
        for (int columnOffset = 0; columnOffset < cells.size(); ++columnOffset) {
            if (columnOffset >= targetColumns.size()) {
                break;
            }

            const int targetColumn = targetColumns.at(columnOffset);
            if (targetColumn >= EntryTableModel::ColumnCount) {
                break;
            }

            const QModelIndex targetIndex = m_model->index(targetRow, targetColumn);
            if (!targetIndex.isValid()) {
                continue;
            }

            if (!(m_model->flags(targetIndex) & Qt::ItemIsEditable)) {
                continue;
            }

            const QString cellText = cells.at(columnOffset);
            if (cellText.trimmed().isEmpty()
                && targetColumn != EntryTableModel::DeliveryDateColumn
                && targetColumn != EntryTableModel::OrderNumberColumn
                && targetColumn != EntryTableModel::SpecificationColumn) {
                continue;
            }

            if (m_model->setData(targetIndex, cellText, Qt::EditRole)) {
                continue;
            }

            const QString columnTitle = m_model->headerData(targetColumn, Qt::Horizontal, Qt::DisplayRole).toString();
            if (errorMessages.size() < 5) {
                errorMessages.append(QStringLiteral("第 %1 行“%2”列：%3")
                    .arg(targetRow + 1)
                    .arg(columnTitle)
                    .arg(cellText));
            } else {
                ++extraErrorCount;
            }
        }
    }

    m_model->setSuppressUndo(false);
    if (m_undoStack) {
        m_undoStack->push(new ModelStateCommand(m_model, before, m_model->captureSnapshot(),
                                                QStringLiteral("粘贴 %1 行").arg(rows.size())));
    }

    if (anchorIndex.isValid()) {
        m_tableView->setCurrentIndex(anchorIndex);
        m_tableView->scrollTo(anchorIndex);
    } else if (m_model->rowCount() > 0) {
        const QModelIndex firstIndex = m_model->index(startRow, startColumn);
        m_tableView->setCurrentIndex(firstIndex);
        m_tableView->scrollTo(firstIndex);
    }

    statusBar()->showMessage(QStringLiteral("已粘贴 %1 行 %2 列").arg(rows.size()).arg(maxPastedColumns), 3000);

    if (!errorMessages.isEmpty()) {
        QString detail = errorMessages.join(QChar('\n'));
        if (extraErrorCount > 0) {
            detail += QStringLiteral("\n... 另有 %1 个单元格未能粘贴").arg(extraErrorCount);
        }

        QMessageBox::warning(
            this,
            QStringLiteral("部分内容未粘贴"),
            QStringLiteral("以下单元格格式无法识别，请检查后重试：\n%1").arg(detail));
    }
}

void MainWindow::onDeleteSelectedRows()
{
    QModelIndexList selectedIndexes;
    if (m_tableView->selectionModel() != nullptr) {
        selectedIndexes = m_tableView->selectionModel()->selectedIndexes();
    }

    if (selectedIndexes.isEmpty()) {
        const QModelIndex currentIndex = m_tableView->currentIndex();
        if (currentIndex.isValid()) {
            selectedIndexes.append(currentIndex);
        }
    }

    if (selectedIndexes.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("删除选中"), QStringLiteral("请先选中要删除的单元格或行。"));
        return;
    }

    std::set<int> rowNumbers;
    for (const QModelIndex &index : selectedIndexes) {
        rowNumbers.insert(index.row());
    }

    QVector<int> rowsToDelete;
    rowsToDelete.reserve(static_cast<int>(rowNumbers.size()));
    for (const int row : rowNumbers) {
        rowsToDelete.append(row);
    }

    std::sort(rowsToDelete.begin(), rowsToDelete.end(), std::greater<>());

    const ModelSnapshot before = m_model->captureSnapshot();
    m_model->setSuppressUndo(true);

    for (const int row : rowsToDelete) {
        m_model->removeRow(row);
    }

    m_model->setSuppressUndo(false);
    if (m_undoStack) {
        m_undoStack->push(new ModelStateCommand(m_model, before, m_model->captureSnapshot(),
                                                QStringLiteral("删除 %1 行").arg(rowsToDelete.size())));
    }

    statusBar()->showMessage(QStringLiteral("已删除 %1 行").arg(rowsToDelete.size()), 3000);
}

void MainWindow::onSetCurrentRowBackgroundColor()
{
    const int row = currentTableRow();
    if (row < 0) {
        QMessageBox::information(this, QStringLiteral("设置背景色"), QStringLiteral("请先选中要设置背景色的当前行。"));
        return;
    }

    QColor initialColor(Qt::white);
    const QString currentColorHex = normalizeBackgroundColorHex(m_model->entries().at(row).backgroundColorHex);
    if (!currentColorHex.isEmpty()) {
        const QColor currentColor(currentColorHex);
        if (currentColor.isValid()) {
            initialColor = currentColor;
        }
    }

    const QColor selectedColor = QColorDialog::getColor(
        initialColor,
        this,
        QStringLiteral("设置当前行背景色"));
    if (!selectedColor.isValid()) {
        return;
    }

    if (m_model->setRowBackgroundColor(row, selectedColor)) {
        statusBar()->showMessage(QStringLiteral("已设置第 %1 行背景色").arg(row + 1), 3000);
    } else {
        statusBar()->showMessage(QStringLiteral("第 %1 行背景色未改变").arg(row + 1), 3000);
    }
}

void MainWindow::onClearCurrentRowBackgroundColor()
{
    const int row = currentTableRow();
    if (row < 0) {
        QMessageBox::information(this, QStringLiteral("清除背景色"), QStringLiteral("请先选中要清除背景色的当前行。"));
        return;
    }

    if (m_model->clearRowBackgroundColor(row)) {
        statusBar()->showMessage(QStringLiteral("已清除第 %1 行背景色").arg(row + 1), 3000);
    } else {
        statusBar()->showMessage(QStringLiteral("第 %1 行没有可清除的背景色").arg(row + 1), 3000);
    }
}

void MainWindow::onTableContextMenuRequested(const QPoint &position)
{
    if (m_tableView == nullptr) {
        return;
    }

    const QModelIndex clickedIndex = m_tableView->indexAt(position);
    if (clickedIndex.isValid()) {
        m_tableView->setCurrentIndex(clickedIndex);
    }

    const bool hasCurrentRow = currentTableRow() >= 0;
    QMenu menu(this);
    QAction *setBackgroundAction = menu.addAction(QStringLiteral("设置当前行背景色"));
    QAction *clearBackgroundAction = menu.addAction(QStringLiteral("清除当前行背景色"));
    setBackgroundAction->setEnabled(hasCurrentRow);
    clearBackgroundAction->setEnabled(hasCurrentRow);
    connect(setBackgroundAction, &QAction::triggered, this, &MainWindow::onSetCurrentRowBackgroundColor);
    connect(clearBackgroundAction, &QAction::triggered, this, &MainWindow::onClearCurrentRowBackgroundColor);
    menu.exec(m_tableView->viewport()->mapToGlobal(position));
}

void MainWindow::onSave()
{
    if (!m_repository || m_currentSheetId <= 0) {
        return;
    }

    QString validationMessage;
    if (!validateEntries(&validationMessage)) {
        QMessageBox::warning(this, QStringLiteral("保存失败"), validationMessage);
        return;
    }

    QString errorMessage;
    QMap<int, qint64> insertedIds;
    if (!m_repository->saveChanges(m_currentSheetId, m_model->entries(), m_model->deletedIds(), &errorMessage, &insertedIds)) {
        QMessageBox::critical(this, QStringLiteral("保存失败"), QStringLiteral("保存数据失败：%1").arg(errorMessage));
        return;
    }

    m_model->markAllRowsClean(insertedIds);
    m_totalEntryCount = m_model->rowCount();
    refreshRecentSpecificationTemplates();
    updateSummary();
    statusBar()->showMessage(QStringLiteral("保存成功"), 3000);
}

void MainWindow::onReload()
{
    if (!maybeSavePendingChanges(QStringLiteral("重新加载数据"))) {
        return;
    }

    loadEntries();
    statusBar()->showMessage(QStringLiteral("已重新加载"), 3000);
}

void MainWindow::onExportExcel()
{
    if (m_model->entries().isEmpty()) {
        QMessageBox::information(this, QStringLiteral("导出 Excel"), QStringLiteral("当前没有可导出的数据。"));
        return;
    }

    const QString filePath = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("导出 Excel"),
        safeFileBaseName(currentSheetName()) + QStringLiteral(".xlsx"),
        QStringLiteral("Excel 文件 (*.xlsx)"));
    const QString normalizedFilePath = ensureFileSuffix(filePath, QStringLiteral("xlsx"));
    if (normalizedFilePath.isEmpty()) {
        return;
    }

    QString errorMessage;
    const QVector<StatementEntry> allEntries = m_repository != nullptr
        ? m_repository->loadEntries(m_currentSheetId, &errorMessage)
        : QVector<StatementEntry> {};
    if (!errorMessage.isEmpty()) {
        QMessageBox::critical(this, QStringLiteral("导出失败"), QStringLiteral("读取导出数据失败：%1").arg(errorMessage));
        return;
    }
    if (!ExcelExporter::exportEntries(normalizedFilePath, allEntries, &errorMessage)) {
        QMessageBox::critical(this, QStringLiteral("导出失败"), QStringLiteral("导出 Excel 失败：%1").arg(errorMessage));
        return;
    }

    statusBar()->showMessage(QStringLiteral("Excel 已导出到 %1").arg(normalizedFilePath), 5000);
    QMessageBox::information(this, QStringLiteral("导出成功"), QStringLiteral("Excel 已成功导出到：\n%1").arg(normalizedFilePath));
}

void MainWindow::onExportDatabase()
{
    const QString sourcePath = m_databaseManager.databasePath();
    if (!QFile::exists(sourcePath)) {
        QMessageBox::warning(this, QStringLiteral("导出数据库"), QStringLiteral("数据库文件不存在。"));
        return;
    }

    const QString destPath = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("导出数据库"),
        QStringLiteral("carton-ledger.db"),
        QStringLiteral("SQLite 数据库 (*.db)"));
    if (destPath.isEmpty()) {
        return;
    }

    if (QFile::exists(destPath)) {
        QFile::remove(destPath);
    }

    if (!QFile::copy(sourcePath, destPath)) {
        QMessageBox::critical(this, QStringLiteral("导出失败"), QStringLiteral("无法复制数据库文件，请检查目标路径权限。"));
        return;
    }

    statusBar()->showMessage(QStringLiteral("数据库已导出到 %1").arg(destPath), 5000);
    QMessageBox::information(this, QStringLiteral("导出成功"), QStringLiteral("数据库已成功导出到：\n%1").arg(destPath));
}

void MainWindow::onImportDatabase()
{
    if (!maybeSavePendingChanges(QStringLiteral("导入数据库"))) {
        return;
    }

    const int ret = QMessageBox::warning(
        this,
        QStringLiteral("导入数据库"),
        QStringLiteral("导入将完全替换当前数据库，此操作不可撤销。确定继续？"),
        QMessageBox::Yes | QMessageBox::Cancel,
        QMessageBox::Cancel);
    if (ret != QMessageBox::Yes) {
        return;
    }

    const QString srcPath = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("选择要导入的数据库文件"),
        QString(),
        QStringLiteral("SQLite 数据库 (*.db)"));
    if (srcPath.isEmpty()) {
        return;
    }

    const QString destPath = m_databaseManager.databasePath();
    m_repository.reset();
    m_databaseManager.close();

    if (QFile::exists(destPath)) {
        QFile::remove(destPath);
    }

    if (!QFile::copy(srcPath, destPath)) {
        // Restore connection even if copy failed
        QString errorMessage;
        m_databaseManager.open(&errorMessage);
        if (!errorMessage.isEmpty()) {
            m_repository.reset();
        } else {
            m_repository = std::make_unique<EntryRepository>(m_databaseManager.database());
            reloadSheets();
        }
        QMessageBox::critical(this, QStringLiteral("导入失败"), QStringLiteral("无法复制数据库文件，请检查源文件权限。"));
        return;
    }

    QString errorMessage;
    if (!m_databaseManager.open(&errorMessage)) {
        QMessageBox::critical(this, QStringLiteral("导入失败"), QStringLiteral("无法打开导入的数据库：%1").arg(errorMessage));
        return;
    }

    m_repository = std::make_unique<EntryRepository>(m_databaseManager.database());
    m_undoStack->clear();
    reloadSheets();
    statusBar()->showMessage(QStringLiteral("数据库导入成功"), 5000);
}

bool MainWindow::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == m_searchEdit && event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Escape) {
            onHideSearchBar();
            return true;
        }
        if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
            if (keyEvent->modifiers() & Qt::ShiftModifier) {
                onSearchPrev();
            } else {
                onSearchNext();
            }
            return true;
        }
    }
    return QMainWindow::eventFilter(obj, event);
}

void MainWindow::onShowSearchBar()
{
    m_searchBar->setVisible(true);
    m_searchEdit->setFocus();
    m_searchEdit->selectAll();
    if (!m_searchEdit->text().isEmpty()) {
        onSearchTextChanged(m_searchEdit->text());
    }
}

void MainWindow::onHideSearchBar()
{
    m_searchBar->setVisible(false);
    m_model->clearSearchHighlights();
    m_searchMatchRows.clear();
    m_searchCurrentMatch = -1;
    m_searchCountLabel->clear();
}

void MainWindow::onSearchTextChanged(const QString &text)
{
    m_searchMatchRows.clear();
    m_searchCurrentMatch = -1;

    const QString needle = text.trimmed();
    if (needle.isEmpty()) {
        m_model->clearSearchHighlights();
        m_searchCountLabel->clear();
        return;
    }

    const QVector<StatementEntry> &entries = m_model->entries();
    for (int row = 0; row < entries.size(); ++row) {
        const StatementEntry &e = entries.at(row);
        const bool matched =
            e.deliveryDate.toString(QStringLiteral("yyyy-MM-dd")).contains(needle, Qt::CaseInsensitive)
            || e.orderNumber.contains(needle, Qt::CaseInsensitive)
            || CalculationService::formatSpecification(e).contains(needle, Qt::CaseInsensitive);
        if (matched) {
            m_searchMatchRows.append(row);
        }
    }

    m_model->setSearchHighlightRows(QSet<int>(m_searchMatchRows.begin(), m_searchMatchRows.end()));

    if (!m_searchMatchRows.isEmpty()) {
        m_searchCurrentMatch = 0;
        navigateToSearchMatch(m_searchCurrentMatch);
    }

    updateSearchCountLabel();
}

void MainWindow::onSearchNext()
{
    if (m_searchMatchRows.isEmpty()) {
        return;
    }
    m_searchCurrentMatch = (m_searchCurrentMatch + 1) % m_searchMatchRows.size();
    navigateToSearchMatch(m_searchCurrentMatch);
    updateSearchCountLabel();
}

void MainWindow::onSearchPrev()
{
    if (m_searchMatchRows.isEmpty()) {
        return;
    }
    m_searchCurrentMatch = (m_searchCurrentMatch - 1 + m_searchMatchRows.size()) % m_searchMatchRows.size();
    navigateToSearchMatch(m_searchCurrentMatch);
    updateSearchCountLabel();
}

void MainWindow::navigateToSearchMatch(int matchIndex)
{
    if (matchIndex < 0 || matchIndex >= m_searchMatchRows.size()) {
        return;
    }
    const int row = m_searchMatchRows.at(matchIndex);
    const QModelIndex idx = m_model->index(row, EntryTableModel::OrderNumberColumn);
    m_tableView->scrollTo(idx, QAbstractItemView::EnsureVisible);
    m_tableView->setCurrentIndex(idx);
}

void MainWindow::updateSearchCountLabel()
{
    if (m_searchMatchRows.isEmpty()) {
        if (!m_searchEdit->text().trimmed().isEmpty()) {
            m_searchCountLabel->setText(QStringLiteral("无匹配"));
            m_searchCountLabel->setStyleSheet(QStringLiteral("color: red;"));
        } else {
            m_searchCountLabel->clear();
            m_searchCountLabel->setStyleSheet(QString());
        }
        return;
    }
    m_searchCountLabel->setText(
        QStringLiteral("%1 / %2 个匹配").arg(m_searchCurrentMatch + 1).arg(m_searchMatchRows.size()));
    m_searchCountLabel->setStyleSheet(QString());
}

} // namespace cartonledger
