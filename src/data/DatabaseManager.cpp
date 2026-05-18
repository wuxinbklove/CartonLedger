#include "data/DatabaseManager.h"

#include <QDir>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QVector>
#include <QVariant>

namespace cartonledger {

namespace {

bool tableHasColumn(QSqlDatabase database, const QString &tableName, const QString &columnName, QString *errorMessage)
{
    QSqlQuery pragmaQuery(database);
    if (!pragmaQuery.exec(QStringLiteral("PRAGMA table_info(%1)").arg(tableName))) {
        if (errorMessage != nullptr) {
            *errorMessage = pragmaQuery.lastError().text();
        }
        return false;
    }

    while (pragmaQuery.next()) {
        if (pragmaQuery.value(1).toString() == columnName) {
            return true;
        }
    }

    return false;
}

bool columnHasNotNullConstraint(QSqlDatabase database, const QString &tableName, const QString &columnName, bool *hasNotNull, QString *errorMessage)
{
    QSqlQuery pragmaQuery(database);
    if (!pragmaQuery.exec(QStringLiteral("PRAGMA table_info(%1)").arg(tableName))) {
        if (errorMessage != nullptr) {
            *errorMessage = pragmaQuery.lastError().text();
        }
        return false;
    }

    while (pragmaQuery.next()) {
        if (pragmaQuery.value(1).toString() == columnName) {
            if (hasNotNull != nullptr) {
                *hasNotNull = pragmaQuery.value(3).toBool();
            }
            return true;
        }
    }

    if (hasNotNull != nullptr) {
        *hasNotNull = false;
    }
    return true;
}

bool migrateEntriesDropDeliveryDateNotNull(QSqlDatabase database, QString *errorMessage)
{
    bool deliveryDateNotNull = false;
    if (!columnHasNotNullConstraint(database, QStringLiteral("entries"), QStringLiteral("delivery_date"), &deliveryDateNotNull, errorMessage)) {
        return false;
    }

    if (!deliveryDateNotNull) {
        return true;
    }

    if (!database.transaction()) {
        if (errorMessage != nullptr) {
            *errorMessage = database.lastError().text();
        }
        return false;
    }

    QSqlQuery query(database);
    query.exec(QStringLiteral("DROP TRIGGER IF EXISTS trg_entries_spec_required_insert"));
    query.exec(QStringLiteral("DROP TRIGGER IF EXISTS trg_entries_spec_required_update"));

    if (!query.exec(QStringLiteral(
            "CREATE TABLE entries_v2 ("
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
            "background_color TEXT NOT NULL DEFAULT '',"
            "remark TEXT NOT NULL DEFAULT '',"
            "display_order INTEGER NOT NULL DEFAULT 0"
            ")"))) {
        database.rollback();
        if (errorMessage != nullptr) {
            *errorMessage = query.lastError().text();
        }
        return false;
    }

    if (!query.exec(QStringLiteral(
            "INSERT INTO entries_v2 "
            "SELECT id, sheet_id, delivery_date, order_number, specification, "
            "length_cm, width_cm, height_cm, quantity, formula_type, price_per_sqm, "
            "price_precision, manual_unit_price, manual_unit_price_precision, background_color, remark, display_order "
            "FROM entries"))) {
        database.rollback();
        if (errorMessage != nullptr) {
            *errorMessage = query.lastError().text();
        }
        return false;
    }

    if (!query.exec(QStringLiteral("DROP TABLE entries"))) {
        database.rollback();
        if (errorMessage != nullptr) {
            *errorMessage = query.lastError().text();
        }
        return false;
    }

    if (!query.exec(QStringLiteral("ALTER TABLE entries_v2 RENAME TO entries"))) {
        database.rollback();
        if (errorMessage != nullptr) {
            *errorMessage = query.lastError().text();
        }
        return false;
    }

    if (!database.commit()) {
        if (errorMessage != nullptr) {
            *errorMessage = database.lastError().text();
        }
        return false;
    }

    QSqlQuery indexQuery(database);
    indexQuery.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_entries_delivery_date ON entries(delivery_date)"));
    indexQuery.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_entries_order_number ON entries(order_number)"));

    return true;
}

qint64 ensureDefaultSheet(QSqlDatabase database, QString *errorMessage)
{
    QSqlQuery selectQuery(database);
    if (!selectQuery.exec(QStringLiteral("SELECT id FROM sheets ORDER BY display_order ASC, id ASC LIMIT 1"))) {
        if (errorMessage != nullptr) {
            *errorMessage = selectQuery.lastError().text();
        }
        return -1;
    }

    if (selectQuery.next()) {
        return selectQuery.value(0).toLongLong();
    }

    QSqlQuery insertQuery(database);
    insertQuery.prepare(QStringLiteral("INSERT INTO sheets (name, display_order) VALUES (?, 1)"));
    insertQuery.addBindValue(QStringLiteral("Sheet1"));
    if (!insertQuery.exec()) {
        if (errorMessage != nullptr) {
            *errorMessage = insertQuery.lastError().text();
        }
        return -1;
    }

    return insertQuery.lastInsertId().toLongLong();
}

bool normalizeSheetDisplayOrder(QSqlDatabase database, QString *errorMessage)
{
    QSqlQuery selectQuery(database);
    if (!selectQuery.exec(QStringLiteral("SELECT id FROM sheets ORDER BY display_order ASC, id ASC"))) {
        if (errorMessage != nullptr) {
            *errorMessage = selectQuery.lastError().text();
        }
        return false;
    }

    QVector<qint64> ids;
    while (selectQuery.next()) {
        ids.append(selectQuery.value(0).toLongLong());
    }

    if (ids.isEmpty()) {
        return true;
    }

    if (!database.transaction()) {
        if (errorMessage != nullptr) {
            *errorMessage = database.lastError().text();
        }
        return false;
    }

    QSqlQuery updateQuery(database);
    if (!updateQuery.prepare(QStringLiteral("UPDATE sheets SET display_order = ? WHERE id = ?"))) {
        database.rollback();
        if (errorMessage != nullptr) {
            *errorMessage = updateQuery.lastError().text();
        }
        return false;
    }

    for (int index = 0; index < ids.size(); ++index) {
        updateQuery.bindValue(0, index + 1);
        updateQuery.bindValue(1, ids.at(index));
        if (!updateQuery.exec()) {
            database.rollback();
            if (errorMessage != nullptr) {
                *errorMessage = updateQuery.lastError().text();
            }
            return false;
        }
    }

    if (!database.commit()) {
        if (errorMessage != nullptr) {
            *errorMessage = database.lastError().text();
        }
        return false;
    }

    return true;
}

bool normalizeEntryDisplayOrder(QSqlDatabase database, QString *errorMessage)
{
    QSqlQuery selectQuery(database);
    if (!selectQuery.exec(QStringLiteral("SELECT id, sheet_id FROM entries ORDER BY sheet_id ASC, display_order ASC, id ASC"))) {
        if (errorMessage != nullptr) {
            *errorMessage = selectQuery.lastError().text();
        }
        return false;
    }

    struct EntryOrderRow {
        qint64 id = -1;
        qint64 sheetId = -1;
    };

    QVector<EntryOrderRow> rows;
    while (selectQuery.next()) {
        EntryOrderRow row;
        row.id = selectQuery.value(0).toLongLong();
        row.sheetId = selectQuery.value(1).toLongLong();
        rows.append(row);
    }

    if (rows.isEmpty()) {
        return true;
    }

    if (!database.transaction()) {
        if (errorMessage != nullptr) {
            *errorMessage = database.lastError().text();
        }
        return false;
    }

    QSqlQuery updateQuery(database);
    if (!updateQuery.prepare(QStringLiteral("UPDATE entries SET display_order = ? WHERE id = ?"))) {
        database.rollback();
        if (errorMessage != nullptr) {
            *errorMessage = updateQuery.lastError().text();
        }
        return false;
    }

    qint64 currentSheetId = -1;
    int displayOrder = 0;
    for (const EntryOrderRow &row : rows) {
        if (row.sheetId != currentSheetId) {
            currentSheetId = row.sheetId;
            displayOrder = 0;
        }

        ++displayOrder;
        updateQuery.bindValue(0, displayOrder);
        updateQuery.bindValue(1, row.id);
        if (!updateQuery.exec()) {
            database.rollback();
            if (errorMessage != nullptr) {
                *errorMessage = updateQuery.lastError().text();
            }
            return false;
        }
    }

    if (!database.commit()) {
        if (errorMessage != nullptr) {
            *errorMessage = database.lastError().text();
        }
        return false;
    }

    return true;
}

} // namespace

DatabaseManager::~DatabaseManager()
{
    close();
}

bool DatabaseManager::open(QString *errorMessage)
{
    QSqlDatabase database;
    if (QSqlDatabase::contains(m_connectionName)) {
        database = QSqlDatabase::database(m_connectionName);
    } else {
        database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    }

    const QString path = databasePath();
    QDir dir;
    dir.mkpath(QFileInfo(path).absolutePath());

    database.setDatabaseName(path);
    if (!database.open()) {
        if (errorMessage != nullptr) {
            *errorMessage = database.lastError().text();
        }
        return false;
    }

    return initializeSchema(errorMessage);
}

void DatabaseManager::close()
{
    if (QSqlDatabase::contains(m_connectionName)) {
        {
            QSqlDatabase db = QSqlDatabase::database(m_connectionName);
            if (db.isOpen()) {
                db.close();
            }
        }
        QSqlDatabase::removeDatabase(m_connectionName);
    }
}

QSqlDatabase DatabaseManager::database() const
{
    return QSqlDatabase::database(m_connectionName);
}

QString DatabaseManager::databasePath() const
{
    const QString overridePath = qEnvironmentVariable("CARTON_LEDGER_DATABASE_PATH").trimmed();
    if (!overridePath.isEmpty()) {
        return overridePath;
    }

    const QString appDataLocation = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return appDataLocation + QStringLiteral("/carton-ledger.db");
}

bool DatabaseManager::initializeSchema(QString *errorMessage)
{
    QSqlQuery query(database());

    const QString createSheets = QStringLiteral(
        "CREATE TABLE IF NOT EXISTS sheets ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "name TEXT NOT NULL DEFAULT '',"
        "display_order INTEGER NOT NULL DEFAULT 0"
        ")");

    if (!query.exec(createSheets)) {
        if (errorMessage != nullptr) {
            *errorMessage = query.lastError().text();
        }
        return false;
    }

    if (!query.exec(QStringLiteral("CREATE UNIQUE INDEX IF NOT EXISTS idx_sheets_name ON sheets(name COLLATE NOCASE)"))) {
        if (errorMessage != nullptr) {
            *errorMessage = query.lastError().text();
        }
        return false;
    }

    if (!query.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_sheets_display_order ON sheets(display_order)"))) {
        if (errorMessage != nullptr) {
            *errorMessage = query.lastError().text();
        }
        return false;
    }

    const QString createEntries = QStringLiteral(
        "CREATE TABLE IF NOT EXISTS entries ("
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
        "background_color TEXT NOT NULL DEFAULT '',"
        "remark TEXT NOT NULL DEFAULT '',"
        "display_order INTEGER NOT NULL DEFAULT 0"
        ")");

    if (!query.exec(createEntries)) {
        if (errorMessage != nullptr) {
            *errorMessage = query.lastError().text();
        }
        return false;
    }

    if (!query.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_entries_delivery_date ON entries(delivery_date)"))) {
        if (errorMessage != nullptr) {
            *errorMessage = query.lastError().text();
        }
        return false;
    }

    if (!query.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_entries_order_number ON entries(order_number)"))) {
        if (errorMessage != nullptr) {
            *errorMessage = query.lastError().text();
        }
        return false;
    }

    QString schemaError;

    if (!tableHasColumn(database(), QStringLiteral("entries"), QStringLiteral("specification"), &schemaError)) {
        if (!schemaError.isEmpty()) {
            if (errorMessage != nullptr) {
                *errorMessage = schemaError;
            }
            return false;
        }

        QSqlQuery alterQuery(database());
        if (!alterQuery.exec(QStringLiteral("ALTER TABLE entries ADD COLUMN specification TEXT NOT NULL DEFAULT ''"))) {
            if (errorMessage != nullptr) {
                *errorMessage = alterQuery.lastError().text();
            }
            return false;
        }
    }

    schemaError.clear();
    if (!tableHasColumn(database(), QStringLiteral("entries"), QStringLiteral("price_precision"), &schemaError)) {
        if (!schemaError.isEmpty()) {
            if (errorMessage != nullptr) {
                *errorMessage = schemaError;
            }
            return false;
        }

        QSqlQuery alterQuery(database());
        if (!alterQuery.exec(QStringLiteral("ALTER TABLE entries ADD COLUMN price_precision INTEGER NOT NULL DEFAULT -1"))) {
            if (errorMessage != nullptr) {
                *errorMessage = alterQuery.lastError().text();
            }
            return false;
        }
    }

    schemaError.clear();
    if (!tableHasColumn(database(), QStringLiteral("entries"), QStringLiteral("manual_unit_price"), &schemaError)) {
        if (!schemaError.isEmpty()) {
            if (errorMessage != nullptr) {
                *errorMessage = schemaError;
            }
            return false;
        }

        QSqlQuery alterQuery(database());
        if (!alterQuery.exec(QStringLiteral("ALTER TABLE entries ADD COLUMN manual_unit_price REAL NOT NULL DEFAULT 0"))) {
            if (errorMessage != nullptr) {
                *errorMessage = alterQuery.lastError().text();
            }
            return false;
        }
    }

    schemaError.clear();
    if (!tableHasColumn(database(), QStringLiteral("entries"), QStringLiteral("manual_unit_price_precision"), &schemaError)) {
        if (!schemaError.isEmpty()) {
            if (errorMessage != nullptr) {
                *errorMessage = schemaError;
            }
            return false;
        }

        QSqlQuery alterQuery(database());
        if (!alterQuery.exec(QStringLiteral("ALTER TABLE entries ADD COLUMN manual_unit_price_precision INTEGER NOT NULL DEFAULT -1"))) {
            if (errorMessage != nullptr) {
                *errorMessage = alterQuery.lastError().text();
            }
            return false;
        }
    }

    schemaError.clear();
    if (!tableHasColumn(database(), QStringLiteral("entries"), QStringLiteral("display_order"), &schemaError)) {
        if (!schemaError.isEmpty()) {
            if (errorMessage != nullptr) {
                *errorMessage = schemaError;
            }
            return false;
        }

        QSqlQuery alterQuery(database());
        if (!alterQuery.exec(QStringLiteral("ALTER TABLE entries ADD COLUMN display_order INTEGER NOT NULL DEFAULT 0"))) {
            if (errorMessage != nullptr) {
                *errorMessage = alterQuery.lastError().text();
            }
            return false;
        }
    }

    schemaError.clear();
    if (!tableHasColumn(database(), QStringLiteral("entries"), QStringLiteral("background_color"), &schemaError)) {
        if (!schemaError.isEmpty()) {
            if (errorMessage != nullptr) {
                *errorMessage = schemaError;
            }
            return false;
        }

        QSqlQuery alterQuery(database());
        if (!alterQuery.exec(QStringLiteral("ALTER TABLE entries ADD COLUMN background_color TEXT NOT NULL DEFAULT ''"))) {
            if (errorMessage != nullptr) {
                *errorMessage = alterQuery.lastError().text();
            }
            return false;
        }
    }

    schemaError.clear();
    if (!tableHasColumn(database(), QStringLiteral("entries"), QStringLiteral("remark"), &schemaError)) {
        if (!schemaError.isEmpty()) {
            if (errorMessage != nullptr) {
                *errorMessage = schemaError;
            }
            return false;
        }

        QSqlQuery alterQuery(database());
        if (!alterQuery.exec(QStringLiteral("ALTER TABLE entries ADD COLUMN remark TEXT NOT NULL DEFAULT ''"))) {
            if (errorMessage != nullptr) {
                *errorMessage = alterQuery.lastError().text();
            }
            return false;
        }
    }

    schemaError.clear();
    if (!tableHasColumn(database(), QStringLiteral("entries"), QStringLiteral("sheet_id"), &schemaError)) {
        if (!schemaError.isEmpty()) {
            if (errorMessage != nullptr) {
                *errorMessage = schemaError;
            }
            return false;
        }

        QSqlQuery alterQuery(database());
        if (!alterQuery.exec(QStringLiteral("ALTER TABLE entries ADD COLUMN sheet_id INTEGER NOT NULL DEFAULT 0"))) {
            if (errorMessage != nullptr) {
                *errorMessage = alterQuery.lastError().text();
            }
            return false;
        }
    }

    if (!query.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_entries_display_order ON entries(display_order)"))) {
        if (errorMessage != nullptr) {
            *errorMessage = query.lastError().text();
        }
        return false;
    }

    if (!query.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_entries_sheet_order ON entries(sheet_id, display_order)"))) {
        if (errorMessage != nullptr) {
            *errorMessage = query.lastError().text();
        }
        return false;
    }

    if (!migrateEntriesDropDeliveryDateNotNull(database(), errorMessage)) {
        return false;
    }

    QString sheetError;
    const qint64 defaultSheetId = ensureDefaultSheet(database(), &sheetError);
    if (defaultSheetId <= 0) {
        if (errorMessage != nullptr) {
            *errorMessage = sheetError;
        }
        return false;
    }

    QSqlQuery backfillQuery(database());
    if (!backfillQuery.exec(QStringLiteral(
            "UPDATE entries SET specification = "
            "CASE WHEN height_cm > 0 THEN printf('%g*%g*%g', length_cm, width_cm, height_cm) "
            "ELSE printf('%g*%g', length_cm, width_cm) END "
            "WHERE TRIM(COALESCE(specification, '')) = '' AND length_cm > 0 AND width_cm > 0"))) {
        if (errorMessage != nullptr) {
            *errorMessage = backfillQuery.lastError().text();
        }
        return false;
    }

    if (!backfillQuery.exec(QStringLiteral(
            "UPDATE entries SET price_precision = -1 WHERE price_precision IS NULL"))) {
        if (errorMessage != nullptr) {
            *errorMessage = backfillQuery.lastError().text();
        }
        return false;
    }

    if (!backfillQuery.exec(QStringLiteral(
            "UPDATE entries SET manual_unit_price = 0 WHERE manual_unit_price IS NULL"))) {
        if (errorMessage != nullptr) {
            *errorMessage = backfillQuery.lastError().text();
        }
        return false;
    }

    if (!backfillQuery.exec(QStringLiteral(
            "UPDATE entries SET manual_unit_price_precision = -1 WHERE manual_unit_price_precision IS NULL"))) {
        if (errorMessage != nullptr) {
            *errorMessage = backfillQuery.lastError().text();
        }
        return false;
    }

    if (!backfillQuery.exec(QStringLiteral(
            "UPDATE entries SET background_color = '' WHERE background_color IS NULL"))) {
        if (errorMessage != nullptr) {
            *errorMessage = backfillQuery.lastError().text();
        }
        return false;
    }

    if (!backfillQuery.exec(QStringLiteral(
            "UPDATE entries SET remark = '' WHERE remark IS NULL"))) {
        if (errorMessage != nullptr) {
            *errorMessage = backfillQuery.lastError().text();
        }
        return false;
    }

    if (!backfillQuery.exec(QStringLiteral(
            "UPDATE entries SET delivery_date = '' WHERE delivery_date IS NULL"))) {
        if (errorMessage != nullptr) {
            *errorMessage = backfillQuery.lastError().text();
        }
        return false;
    }

    backfillQuery.prepare(QStringLiteral(
        "UPDATE entries SET sheet_id = ? "
        "WHERE sheet_id IS NULL OR sheet_id = 0 OR NOT EXISTS ("
        "SELECT 1 FROM sheets WHERE sheets.id = entries.sheet_id)"));
    backfillQuery.addBindValue(defaultSheetId);
    if (!backfillQuery.exec()) {
        if (errorMessage != nullptr) {
            *errorMessage = backfillQuery.lastError().text();
        }
        return false;
    }

    if (!normalizeSheetDisplayOrder(database(), errorMessage)) {
        return false;
    }

    if (!normalizeEntryDisplayOrder(database(), errorMessage)) {
        return false;
    }

    if (!backfillQuery.exec(QStringLiteral(
            "CREATE TRIGGER IF NOT EXISTS trg_entries_spec_required_insert "
            "BEFORE INSERT ON entries "
            "FOR EACH ROW WHEN TRIM(COALESCE(NEW.specification, '')) = '' "
            "BEGIN SELECT RAISE(ABORT, 'specification required'); END"))) {
        if (errorMessage != nullptr) {
            *errorMessage = backfillQuery.lastError().text();
        }
        return false;
    }

    if (!backfillQuery.exec(QStringLiteral(
            "CREATE TRIGGER IF NOT EXISTS trg_entries_spec_required_update "
            "BEFORE UPDATE OF specification ON entries "
            "FOR EACH ROW WHEN TRIM(COALESCE(NEW.specification, '')) = '' "
            "BEGIN SELECT RAISE(ABORT, 'specification required'); END"))) {
        if (errorMessage != nullptr) {
            *errorMessage = backfillQuery.lastError().text();
        }
        return false;
    }

    return true;
}

} // namespace cartonledger
