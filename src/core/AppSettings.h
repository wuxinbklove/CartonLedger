#pragma once

#include <QtGlobal>

namespace cartonledger {

class AppSettings {
public:
    double defaultPricePerSquareMeter() const;
    int defaultPricePerSquareMeterPrecision() const;
    void setDefaultPricePerSquareMeter(double value, int precision);
    qint64 lastSheetId() const;
    void setLastSheetId(qint64 sheetId);
};

} // namespace cartonledger
