#include "core/AppSettings.h"

#include "core/CalculationService.h"

#include <QSettings>

namespace cartonledger {

namespace {

constexpr auto kDefaultPriceKey = "pricing/defaultPricePerSquareMeter";
constexpr auto kDefaultPricePrecisionKey = "pricing/defaultPricePerSquareMeterPrecision";
constexpr auto kLastSheetIdKey = "ui/lastSheetId";
constexpr double kFallbackDefaultPrice = 2.15;
constexpr int kFallbackDefaultPricePrecision = 2;

} // namespace

double AppSettings::defaultPricePerSquareMeter() const
{
    QSettings settings;
    const double value = settings.value(kDefaultPriceKey, kFallbackDefaultPrice).toDouble();
    return value > 0.0 ? value : kFallbackDefaultPrice;
}

int AppSettings::defaultPricePerSquareMeterPrecision() const
{
    QSettings settings;
    const int precision = settings.value(
        kDefaultPricePrecisionKey,
        CalculationService::inferPricePrecision(defaultPricePerSquareMeter())).toInt();
    return precision >= 0 ? precision : kFallbackDefaultPricePrecision;
}

void AppSettings::setDefaultPricePerSquareMeter(double value, int precision)
{
    QSettings settings;
    const double normalizedValue = value > 0.0 ? value : kFallbackDefaultPrice;
    settings.setValue(kDefaultPriceKey, normalizedValue);
    settings.setValue(kDefaultPricePrecisionKey, precision >= 0 ? precision : CalculationService::inferPricePrecision(normalizedValue));
}

qint64 AppSettings::lastSheetId() const
{
    QSettings settings;
    return settings.value(kLastSheetIdKey, -1).toLongLong();
}

void AppSettings::setLastSheetId(qint64 sheetId)
{
    QSettings settings;
    settings.setValue(kLastSheetIdKey, sheetId);
}

} // namespace cartonledger
