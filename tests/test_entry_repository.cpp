#include <QtTest>

#include "core/CalculationService.h"
#include "data/DatabaseManager.h"
#include "data/EntryRepository.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTemporaryDir>

using namespace cartonledger;

namespace {

StatementEntry makeEntry(const QString &orderNumber, const QDate &deliveryDate)
{
    StatementEntry entry;
    entry.deliveryDate = deliveryDate;
    entry.orderNumber = orderNumber;
    entry.specification = QStringLiteral("100*50");
    entry.quantity = 1;
    entry.pricePerSquareMeter = 2.15;
    entry.pricePrecision = 2;
    entry.formulaType = FormulaType::C;
    return entry;
}

bool execSql(QSqlDatabase database, const QString &sql, QString *errorMessage = nullptr)
{
    QSqlQuery query(database);
    if (query.exec(sql)) {
        return true;
    }

    if (errorMessage != nullptr) {
        *errorMessage = query.lastError().text();
    }
    return false;
}

bool tableHasColumn(QSqlDatabase database, const QString &tableName, const QString &columnName)
{
    QSqlQuery query(database);
    if (!query.exec(QStringLiteral("PRAGMA table_info(%1)").arg(tableName))) {
        return false;
    }

    while (query.next()) {
        if (query.value(1).toString() == columnName) {
            return true;
        }
    }

    return false;
}

QString useTemporaryDatabasePath(QTemporaryDir &dir)
{
    const QString databasePath = dir.filePath(QStringLiteral("carton-ledger.db"));
    qputenv("CARTON_LEDGER_DATABASE_PATH", databasePath.toUtf8());
    return databasePath;
}

} // namespace

class EntryRepositoryTest : public QObject {
    Q_OBJECT

private slots:
    void migratesLegacySchemaAndPreservesManualOrder();
    void migratesCurrentSchemaWithoutBackgroundColor();
    void persistsManualUnitPrice();
    void persistsRemark();
    void persistsRowBackgroundColor();
    void separatesEntriesBySheetAndSupportsSheetManagement();
};

