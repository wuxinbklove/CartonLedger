#include "data/EntryRepository.h"

#include "core/CalculationService.h"

#include <algorithm>
#include <QSqlError>
#include <QSqlQuery>
#include <QSet>
#include <QStringList>
#include <QVariant>

namespace cartonledger {

namespace {

QString serializeDeliveryDate(const QDate &deliveryDate)
{
    return deliveryDate.isValid() ? deliveryDate.toString(Qt::ISODate) : QStringLiteral("");
}

bool assignError(QString *errorMessage, const QString &message)
{
    if (errorMessage != nullptr) {
        *errorMessage = message;
    }
    return false;
}

bool isValidSheetId(qint64 sheetId)
{
    return sheetId > 0;
}

int countSheets(QSqlDatabase database, QString *errorMessage)
{
    QSqlQuery query(database);
    query.prepare(QStringLiteral("SELECT COUNT(*) FROM sheets"));
    if (!query.exec()) {
        assignError(errorMessage, query.lastError().text());
        return 0;
    }

    if (!query.next()) {
        return 0;
    }

    return query.value(0).toInt();
}

int nextSheetDisplayOrder(QSqlDatabase database, QString *errorMessage)
{
    QSqlQuery query(database);
    query.prepare(QStringLiteral("SELECT COALESCE(MAX(display_order), 0) + 1 FROM sheets"));
    if (!query.exec()) {
        assignError(errorMessage, query.lastError().text());
        return 0;
    }

    if (!query.next()) {
        return 1;
    }

    return std::max(1, query.value(0).toInt());
}

bool loadSheetDisplayOrder(QSqlDatabase database, qint64 sheetId, int *displayOrder, QString *errorMessage)
{
    QSqlQuery query(database);
    query.prepare(QStringLiteral("SELECT display_order FROM sheets WHERE id = ?"));
    query.addBindValue(sheetId);
    if (!query.exec()) {
        return assignError(errorMessage, query.lastError().text());
    }

    if (!query.next()) {
        return assignError(errorMessage, QStringLiteral("标签页不存在"));
    }

    if (displayOrder != nullptr) {
        *displayOrder = query.value(0).toInt();
    }
    return true;
}

QVector<SheetInfo> loadOrderedSheets(QSqlDatabase database, QString *errorMessage)
{
    QSqlQuery query(database);
    query.prepare(QStringLiteral("SELECT id, name, display_order FROM sheets ORDER BY display_order ASC, id ASC"));
    if (!query.exec()) {
        assignError(errorMessage, query.lastError().text());
        return {};
    }

    QVector<SheetInfo> sheets;
    while (query.next()) {
        SheetInfo sheet;
        sheet.id = query.value(0).toLongLong();
        sheet.name = query.value(1).toString();
        sheet.displayOrder = query.value(2).toInt();
        sheets.append(sheet);
    }

    return sheets;
}

bool sheetExists(QSqlDatabase database, qint64 sheetId, QString *errorMessage)
{
    QSqlQuery query(database);
    query.prepare(QStringLiteral("SELECT 1 FROM sheets WHERE id = ?"));
    query.addBindValue(sheetId);
    if (!query.exec()) {
        return assignError(errorMessage, query.lastError().text());
    }

    return query.next();
}

} // namespace

EntryRepository::EntryRepository(QSqlDatabase database)
    : m_database(std::move(database))
{
}

QVector<SheetInfo> EntryRepository::loadSheets(QString *errorMessage) const
{
    return loadOrderedSheets(m_database, errorMessage);
}

SheetInfo EntryRepository::createSheet(const QString &name, QString *errorMessage)
{
    const QString trimmedName = name.trimmed();
    if (trimmedName.isEmpty()) {
        assignError(errorMessage, QStringLiteral("标签页名称不能为空"));
        return {};
    }

    QString orderError;
    const int displayOrder = nextSheetDisplayOrder(m_database, &orderError);
    if (!orderError.isEmpty()) {
        assignError(errorMessage, orderError);
        return {};
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("INSERT INTO sheets (name, display_order) VALUES (?, ?)"));
    query.addBindValue(trimmedName);
    query.addBindValue(displayOrder);
    if (!query.exec()) {
        assignError(errorMessage, query.lastError().text());
        return {};
    }

    SheetInfo sheet;
    sheet.id = query.lastInsertId().toLongLong();
    sheet.name = trimmedName;
    sheet.displayOrder = displayOrder;
    return sheet;
}

bool EntryRepository::renameSheet(qint64 sheetId, const QString &name, QString *errorMessage)
{
    if (!isValidSheetId(sheetId)) {
        return assignError(errorMessage, QStringLiteral("标签页不存在"));
    }

    const QString trimmedName = name.trimmed();
    if (trimmedName.isEmpty()) {
        return assignError(errorMessage, QStringLiteral("标签页名称不能为空"));
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("UPDATE sheets SET name = ? WHERE id = ?"));
    query.addBindValue(trimmedName);
    query.addBindValue(sheetId);
    if (!query.exec()) {
        return assignError(errorMessage, query.lastError().text());
    }

    if (query.numRowsAffected() == 0) {
        return assignError(errorMessage, QStringLiteral("标签页不存在"));
    }

    return true;
}

bool EntryRepository::deleteSheet(qint64 sheetId, QString *errorMessage)
{
    if (!isValidSheetId(sheetId)) {
        return assignError(errorMessage, QStringLiteral("标签页不存在"));
    }

    QString countError;
    const int sheetCount = countSheets(m_database, &countError);
    if (!countError.isEmpty()) {
        return assignError(errorMessage, countError);
    }
    if (sheetCount <= 1) {
        return assignError(errorMessage, QStringLiteral("至少保留一个标签页"));
    }

    int displayOrder = 0;
    if (!loadSheetDisplayOrder(m_database, sheetId, &displayOrder, errorMessage)) {
        return false;
    }

    if (!m_database.transaction()) {
        return assignError(errorMessage, m_database.lastError().text());
    }

    QSqlQuery deleteEntriesQuery(m_database);
    deleteEntriesQuery.prepare(QStringLiteral("DELETE FROM entries WHERE sheet_id = ?"));
    deleteEntriesQuery.addBindValue(sheetId);
    if (!deleteEntriesQuery.exec()) {
        m_database.rollback();
        return assignError(errorMessage, deleteEntriesQuery.lastError().text());
    }

    QSqlQuery deleteSheetQuery(m_database);
    deleteSheetQuery.prepare(QStringLiteral("DELETE FROM sheets WHERE id = ?"));
    deleteSheetQuery.addBindValue(sheetId);
    if (!deleteSheetQuery.exec()) {
        m_database.rollback();
        return assignError(errorMessage, deleteSheetQuery.lastError().text());
    }

    QSqlQuery reorderQuery(m_database);
    reorderQuery.prepare(QStringLiteral("UPDATE sheets SET display_order = display_order - 1 WHERE display_order > ?"));
    reorderQuery.addBindValue(displayOrder);
    if (!reorderQuery.exec()) {
        m_database.rollback();
        return assignError(errorMessage, reorderQuery.lastError().text());
    }

    if (!m_database.commit()) {
        return assignError(errorMessage, m_database.lastError().text());
    }

    return true;
}

bool EntryRepository::reorderSheets(const QVector<qint64> &orderedIds, QString *errorMessage)
{
    if (orderedIds.isEmpty()) {
        return true;
    }

    if (!m_database.transaction()) {
        return assignError(errorMessage, m_database.lastError().text());
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("UPDATE sheets SET display_order = ? WHERE id = ?"));
    for (int i = 0; i < orderedIds.size(); ++i) {
        query.bindValue(0, i + 1);
        query.bindValue(1, orderedIds.at(i));
        if (!query.exec()) {
            m_database.rollback();
            return assignError(errorMessage, query.lastError().text());
        }
    }

    if (!m_database.commit()) {
        return assignError(errorMessage, m_database.lastError().text());
    }

    return true;
}

QVector<StatementEntry> EntryRepository::loadEntries(qint64 sheetId, QString *errorMessage) const
{
    if (!isValidSheetId(sheetId)) {
        assignError(errorMessage, QStringLiteral("标签页不存在"));
        return {};
    }

    const QString sql = QStringLiteral(
        "SELECT id, delivery_date, order_number, specification, length_cm, width_cm, height_cm, quantity, formula_type, price_per_sqm, price_precision, manual_unit_price, manual_unit_price_precision, background_color "
        "FROM entries WHERE sheet_id = ? ORDER BY display_order ASC, id ASC");

    QSqlQuery query(m_database);
    query.prepare(sql);
    query.addBindValue(sheetId);

    if (!query.exec()) {
        assignError(errorMessage, query.lastError().text());
        return {};
    }

    QVector<StatementEntry> entries;
    while (query.next()) {
        StatementEntry entry;
        entry.id = query.value(0).toLongLong();
        entry.deliveryDate = QDate::fromString(query.value(1).toString(), Qt::ISODate);
        entry.orderNumber = query.value(2).toString();
        entry.specification = CalculationService::normalizeSpecification(query.value(3).toString());
        entry.lengthCm = query.value(4).toDouble();
        entry.widthCm = query.value(5).toDouble();
        entry.heightCm = query.value(6).toDouble();
        entry.quantity = query.value(7).toInt();
        entry.formulaType = formulaTypeFromString(query.value(8).toString()).value_or(FormulaType::A);
        entry.pricePerSquareMeter = query.value(9).toDouble();
        entry.pricePrecision = query.value(10).toInt();
        entry.manualUnitPrice = query.value(11).toDouble();
        entry.manualUnitPricePrecision = query.value(12).toInt();
        entry.backgroundColorHex = normalizeBackgroundColorHex(query.value(13).toString());
        if (entry.specification.isEmpty()) {
            entry.specification = CalculationService::specificationFromDimensions(entry.lengthCm, entry.widthCm, entry.heightCm);
        }
        if (entry.pricePrecision < 0) {
            entry.pricePrecision = CalculationService::inferPricePrecision(entry.pricePerSquareMeter);
        }
        if (entry.manualUnitPricePrecision < 0) {
            entry.manualUnitPricePrecision = CalculationService::inferPricePrecision(entry.manualUnitPrice);
        }
        entries.append(entry);
    }

    return entries;
}

int EntryRepository::countEntries(qint64 sheetId, QString *errorMessage) const
{
    if (!isValidSheetId(sheetId)) {
        assignError(errorMessage, QStringLiteral("标签页不存在"));
        return 0;
    }

    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT COUNT(*) FROM entries WHERE sheet_id = ?"));
    query.addBindValue(sheetId);
    if (!query.exec()) {
        assignError(errorMessage, query.lastError().text());
        return 0;
    }

    if (!query.next()) {
        return 0;
    }

    return query.value(0).toInt();
}

QVector<StatementEntry> EntryRepository::loadRecentSpecificationTemplates(QString *errorMessage) const
{
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
        "SELECT id, delivery_date, order_number, specification, length_cm, width_cm, height_cm, quantity, formula_type, price_per_sqm, price_precision, manual_unit_price, manual_unit_price_precision, background_color "
        "FROM entries "
        "WHERE TRIM(COALESCE(specification, '')) <> '' "
        "ORDER BY delivery_date DESC, id DESC"));

    if (!query.exec()) {
        assignError(errorMessage, query.lastError().text());
        return {};
    }

    QVector<StatementEntry> templates;
    QSet<QString> seenSpecifications;
    while (query.next()) {
        StatementEntry entry;
        entry.id = query.value(0).toLongLong();
        entry.deliveryDate = QDate::fromString(query.value(1).toString(), Qt::ISODate);
        entry.orderNumber = query.value(2).toString();
        entry.specification = CalculationService::normalizeSpecification(query.value(3).toString());
        entry.lengthCm = query.value(4).toDouble();
        entry.widthCm = query.value(5).toDouble();
        entry.heightCm = query.value(6).toDouble();
        entry.quantity = query.value(7).toInt();
        entry.formulaType = formulaTypeFromString(query.value(8).toString()).value_or(FormulaType::A);
        entry.pricePerSquareMeter = query.value(9).toDouble();
        entry.pricePrecision = query.value(10).toInt();
        entry.manualUnitPrice = query.value(11).toDouble();
        entry.manualUnitPricePrecision = query.value(12).toInt();
        entry.backgroundColorHex = normalizeBackgroundColorHex(query.value(13).toString());

        if (entry.specification.isEmpty()) {
            entry.specification = CalculationService::specificationFromDimensions(entry.lengthCm, entry.widthCm, entry.heightCm);
        }
        if (entry.specification.isEmpty() || seenSpecifications.contains(entry.specification)) {
            continue;
        }

        if (entry.pricePrecision < 0) {
            entry.pricePrecision = CalculationService::inferPricePrecision(entry.pricePerSquareMeter);
        }
        if (entry.manualUnitPricePrecision < 0) {
            entry.manualUnitPricePrecision = CalculationService::inferPricePrecision(entry.manualUnitPrice);
        }

        seenSpecifications.insert(entry.specification);
        templates.append(entry);
        if (templates.size() >= 200) {
            break;
        }
    }

    return templates;
}

