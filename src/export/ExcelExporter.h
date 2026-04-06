#pragma once

#include "core/StatementEntry.h"

#include <QString>
#include <QVector>

namespace cartonledger {

class ExcelExporter {
public:
    static bool exportEntries(const QString &filePath, const QVector<StatementEntry> &entries, QString *errorMessage = nullptr);
};

} // namespace cartonledger