void EntryRepositoryTest::migratesLegacySchemaAndPreservesManualOrder()
{
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("CartonLedgerTests"));
    QCoreApplication::setApplicationName(QStringLiteral("EntryRepositoryMigrationTest"));

    QTemporaryDir databaseDir;
    QVERIFY(databaseDir.isValid());
    const QString databasePath = useTemporaryDatabasePath(databaseDir);
    DatabaseManager databaseManager;
    QVERIFY(QDir().mkpath(QFileInfo(databasePath).absolutePath()));
    QVERIFY(QFile::remove(databasePath) || !QFile::exists(databasePath));

    {
        QSqlDatabase legacyDatabase = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("legacy_setup_connection"));
        legacyDatabase.setDatabaseName(databasePath);
        QVERIFY2(legacyDatabase.open(), qPrintable(legacyDatabase.lastError().text()));

        QString errorMessage;
        QVERIFY2(execSql(
            legacyDatabase,
            QStringLiteral(
                "CREATE TABLE entries ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                "delivery_date TEXT NOT NULL,"
                "order_number TEXT DEFAULT '',"
                "specification TEXT NOT NULL DEFAULT '',"
                "length_cm REAL NOT NULL DEFAULT 0,"
                "width_cm REAL NOT NULL DEFAULT 0,"
                "height_cm REAL NOT NULL DEFAULT 0,"
                "quantity INTEGER NOT NULL DEFAULT 0,"
                "formula_type TEXT NOT NULL DEFAULT 'A',"
                "price_per_sqm REAL NOT NULL DEFAULT 2.15,"
                "price_precision INTEGER NOT NULL DEFAULT -1"
                ")"),
            &errorMessage),
            qPrintable(errorMessage));
        QVERIFY2(execSql(
            legacyDatabase,
            QStringLiteral(
                "INSERT INTO entries (delivery_date, order_number, specification, length_cm, width_cm, height_cm, quantity, formula_type, price_per_sqm, price_precision) VALUES "
                "('2026-04-01', 'LEGACY-1', '100*50', 100, 50, 0, 1, 'C', 2.15, 2),"
                "('2026-04-09', 'LEGACY-2', '100*50', 100, 50, 0, 1, 'C', 2.15, 2)"),
            &errorMessage),
            qPrintable(errorMessage));

        legacyDatabase.close();
    }
    QSqlDatabase::removeDatabase(QStringLiteral("legacy_setup_connection"));

    QString errorMessage;
    QVERIFY2(databaseManager.open(&errorMessage), qPrintable(errorMessage));
    QVERIFY(tableHasColumn(databaseManager.database(), QStringLiteral("entries"), QStringLiteral("background_color")));
    QVERIFY(tableHasColumn(databaseManager.database(), QStringLiteral("entries"), QStringLiteral("remark")));

    EntryRepository repository(databaseManager.database());
    const QVector<SheetInfo> sheets = repository.loadSheets(&errorMessage);
    QVERIFY2(errorMessage.isEmpty(), qPrintable(errorMessage));
    QCOMPARE(sheets.size(), 1);
    QCOMPARE(sheets.constFirst().name, QStringLiteral("Sheet1"));

    const qint64 sheetId = sheets.constFirst().id;
    QVector<StatementEntry> entries = repository.loadEntries(sheetId, &errorMessage);
    QVERIFY2(errorMessage.isEmpty(), qPrintable(errorMessage));
    QCOMPARE(entries.size(), 2);
    QCOMPARE(entries.at(0).orderNumber, QStringLiteral("LEGACY-1"));
    QCOMPARE(entries.at(1).orderNumber, QStringLiteral("LEGACY-2"));

    QSqlQuery clearQuery(databaseManager.database());
    clearQuery.prepare(QStringLiteral("DELETE FROM entries WHERE sheet_id = ?"));
    clearQuery.addBindValue(sheetId);
    QVERIFY2(clearQuery.exec(), qPrintable(clearQuery.lastError().text()));

    const StatementEntry firstEntry = makeEntry(QStringLiteral("ROW-A"), QDate(2026, 4, 5));
    const StatementEntry thirdEntry = makeEntry(QStringLiteral("ROW-C"), QDate(2026, 4, 1));
    QVERIFY2(repository.saveChanges(sheetId, {firstEntry, thirdEntry}, {}, &errorMessage), qPrintable(errorMessage));

    entries = repository.loadEntries(sheetId, &errorMessage);
    QVERIFY2(errorMessage.isEmpty(), qPrintable(errorMessage));
    QCOMPARE(entries.size(), 2);

    StatementEntry middleEntry = makeEntry(QStringLiteral("ROW-B"), QDate(2026, 4, 10));
    QVector<StatementEntry> reorderedEntries = {entries.at(0), middleEntry, entries.at(1)};
    QVERIFY2(repository.saveChanges(sheetId, reorderedEntries, {}, &errorMessage), qPrintable(errorMessage));

    entries = repository.loadEntries(sheetId, &errorMessage);
    QVERIFY2(errorMessage.isEmpty(), qPrintable(errorMessage));
    QCOMPARE(entries.size(), 3);
    QCOMPARE(entries.at(0).orderNumber, QStringLiteral("ROW-A"));
    QCOMPARE(entries.at(1).orderNumber, QStringLiteral("ROW-B"));
    QCOMPARE(entries.at(2).orderNumber, QStringLiteral("ROW-C"));

    QSqlQuery orderQuery(databaseManager.database());
    orderQuery.prepare(QStringLiteral("SELECT order_number, display_order, sheet_id FROM entries WHERE sheet_id = ? ORDER BY display_order ASC, id ASC"));
    orderQuery.addBindValue(sheetId);
    QVERIFY2(orderQuery.exec(), qPrintable(orderQuery.lastError().text()));
    QVERIFY(orderQuery.next());
    QCOMPARE(orderQuery.value(0).toString(), QStringLiteral("ROW-A"));
    QCOMPARE(orderQuery.value(1).toInt(), 1);
    QCOMPARE(orderQuery.value(2).toLongLong(), sheetId);
    QVERIFY(orderQuery.next());
    QCOMPARE(orderQuery.value(0).toString(), QStringLiteral("ROW-B"));
    QCOMPARE(orderQuery.value(1).toInt(), 2);
    QCOMPARE(orderQuery.value(2).toLongLong(), sheetId);
    QVERIFY(orderQuery.next());
    QCOMPARE(orderQuery.value(0).toString(), QStringLiteral("ROW-C"));
    QCOMPARE(orderQuery.value(1).toInt(), 3);
    QCOMPARE(orderQuery.value(2).toLongLong(), sheetId);

    StatementEntry noDateEntry = makeEntry(QStringLiteral(""), QDate());
    noDateEntry.specification = QStringLiteral("80*60");
    noDateEntry.lengthCm = 80.0;
    noDateEntry.widthCm = 60.0;
    noDateEntry.formulaType = FormulaType::C;
    const QVector<qint64> prevIds = [&]() {
        QVector<qint64> ids;
        for (const StatementEntry &e : entries) {
            ids.append(e.id);
        }
        return ids;
    }();
    QVERIFY2(repository.saveChanges(sheetId, {noDateEntry}, prevIds, &errorMessage),
        qPrintable(QStringLiteral("迁移后保存空送货日期/订单号失败: %1").arg(errorMessage)));
    const QVector<StatementEntry> afterMigrationEntries = repository.loadEntries(sheetId, &errorMessage);
    QVERIFY2(errorMessage.isEmpty(), qPrintable(errorMessage));
    QCOMPARE(afterMigrationEntries.size(), 1);
    QVERIFY(!afterMigrationEntries.constFirst().deliveryDate.isValid());
    QVERIFY(afterMigrationEntries.constFirst().orderNumber.isEmpty());
    QVERIFY(afterMigrationEntries.constFirst().remark.isEmpty());
}

