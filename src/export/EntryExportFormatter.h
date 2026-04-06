#pragma once

#include "core/StatementEntry.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace cartonledger {

enum class ExportCellType {
    Text,
    Number,
};

struct ExportCell {
    QString value;
    ExportCellType type = ExportCellType::Text;
};

using ExportRow = QVector<ExportCell>;

QStringList exportColumnHeaders();
QVector<ExportRow> buildExportRows(const QVector<StatementEntry> &entries);

} // namespace cartonledger