bool EntryRepository::saveChanges(qint64 sheetId, const QVector<StatementEntry> &entries, const QVector<qint64> &deletedIds,
                                  QString *errorMessage, QMap<int, qint64> *insertedIds)
{
    if (!isValidSheetId(sheetId)) {
        return assignError(errorMessage, QStringLiteral("标签页不存在"));
    }

    QString existsError;
    if (!sheetExists(m_database, sheetId, &existsError)) {
        if (existsError.isEmpty()) {
            return assignError(errorMessage, QStringLiteral("标签页不存在"));
        }
        return assignError(errorMessage, existsError);
    }

    if (!m_database.transaction()) {
        return assignError(errorMessage, m_database.lastError().text());
    }

    if (!deletedIds.isEmpty()) {
        QStringList placeholders;
        placeholders.reserve(deletedIds.size());
        for (int index = 0; index < deletedIds.size(); ++index) {
            placeholders.append(QStringLiteral("?"));
        }

        QSqlQuery deleteQuery(m_database);
        deleteQuery.prepare(QStringLiteral("DELETE FROM entries WHERE sheet_id = ? AND id IN (%1)").arg(placeholders.join(QStringLiteral(","))));
        deleteQuery.addBindValue(sheetId);
        for (const qint64 id : deletedIds) {
            deleteQuery.addBindValue(id);
        }

        if (!deleteQuery.exec()) {
            m_database.rollback();
            return assignError(errorMessage, deleteQuery.lastError().text());
        }
    }

    for (int row = 0; row < entries.size(); ++row) {
        const StatementEntry &entry = entries.at(row);
        const QString specification = CalculationService::formatSpecification(entry);
        const auto parsedSpecification = CalculationService::parseSpecification(specification);
        if (!parsedSpecification.has_value()) {
            m_database.rollback();
            return assignError(errorMessage, QStringLiteral("规格格式无效，请使用 长*宽 或 长*宽*高"));
        }

        if (!CalculationService::isFormulaAllowed(specification, entry.formulaType)) {
            m_database.rollback();
            return assignError(errorMessage, QStringLiteral("当前规格与所选计算公式不匹配"));
        }

        const double lengthCm = CalculationService::roundTo(parsedSpecification->lengthCm, 2);
        const double widthCm = CalculationService::roundTo(parsedSpecification->widthCm, 2);
        const double heightCm = CalculationService::roundTo(parsedSpecification->heightCm.value_or(0.0), 2);
        const int pricePrecision = CalculationService::effectivePricePrecision(entry);
        const double pricePerSquareMeter = CalculationService::roundTo(entry.pricePerSquareMeter, pricePrecision);
        const int manualUnitPricePrecision = CalculationService::effectiveManualUnitPricePrecision(entry);
        const double manualUnitPrice = CalculationService::roundTo(entry.manualUnitPrice, manualUnitPricePrecision);
        QString backgroundColorHex = normalizeBackgroundColorHex(entry.backgroundColorHex);
        if (backgroundColorHex.isEmpty()) {
            backgroundColorHex = QStringLiteral("");
        }
        const int displayOrder = row + 1;

        if (entry.id < 0) {
            QSqlQuery insertQuery(m_database);
            insertQuery.prepare(QStringLiteral(
                "INSERT INTO entries (sheet_id, delivery_date, order_number, specification, length_cm, width_cm, height_cm, quantity, formula_type, price_per_sqm, price_precision, manual_unit_price, manual_unit_price_precision, background_color, display_order) "
                "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
            insertQuery.addBindValue(sheetId);
            insertQuery.addBindValue(serializeDeliveryDate(entry.deliveryDate));
            insertQuery.addBindValue(entry.orderNumber.trimmed());
            insertQuery.addBindValue(specification);
            insertQuery.addBindValue(lengthCm);
            insertQuery.addBindValue(widthCm);
            insertQuery.addBindValue(heightCm);
            insertQuery.addBindValue(entry.quantity);
            insertQuery.addBindValue(formulaTypeDisplay(entry.formulaType));
            insertQuery.addBindValue(pricePerSquareMeter);
            insertQuery.addBindValue(pricePrecision);
            insertQuery.addBindValue(manualUnitPrice);
            insertQuery.addBindValue(manualUnitPricePrecision);
            insertQuery.addBindValue(backgroundColorHex);
            insertQuery.addBindValue(displayOrder);

            if (!insertQuery.exec()) {
                m_database.rollback();
                return assignError(errorMessage, insertQuery.lastError().text());
            }

            if (insertedIds != nullptr) {
                insertedIds->insert(row, insertQuery.lastInsertId().toLongLong());
            }
        } else {
            QSqlQuery updateQuery(m_database);
            updateQuery.prepare(QStringLiteral(
                "UPDATE entries SET delivery_date = ?, order_number = ?, specification = ?, length_cm = ?, width_cm = ?, height_cm = ?, quantity = ?, formula_type = ?, price_per_sqm = ?, price_precision = ?, manual_unit_price = ?, manual_unit_price_precision = ?, background_color = ?, display_order = ? "
                "WHERE id = ? AND sheet_id = ?"));
            updateQuery.addBindValue(serializeDeliveryDate(entry.deliveryDate));
            updateQuery.addBindValue(entry.orderNumber.trimmed());
            updateQuery.addBindValue(specification);
            updateQuery.addBindValue(lengthCm);
            updateQuery.addBindValue(widthCm);
            updateQuery.addBindValue(heightCm);
            updateQuery.addBindValue(entry.quantity);
            updateQuery.addBindValue(formulaTypeDisplay(entry.formulaType));
            updateQuery.addBindValue(pricePerSquareMeter);
            updateQuery.addBindValue(pricePrecision);
            updateQuery.addBindValue(manualUnitPrice);
            updateQuery.addBindValue(manualUnitPricePrecision);
            updateQuery.addBindValue(backgroundColorHex);
            updateQuery.addBindValue(displayOrder);
            updateQuery.addBindValue(entry.id);
            updateQuery.addBindValue(sheetId);

            if (!updateQuery.exec()) {
                m_database.rollback();
                return assignError(errorMessage, updateQuery.lastError().text());
            }
        }
    }

    if (!m_database.commit()) {
        return assignError(errorMessage, m_database.lastError().text());
    }

    return true;
}

} // namespace cartonledger