void EntryRepositoryTest::migratesCurrentSchemaWithoutBackgroundColor()
{
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("CartonLedgerTests"));
    QCoreApplication::setApplicationName(QStringLiteral("EntryRepositoryCurrentBackgroundMigrationTest"));

    QTemporaryDir databaseDir;
    QVERIFY(databaseDir.isValid());
    const QString databasePath = useTemporaryDatabasePath(databaseDir);
    DatabaseManager databaseManager;
    QVERIFY(QDir().mkpath(QFileInfo(databasePath).absolutePath()));
    QVERIFY(QFile::remove(databasePath) || !QFile::exists(databasePath));

    {
        QSqlDatabase legacyDatabase = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("current_schema_setup_connection"));
        legacyDatabase.setDatabaseName(databasePath);
        QVERIFY2(legacyDatabase.open(), qPrintable(legacyDatabase.lastError().text()));

        QString errorMessage;
        QVERIFY2(execSql(
            legacyDatabase,
            QStringLiteral(
                "CREATE TABLE sheets ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                "name TEXT NOT NULL DEFAULT '',"
                "display_order INTEGER NOT NULL DEFAULT 0"
                ")"),
            &errorMessage),
            qPrintable(errorMessage));
        QVERIFY2(execSql(legacyDatabase, QStringLiteral("INSERT INTO sheets (id, name, display_order) VALUES (1, 'Sheet1', 1)"), &errorMessage),
            qPrintable(errorMessage));
        QVERIFY2(execSql(
            legacyDatabase,
            QStringLiteral(
                "CREATE TABLE entries ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                "sheet_id INTEGER NOT NULL DEFAULT 0,"
                "delivery_date TEXT DEFAULT '',"
                "order_number TEXT DEFAULT '',"
                "specification TEXT NOT NULL DEFAULT '',"
                "length_cm REAL NOT NULL DEFAULT 0,"
                "width_cm REAL NOT NULL DEFAULT 0,"
                "height_cm REAL NOT NULL DEFAULT 0,"
                "quantity INTEGER NOT NULL DEFAULT 0,"
                "formula_type TEXT NOT NULL DEFAULT 'A',"
                "price_per_sqm REAL NOT NULL DEFAULT 2.15,"
                "price_precision INTEGER NOT NULL DEFAULT -1,"
                "manual_unit_price REAL NOT NULL DEFAULT 0,"
                "manual_unit_price_precision INTEGER NOT NULL DEFAULT -1,"
                "display_order INTEGER NOT NULL DEFAULT 0"
                ")"),
            &errorMessage),
            qPrintable(errorMessage));
        QVERIFY2(execSql(
            legacyDatabase,
            QStringLiteral(
                "INSERT INTO entries (sheet_id, delivery_date, order_number, specification, length_cm, width_cm, height_cm, quantity, formula_type, price_per_sqm, price_precision, manual_unit_price, manual_unit_price_precision, display_order) "
                "VALUES (1, '2026-04-01', 'OLD-BG', '100*50', 100, 50, 0, 2, 'C', 2.15, 2, 0, -1, 1)"),
            &errorMessage),
            qPrintable(errorMessage));

        legacyDatabase.close();
    }
    QSqlDatabase::removeDatabase(QStringLiteral("current_schema_setup_connection"));

    QString errorMessage;
    QVERIFY2(databaseManager.open(&errorMessage), qPrintable(errorMessage));
    QVERIFY(tableHasColumn(databaseManager.database(), QStringLiteral("entries"), QStringLiteral("background_color")));
    QVERIFY(tableHasColumn(databaseManager.database(), QStringLiteral("entries"), QStringLiteral("remark")));

    EntryRepository repository(databaseManager.database());
    const QVector<SheetInfo> sheets = repository.loadSheets(&errorMessage);
    QVERIFY2(errorMessage.isEmpty(), qPrintable(errorMessage));
    QCOMPARE(sheets.size(), 1);

    QVector<StatementEntry> entries = repository.loadEntries(sheets.constFirst().id, &errorMessage);
    QVERIFY2(errorMessage.isEmpty(), qPrintable(errorMessage));
    QCOMPARE(entries.size(), 1);
    QCOMPARE(entries.constFirst().orderNumber, QStringLiteral("OLD-BG"));
    QVERIFY(entries.constFirst().backgroundColorHex.isEmpty());
    QVERIFY(entries.constFirst().remark.isEmpty());
}

