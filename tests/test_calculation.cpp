#include <QtTest>

#include "core/CalculationService.h"

using namespace cartonledger;

class CalculationServiceTest : public QObject {
    Q_OBJECT

private slots:
    void parsesAndNormalizesSpecification();
    void restrictsFormulaBySpecificationShape();
    void parsesHighPrecisionPriceValue();
    void calculatesFormulaA();
    void calculatesFormulaB();
    void calculatesFormulaC();
    void usesTwoDecimalsForAutomaticAmounts();
    void calculatesManualUnitPrice();
    void infersFormulaFromEditedUnitPrice();
    void rejectsMismatchedFormula();
    void preservesHigherPrecisionForAutoFormula();
};

void CalculationServiceTest::parsesAndNormalizesSpecification()
{
    const auto parsed = CalculationService::parseSpecification(QStringLiteral("100 x 50 x 40"));

    QVERIFY(parsed.has_value());
    QCOMPARE(parsed->lengthCm, 100.0);
    QCOMPARE(parsed->widthCm, 50.0);
    QVERIFY(parsed->heightCm.has_value());
    QCOMPARE(parsed->heightCm.value(), 40.0);
    QCOMPARE(CalculationService::normalizeSpecification(QStringLiteral("100 x 50 x 40")), QStringLiteral("100*50*40"));
}

void CalculationServiceTest::restrictsFormulaBySpecificationShape()
{
    QVERIFY(CalculationService::isFormulaAllowed(QStringLiteral("100*50"), FormulaType::C));
    QVERIFY(CalculationService::isFormulaAllowed(QStringLiteral("100*50"), FormulaType::Manual));
    QVERIFY(!CalculationService::isFormulaAllowed(QStringLiteral("100*50"), FormulaType::A));
    QVERIFY(!CalculationService::isFormulaAllowed(QStringLiteral("100*50"), FormulaType::B));
    QVERIFY(CalculationService::isFormulaAllowed(QStringLiteral("100*50*40"), FormulaType::A));
    QVERIFY(CalculationService::isFormulaAllowed(QStringLiteral("100*50*40"), FormulaType::B));
    QVERIFY(CalculationService::isFormulaAllowed(QStringLiteral("100*50*40"), FormulaType::Manual));
    QVERIFY(!CalculationService::isFormulaAllowed(QStringLiteral("100*50*40"), FormulaType::C));
}

void CalculationServiceTest::parsesHighPrecisionPriceValue()
{
    const auto parsed = CalculationService::parsePriceValue(QStringLiteral("6.123456789"));

    QVERIFY(parsed.has_value());
    QVERIFY(qAbs(parsed->value - 6.123456789) < 1e-12);
    QCOMPARE(parsed->precision, 9);
}

void CalculationServiceTest::calculatesFormulaA()
{
    StatementEntry entry;
    entry.specification = QStringLiteral("100*50*40");
    entry.quantity = 12;
    entry.pricePerSquareMeter = 2.15;
    entry.pricePrecision = 2;
    entry.formulaType = FormulaType::A;

    QCOMPARE(CalculationService::calculateUnitPrice(entry), 6.09);
    QCOMPARE(CalculationService::calculateTotalPrice(entry), 73.08);
}

void CalculationServiceTest::calculatesFormulaB()
{
    StatementEntry entry;
    entry.specification = QStringLiteral("100*50*40");
    entry.quantity = 5;
    entry.pricePerSquareMeter = 2.15;
    entry.pricePrecision = 2;
    entry.formulaType = FormulaType::B;

    QCOMPARE(CalculationService::calculateUnitPrice(entry), 9.40);
    QCOMPARE(CalculationService::calculateTotalPrice(entry), 47.0);
}

void CalculationServiceTest::calculatesFormulaC()
{
    StatementEntry entry;
    entry.specification = QStringLiteral("100*50");
    entry.quantity = 7;
    entry.pricePerSquareMeter = 2.15;
    entry.pricePrecision = 2;
    entry.formulaType = FormulaType::C;

    QCOMPARE(CalculationService::calculateUnitPrice(entry), 1.08);
    QCOMPARE(CalculationService::calculateTotalPrice(entry), 7.56);
}

void CalculationServiceTest::usesTwoDecimalsForAutomaticAmounts()
{
    StatementEntry twoDecimalMinimumEntry;
    twoDecimalMinimumEntry.specification = QStringLiteral("100*50");
    twoDecimalMinimumEntry.quantity = 3;
    twoDecimalMinimumEntry.pricePerSquareMeter = 1.1;
    twoDecimalMinimumEntry.pricePrecision = 1;
    twoDecimalMinimumEntry.formulaType = FormulaType::C;

    QCOMPARE(CalculationService::effectiveAmountPrecision(twoDecimalMinimumEntry), 2);
    QCOMPARE(CalculationService::calculateUnitPrice(twoDecimalMinimumEntry), 0.55);
    QCOMPARE(CalculationService::calculateTotalPrice(twoDecimalMinimumEntry), 1.65);

    StatementEntry higherPrecisionPriceEntry;
    higherPrecisionPriceEntry.specification = QStringLiteral("100*50");
    higherPrecisionPriceEntry.quantity = 3;
    higherPrecisionPriceEntry.pricePerSquareMeter = 1.125;
    higherPrecisionPriceEntry.pricePrecision = 3;
    higherPrecisionPriceEntry.formulaType = FormulaType::C;

    QCOMPARE(CalculationService::effectiveAmountPrecision(higherPrecisionPriceEntry), 2);
    QCOMPARE(CalculationService::calculateUnitPrice(higherPrecisionPriceEntry), 0.56);
    QCOMPARE(CalculationService::calculateTotalPrice(higherPrecisionPriceEntry), 1.68);
}

