#pragma once

#include <QVector>
#include <QStringList>

namespace cartonledger {

QVector<int> resolvePasteTargetColumns(int startColumn, const QStringList &cells);

} // namespace cartonledger