void EntryRepositoryTest::persistsManualUnitPrice()
{
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("CartonLedgerTests"));
    QCoreApplication::setApplicationName(QStringLiteral("EntryRepositoryManualPriceTest"));

    QTemporaryDir databaseDir;
    QVERIFY(databaseDir.isValid());
    const QString databasePath = useTemporaryDatabasePath(databaseDir);
    DatabaseManager databaseManager;
    QVERIFY(QDir().mkpath(QFileInfo(databasePath).absolutePath()));
    QVERIFY(QFile::remove(databasePath) || !QFile::exists(databasePath));

    QString errorMessage;
    QVERIFY2(databaseManager.open(&errorMessage), qPrintable(errorMessage));

    EntryRepository repository(databaseManager.database());
    const QVector<SheetInfo> sheets = repository.loadSheets(&errorMessage);
    QVERIFY2(errorMessage.isEmpty(), qPrintable(errorMessage));
    QCOMPARE(sheets.size(), 1);

    StatementEntry manualEntry = makeEntry(QStringLiteral("ROW-MANUAL"), QDate(2026, 4, 20));
    manualEntry.quantity = 3;
    manualEntry.formulaType = FormulaType::Manual;
    manualEntry.manualUnitPrice = 8.75;
    manualEntry.manualUnitPricePrecision = 2;

    QVERIFY2(repository.saveChanges(sheets.constFirst().id, {manualEntry}, {}, &errorMessage), qPrintable(errorMessage));

    const QVector<StatementEntry> entries = repository.loadEntries(sheets.constFirst().id, &errorMessage);
    QVERIFY2(errorMessage.isEmpty(), qPrintable(errorMessage));
    QCOMPARE(entries.size(), 1);
    QCOMPARE(entries.constFirst().formulaType, FormulaType::Manual);
    QCOMPARE(entries.constFirst().manualUnitPrice, 8.75);
    QCOMPARE(entries.constFirst().manualUnitPricePrecision, 2);
    QCOMPARE(CalculationService::calculateTotalPrice(entries.constFirst()), 26.25);

    QSqlQuery query(databaseManager.database());
    query.prepare(QStringLiteral("SELECT formula_type, manual_unit_price, manual_unit_price_precision FROM entries WHERE sheet_id = ?"));
    query.addBindValue(sheets.constFirst().id);
    QVERIFY2(query.exec(), qPrintable(query.lastError().text()));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toString(), QStringLiteral("手动"));
    QCOMPARE(query.value(1).toDouble(), 8.75);
    QCOMPARE(query.value(2).toInt(), 2);
}

