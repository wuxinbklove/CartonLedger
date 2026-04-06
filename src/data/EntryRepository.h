#pragma once

#include "core/StatementEntry.h"

#include <QMap>
#include <QSqlDatabase>
#include <QVector>

namespace cartonledger {

struct SheetInfo {
    qint64 id = -1;
    QString name;
    int displayOrder = 0;
};

class EntryRepository {
public:
    explicit EntryRepository(QSqlDatabase database);

    QVector<SheetInfo> loadSheets(QString *errorMessage = nullptr) const;
    SheetInfo createSheet(const QString &name, QString *errorMessage = nullptr);
    bool renameSheet(qint64 sheetId, const QString &name, QString *errorMessage = nullptr);
    bool deleteSheet(qint64 sheetId, QString *errorMessage = nullptr);
    bool reorderSheets(const QVector<qint64> &orderedIds, QString *errorMessage = nullptr);

    QVector<StatementEntry> loadEntries(qint64 sheetId, QString *errorMessage = nullptr) const;
    int countEntries(qint64 sheetId, QString *errorMessage = nullptr) const;
    QVector<StatementEntry> loadRecentSpecificationTemplates(QString *errorMessage = nullptr) const;
    bool saveChanges(qint64 sheetId, const QVector<StatementEntry> &entries, const QVector<qint64> &deletedIds,
                     QString *errorMessage = nullptr, QMap<int, qint64> *insertedIds = nullptr);

private:
    QSqlDatabase m_database;
};

} // namespace cartonledger