void CalculationServiceTest::calculatesManualUnitPrice()
{
    StatementEntry entry;
    entry.specification = QStringLiteral("100*50*40");
    entry.quantity = 4;
    entry.formulaType = FormulaType::Manual;
    entry.manualUnitPrice = 6.25;
    entry.manualUnitPricePrecision = 2;

    QCOMPARE(CalculationService::calculateUnitPrice(entry), 6.25);
    QCOMPARE(CalculationService::calculateTotalPrice(entry), 25.0);
    QCOMPARE(CalculationService::effectiveAmountPrecision(entry), 2);
}

void CalculationServiceTest::infersFormulaFromEditedUnitPrice()
{
    StatementEntry threeDimensionalEntry;
    threeDimensionalEntry.specification = QStringLiteral("100*50*40");
    threeDimensionalEntry.pricePerSquareMeter = 2.15;
    threeDimensionalEntry.pricePrecision = 2;

    QCOMPARE(CalculationService::inferFormulaTypeForUnitPrice(threeDimensionalEntry, 6.09), std::optional<FormulaType>(FormulaType::A));
    QCOMPARE(CalculationService::inferFormulaTypeForUnitPrice(threeDimensionalEntry, 9.40), std::optional<FormulaType>(FormulaType::B));
    QCOMPARE(CalculationService::inferFormulaTypeForUnitPrice(threeDimensionalEntry, 8.88), std::nullopt);
    QCOMPARE(CalculationService::inferFormulaTypeForUnitPrice(threeDimensionalEntry, 6.25), std::nullopt);

    StatementEntry regressionEntry;
    regressionEntry.specification = QStringLiteral("20*12*25.8");
    regressionEntry.pricePerSquareMeter = 4.6;
    regressionEntry.pricePrecision = 1;

    QCOMPARE(CalculationService::calculateUnitPrice(regressionEntry), 1.32);
    QCOMPARE(CalculationService::inferFormulaTypeForUnitPrice(regressionEntry, 1.33), std::nullopt);

    StatementEntry twoDimensionalEntry;
    twoDimensionalEntry.specification = QStringLiteral("100*50");
    twoDimensionalEntry.pricePerSquareMeter = 6.2508;
    twoDimensionalEntry.pricePrecision = 4;

    QCOMPARE(CalculationService::inferFormulaTypeForUnitPrice(twoDimensionalEntry, 3.124), std::optional<FormulaType>(FormulaType::C));
    QCOMPARE(CalculationService::inferFormulaTypeForUnitPrice(twoDimensionalEntry, 3.13), std::optional<FormulaType>(FormulaType::C));
    QCOMPARE(CalculationService::inferFormulaTypeForUnitPrice(twoDimensionalEntry, 3.1), std::optional<FormulaType>(FormulaType::C));
    QCOMPARE(CalculationService::inferFormulaTypeForUnitPrice(twoDimensionalEntry, 3.0), std::optional<FormulaType>(FormulaType::C));
    QCOMPARE(CalculationService::inferFormulaTypeForUnitPrice(twoDimensionalEntry, 4.0), std::nullopt);
}

void CalculationServiceTest::rejectsMismatchedFormula()
{
    StatementEntry entry;
    entry.specification = QStringLiteral("100*50");
    entry.quantity = 8;
    entry.pricePerSquareMeter = 2.15;
    entry.pricePrecision = 2;
    entry.formulaType = FormulaType::A;

    QCOMPARE(CalculationService::calculateUnitPrice(entry), 0.0);
    QCOMPARE(CalculationService::calculateTotalPrice(entry), 0.0);
}

void CalculationServiceTest::preservesHigherPrecisionForAutoFormula()
{
    // 35.5*27, price 2.15, formula C: 35.5*27*2.15/10000 = 0.2061225
    // User enters 0.206 (3 decimals) which matches C at precision 3.
    // effectiveAmountPrecision should be 3, not 2.
    StatementEntry entry;
    entry.specification = QStringLiteral("35.5*27");
    entry.quantity = 84;
    entry.pricePerSquareMeter = 2.15;
    entry.pricePrecision = 2;
    entry.formulaType = FormulaType::C;
    entry.manualUnitPricePrecision = 3;

    QCOMPARE(CalculationService::effectiveAmountPrecision(entry), 3);
    QCOMPARE(CalculationService::calculateUnitPrice(entry), 0.206);
    QCOMPARE(CalculationService::calculateTotalPrice(entry), 17.304);

    // When manualUnitPricePrecision is not set, default to 2 decimals.
    entry.manualUnitPricePrecision = -1;
    QCOMPARE(CalculationService::effectiveAmountPrecision(entry), 2);
    QCOMPARE(CalculationService::calculateUnitPrice(entry), 0.21);

    // When manualUnitPricePrecision equals automatic (2), still 2.
    entry.manualUnitPricePrecision = 2;
    QCOMPARE(CalculationService::effectiveAmountPrecision(entry), 2);
    QCOMPARE(CalculationService::calculateUnitPrice(entry), 0.21);
}

QTEST_APPLESS_MAIN(CalculationServiceTest)

#include "test_calculation.moc"