void EntryRepositoryTest::persistsRemark()
{
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("CartonLedgerTests"));
    QCoreApplication::setApplicationName(QStringLiteral("EntryRepositoryRemarkTest"));

    QTemporaryDir databaseDir;
    QVERIFY(databaseDir.isValid());
    const QString databasePath = useTemporaryDatabasePath(databaseDir);
    DatabaseManager databaseManager;
    QVERIFY(QDir().mkpath(QFileInfo(databasePath).absolutePath()));
    QVERIFY(QFile::remove(databasePath) || !QFile::exists(databasePath));

    QString errorMessage;
    QVERIFY2(databaseManager.open(&errorMessage), qPrintable(errorMessage));

    EntryRepository repository(databaseManager.database());
    const QVector<SheetInfo> sheets = repository.loadSheets(&errorMessage);
    QVERIFY2(errorMessage.isEmpty(), qPrintable(errorMessage));
    QCOMPARE(sheets.size(), 1);

    StatementEntry remarkEntry = makeEntry(QStringLiteral("ROW-REMARK"), QDate(2026, 4, 22));
    remarkEntry.remark = QStringLiteral("  客户要求加急；送货前电话确认\n第二行备注  ");
    QVERIFY2(repository.saveChanges(sheets.constFirst().id, {remarkEntry}, {}, &errorMessage), qPrintable(errorMessage));

    QVector<StatementEntry> entries = repository.loadEntries(sheets.constFirst().id, &errorMessage);
    QVERIFY2(errorMessage.isEmpty(), qPrintable(errorMessage));
    QCOMPARE(entries.size(), 1);
    QCOMPARE(entries.constFirst().remark, remarkEntry.remark);

    entries.first().remark = QString();
    QVERIFY2(repository.saveChanges(sheets.constFirst().id, entries, {}, &errorMessage), qPrintable(errorMessage));

    const QVector<StatementEntry> clearedEntries = repository.loadEntries(sheets.constFirst().id, &errorMessage);
    QVERIFY2(errorMessage.isEmpty(), qPrintable(errorMessage));
    QCOMPARE(clearedEntries.size(), 1);
    QVERIFY(clearedEntries.constFirst().remark.isEmpty());

    QSqlQuery query(databaseManager.database());
    query.prepare(QStringLiteral("SELECT remark FROM entries WHERE sheet_id = ?"));
    query.addBindValue(sheets.constFirst().id);
    QVERIFY2(query.exec(), qPrintable(query.lastError().text()));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toString(), QStringLiteral(""));
}

void EntryRepositoryTest::persistsRowBackgroundColor()
{
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("CartonLedgerTests"));
    QCoreApplication::setApplicationName(QStringLiteral("EntryRepositoryBackgroundColorTest"));

    QTemporaryDir databaseDir;
    QVERIFY(databaseDir.isValid());
    const QString databasePath = useTemporaryDatabasePath(databaseDir);
    DatabaseManager databaseManager;
    QVERIFY(QDir().mkpath(QFileInfo(databasePath).absolutePath()));
    QVERIFY(QFile::remove(databasePath) || !QFile::exists(databasePath));

    QString errorMessage;
    QVERIFY2(databaseManager.open(&errorMessage), qPrintable(errorMessage));

    EntryRepository repository(databaseManager.database());
    const QVector<SheetInfo> sheets = repository.loadSheets(&errorMessage);
    QVERIFY2(errorMessage.isEmpty(), qPrintable(errorMessage));
    QCOMPARE(sheets.size(), 1);

    StatementEntry coloredEntry = makeEntry(QStringLiteral("ROW-COLOR"), QDate(2026, 4, 21));
    coloredEntry.backgroundColorHex = QStringLiteral("#cce5ff");
    QVERIFY2(repository.saveChanges(sheets.constFirst().id, {coloredEntry}, {}, &errorMessage), qPrintable(errorMessage));

    QVector<StatementEntry> entries = repository.loadEntries(sheets.constFirst().id, &errorMessage);
    QVERIFY2(errorMessage.isEmpty(), qPrintable(errorMessage));
    QCOMPARE(entries.size(), 1);
    QCOMPARE(entries.constFirst().backgroundColorHex, QStringLiteral("#CCE5FF"));

    entries.first().backgroundColorHex = QStringLiteral("");
    QVERIFY2(repository.saveChanges(sheets.constFirst().id, entries, {}, &errorMessage), qPrintable(errorMessage));

    const QVector<StatementEntry> clearedEntries = repository.loadEntries(sheets.constFirst().id, &errorMessage);
    QVERIFY2(errorMessage.isEmpty(), qPrintable(errorMessage));
    QCOMPARE(clearedEntries.size(), 1);
    QVERIFY(clearedEntries.constFirst().backgroundColorHex.isEmpty());
}

void EntryRepositoryTest::separatesEntriesBySheetAndSupportsSheetManagement()
{
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("CartonLedgerTests"));
    QCoreApplication::setApplicationName(QStringLiteral("EntryRepositorySheetTest"));

    QTemporaryDir databaseDir;
    QVERIFY(databaseDir.isValid());
    const QString databasePath = useTemporaryDatabasePath(databaseDir);
    DatabaseManager databaseManager;
    QVERIFY(QDir().mkpath(QFileInfo(databasePath).absolutePath()));
    QVERIFY(QFile::remove(databasePath) || !QFile::exists(databasePath));

    QString errorMessage;
    QVERIFY2(databaseManager.open(&errorMessage), qPrintable(errorMessage));

    EntryRepository repository(databaseManager.database());
    QVector<SheetInfo> sheets = repository.loadSheets(&errorMessage);
    QVERIFY2(errorMessage.isEmpty(), qPrintable(errorMessage));
    QCOMPARE(sheets.size(), 1);

    const qint64 firstSheetId = sheets.constFirst().id;
    SheetInfo secondSheet = repository.createSheet(QStringLiteral("Sheet2"), &errorMessage);
    QVERIFY2(secondSheet.id > 0, qPrintable(errorMessage));
    QVERIFY2(repository.renameSheet(secondSheet.id, QStringLiteral("客户B"), &errorMessage), qPrintable(errorMessage));

    StatementEntry firstEntry = makeEntry(QStringLiteral("ROW-EMPTY-DATE"), QDate());
    StatementEntry secondEntry = makeEntry(QStringLiteral("ROW-WITH-DATE"), QDate(2026, 4, 12));
    StatementEntry thirdEntry = makeEntry(QStringLiteral("ROW-SHEET2"), QDate(2026, 4, 18));

    QVERIFY2(repository.saveChanges(firstSheetId, {firstEntry, secondEntry}, {}, &errorMessage), qPrintable(errorMessage));
    QVERIFY2(repository.saveChanges(secondSheet.id, {thirdEntry}, {}, &errorMessage), qPrintable(errorMessage));

    QCOMPARE(repository.countEntries(firstSheetId, &errorMessage), 2);
    QVERIFY2(errorMessage.isEmpty(), qPrintable(errorMessage));
    QCOMPARE(repository.countEntries(secondSheet.id, &errorMessage), 1);
    QVERIFY2(errorMessage.isEmpty(), qPrintable(errorMessage));

    QVector<StatementEntry> firstSheetEntries = repository.loadEntries(firstSheetId, &errorMessage);
    QVERIFY2(errorMessage.isEmpty(), qPrintable(errorMessage));
    QCOMPARE(firstSheetEntries.size(), 2);
    QVERIFY(!firstSheetEntries.at(0).deliveryDate.isValid());
    QVERIFY(firstSheetEntries.at(1).deliveryDate.isValid());

    const QVector<StatementEntry> secondSheetEntries = repository.loadEntries(secondSheet.id, &errorMessage);
    QVERIFY2(errorMessage.isEmpty(), qPrintable(errorMessage));
    QCOMPARE(secondSheetEntries.size(), 1);
    QCOMPARE(secondSheetEntries.constFirst().orderNumber, QStringLiteral("ROW-SHEET2"));

    sheets = repository.loadSheets(&errorMessage);
    QVERIFY2(errorMessage.isEmpty(), qPrintable(errorMessage));
    QCOMPARE(sheets.size(), 2);
    QCOMPARE(sheets.at(1).name, QStringLiteral("客户B"));

    QVERIFY2(repository.deleteSheet(secondSheet.id, &errorMessage), qPrintable(errorMessage));
    sheets = repository.loadSheets(&errorMessage);
    QVERIFY2(errorMessage.isEmpty(), qPrintable(errorMessage));
    QCOMPARE(sheets.size(), 1);
    QCOMPARE(sheets.constFirst().id, firstSheetId);

    QVERIFY(!repository.deleteSheet(firstSheetId, &errorMessage));
    QCOMPARE(errorMessage, QStringLiteral("至少保留一个标签页"));
}

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    EntryRepositoryTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_entry_repository.moc"
