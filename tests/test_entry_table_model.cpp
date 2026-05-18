#include <QtTest>

#include "ui/PasteColumnResolver.h"
#include "ui/models/EntryTableModel.h"

#include <QBrush>
#include <QColor>
#include <QUndoStack>

using namespace cartonledger;

class EntryTableModelTest : public QObject {
    Q_OBJECT

private slots:
    void autoFillsRecentValuesWhenSpecificationMatches();
    void acceptsExcelStyleDateText();
    void allowsEmptyDeliveryDate();
    void allowsEditingUnitPriceAtAnyTime();
    void defaultsToAOrCWhenNoTemplateMatches();
    void infersFormulaWhenUnitPriceChanges();
    void keepsEditedUnitPriceWhenNoFormulaMatches();
    void preservesHigherPrecisionUnitPriceForAutoFormula();
    void keepsManualInputPrecision();
    void resolvesThreeColumnPasteFromSpecification();
    void usesUnsavedRowsForAutocompleteAndAutoFill();
    void highlightsUnsavedRowsUntilReload();
    void setsAndClearsRowBackgroundColor();
    void setsAndClearsMultipleRowBackgroundColors();
    void usesUpdatedColumnOrder();
    void showsVerticalHeaderRowNumbers();
    void showsFormulaGuideInHeaderTooltip();
};

void EntryTableModelTest::autoFillsRecentValuesWhenSpecificationMatches()
{
    EntryTableModel model;

    StatementEntry recentEntry;
    recentEntry.specification = QStringLiteral("100*50");
    recentEntry.quantity = 32;
    recentEntry.pricePerSquareMeter = 2.150;
    recentEntry.pricePrecision = 3;
    recentEntry.formulaType = FormulaType::C;

    model.setRecentSpecificationTemplates({recentEntry});
    QVERIFY(model.insertRow(0));
    QVERIFY(model.setData(model.index(0, EntryTableModel::SpecificationColumn), QStringLiteral("100*50"), Qt::EditRole));

    const StatementEntry &entry = model.entries().constFirst();
    QCOMPARE(entry.specification, QStringLiteral("100*50"));
    QCOMPARE(entry.quantity, 32);
    QCOMPARE(entry.pricePerSquareMeter, 2.15);
    QCOMPARE(entry.pricePrecision, 3);
    QCOMPARE(entry.formulaType, FormulaType::C);
    QCOMPARE(model.data(model.index(0, EntryTableModel::PricePerSquareMeterColumn), Qt::DisplayRole).toString(), QStringLiteral("2.150"));
}

void EntryTableModelTest::acceptsExcelStyleDateText()
{
    EntryTableModel model;

    QVERIFY(model.insertRow(0));
    QVERIFY(model.setData(model.index(0, EntryTableModel::DeliveryDateColumn), QStringLiteral("2026/4/4"), Qt::EditRole));
    QCOMPARE(model.data(model.index(0, EntryTableModel::DeliveryDateColumn), Qt::DisplayRole).toString(), QStringLiteral("2026-04-04"));
}

void EntryTableModelTest::allowsEmptyDeliveryDate()
{
    EntryTableModel model;

    QVERIFY(model.insertRow(0));
    QVERIFY(model.setData(model.index(0, EntryTableModel::DeliveryDateColumn), QStringLiteral("2026-04-04"), Qt::EditRole));
    QVERIFY(model.setData(model.index(0, EntryTableModel::DeliveryDateColumn), QString(), Qt::EditRole));
    QVERIFY(model.data(model.index(0, EntryTableModel::DeliveryDateColumn), Qt::DisplayRole).toString().isEmpty());
    QVERIFY(!model.entries().constFirst().deliveryDate.isValid());
}

void EntryTableModelTest::allowsEditingUnitPriceAtAnyTime()
{
    EntryTableModel model;

    QVERIFY(model.insertRow(0));
    QVERIFY(model.setData(model.index(0, EntryTableModel::SpecificationColumn), QStringLiteral("100*50"), Qt::EditRole));
    QVERIFY(model.setData(model.index(0, EntryTableModel::QuantityColumn), QStringLiteral("3"), Qt::EditRole));

    const QModelIndex unitPriceIndex = model.index(0, EntryTableModel::UnitPriceColumn);
    QVERIFY(model.flags(unitPriceIndex) & Qt::ItemIsEditable);
    QCOMPARE(model.data(unitPriceIndex, Qt::DisplayRole).toString(), QStringLiteral("1.08"));

    QVERIFY(model.setData(unitPriceIndex, QStringLiteral("6.25"), Qt::EditRole));

    const StatementEntry &entry = model.entries().constFirst();
    QCOMPARE(entry.formulaType, FormulaType::Manual);
    QCOMPARE(entry.manualUnitPrice, 6.25);
    QCOMPARE(entry.manualUnitPricePrecision, 2);
    QCOMPARE(model.data(unitPriceIndex, Qt::DisplayRole).toString(), QStringLiteral("6.25"));
    QCOMPARE(model.data(model.index(0, EntryTableModel::TotalPriceColumn), Qt::DisplayRole).toString(), QStringLiteral("18.75"));
}

void EntryTableModelTest::defaultsToAOrCWhenNoTemplateMatches()
{
    EntryTableModel model;

    QVERIFY(model.insertRow(0));
    QCOMPARE(model.entries().constFirst().formulaType, FormulaType::A);

    QVERIFY(model.setData(model.index(0, EntryTableModel::SpecificationColumn), QStringLiteral("100*50"), Qt::EditRole));
    QCOMPARE(model.entries().constFirst().formulaType, FormulaType::C);

    QVERIFY(model.insertRow(1));
    QVERIFY(model.setData(model.index(1, EntryTableModel::SpecificationColumn), QStringLiteral("100*50*40"), Qt::EditRole));
    QCOMPARE(model.entries().at(1).formulaType, FormulaType::A);
}

void EntryTableModelTest::infersFormulaWhenUnitPriceChanges()
{
    EntryTableModel model;

    QVERIFY(model.insertRow(0));
    QVERIFY(model.setData(model.index(0, EntryTableModel::SpecificationColumn), QStringLiteral("100*50*40"), Qt::EditRole));

    const QModelIndex unitPriceIndex = model.index(0, EntryTableModel::UnitPriceColumn);
    QVERIFY(model.setData(unitPriceIndex, QStringLiteral("9.40"), Qt::EditRole));
    QCOMPARE(model.entries().constFirst().formulaType, FormulaType::B);
    QCOMPARE(model.data(unitPriceIndex, Qt::DisplayRole).toString(), QStringLiteral("9.40"));

    QVERIFY(model.setData(unitPriceIndex, QStringLiteral("6.09"), Qt::EditRole));
    QCOMPARE(model.entries().constFirst().formulaType, FormulaType::A);

    QVERIFY(model.setData(unitPriceIndex, QStringLiteral("8.88"), Qt::EditRole));
    QCOMPARE(model.entries().constFirst().formulaType, FormulaType::Manual);
}

void EntryTableModelTest::keepsEditedUnitPriceWhenNoFormulaMatches()
{
    EntryTableModel model;

    QVERIFY(model.insertRow(0));
    QVERIFY(model.setData(model.index(0, EntryTableModel::SpecificationColumn), QStringLiteral("20*12*25.8"), Qt::EditRole));
    QVERIFY(model.setData(model.index(0, EntryTableModel::QuantityColumn), QStringLiteral("1820"), Qt::EditRole));
    QVERIFY(model.setData(model.index(0, EntryTableModel::PricePerSquareMeterColumn), QStringLiteral("4.6"), Qt::EditRole));

    const QModelIndex unitPriceIndex = model.index(0, EntryTableModel::UnitPriceColumn);
    QVERIFY(model.setData(unitPriceIndex, QStringLiteral("1.33"), Qt::EditRole));

    const StatementEntry &entry = model.entries().constFirst();
    QCOMPARE(entry.formulaType, FormulaType::Manual);
    QCOMPARE(entry.manualUnitPrice, 1.33);
    QCOMPARE(model.data(unitPriceIndex, Qt::DisplayRole).toString(), QStringLiteral("1.33"));
}

void EntryTableModelTest::preservesHigherPrecisionUnitPriceForAutoFormula()
{
    EntryTableModel model;

    QVERIFY(model.insertRow(0));
    QVERIFY(model.setData(model.index(0, EntryTableModel::SpecificationColumn), QStringLiteral("35.5*27"), Qt::EditRole));
    QVERIFY(model.setData(model.index(0, EntryTableModel::QuantityColumn), QStringLiteral("84"), Qt::EditRole));
    QVERIFY(model.setData(model.index(0, EntryTableModel::PricePerSquareMeterColumn), QStringLiteral("2.15"), Qt::EditRole));

    const QModelIndex unitPriceIndex = model.index(0, EntryTableModel::UnitPriceColumn);
    QVERIFY(model.setData(unitPriceIndex, QStringLiteral("0.206"), Qt::EditRole));

    const StatementEntry &entry = model.entries().constFirst();
    QCOMPARE(entry.formulaType, FormulaType::C);
    QCOMPARE(model.data(unitPriceIndex, Qt::DisplayRole).toString(), QStringLiteral("0.206"));

    const QModelIndex totalPriceIndex = model.index(0, EntryTableModel::TotalPriceColumn);
    QCOMPARE(model.data(totalPriceIndex, Qt::DisplayRole).toString(), QStringLiteral("17.304"));
}

void EntryTableModelTest::keepsManualInputPrecision()
{
    EntryTableModel model;

    QVERIFY(model.insertRow(0));
    QVERIFY(model.setData(model.index(0, EntryTableModel::SpecificationColumn), QStringLiteral("100*50"), Qt::EditRole));

    const QModelIndex unitPriceIndex = model.index(0, EntryTableModel::UnitPriceColumn);
    QVERIFY(model.setData(unitPriceIndex, QStringLiteral("6.123456789"), Qt::EditRole));

    const StatementEntry &entry = model.entries().constFirst();
    QCOMPARE(entry.formulaType, FormulaType::Manual);
    QCOMPARE(entry.manualUnitPricePrecision, 9);
    QCOMPARE(model.data(unitPriceIndex, Qt::DisplayRole).toString(), QStringLiteral("6.123456789"));
}

void EntryTableModelTest::resolvesThreeColumnPasteFromSpecification()
{
    const QVector<int> priceColumns = resolvePasteTargetColumns(
        EntryTableModel::SpecificationColumn,
        {QStringLiteral("100*50*40"), QStringLiteral("2.15"), QStringLiteral("9.40")});
    QCOMPARE(priceColumns, QVector<int>({
        EntryTableModel::SpecificationColumn,
        EntryTableModel::PricePerSquareMeterColumn,
        EntryTableModel::UnitPriceColumn,
    }));

    const QVector<int> contiguousColumns = resolvePasteTargetColumns(
        EntryTableModel::SpecificationColumn,
        {QStringLiteral("100*50*40"), QStringLiteral("3"), QStringLiteral("2.15")});
    QCOMPARE(contiguousColumns, QVector<int>({
        EntryTableModel::SpecificationColumn,
        EntryTableModel::QuantityColumn,
        EntryTableModel::PricePerSquareMeterColumn,
    }));
}

void EntryTableModelTest::usesUnsavedRowsForAutocompleteAndAutoFill()
{
    EntryTableModel model;

    QVERIFY(model.insertRow(0));
    QVERIFY(model.setData(model.index(0, EntryTableModel::SpecificationColumn), QStringLiteral("120*60"), Qt::EditRole));
    QVERIFY(model.setData(model.index(0, EntryTableModel::QuantityColumn), QStringLiteral("18"), Qt::EditRole));
    QVERIFY(model.setData(model.index(0, EntryTableModel::PricePerSquareMeterColumn), QStringLiteral("2.350"), Qt::EditRole));

    const QStringList suggestions = model.data(
        model.index(0, EntryTableModel::SpecificationColumn),
        EntryTableModel::SpecificationSuggestionsRole).toStringList();
    QVERIFY(suggestions.contains(QStringLiteral("120*60")));

    QVERIFY(model.insertRow(1));
    QVERIFY(model.setData(model.index(1, EntryTableModel::SpecificationColumn), QStringLiteral("120*60"), Qt::EditRole));

    const StatementEntry &entry = model.entries().at(1);
    QCOMPARE(entry.quantity, 18);
    QCOMPARE(entry.pricePerSquareMeter, 2.35);
    QCOMPARE(entry.pricePrecision, 3);
    QCOMPARE(entry.formulaType, FormulaType::C);
}

void EntryTableModelTest::highlightsUnsavedRowsUntilReload()
{
    EntryTableModel model;

    StatementEntry savedEntry;
    savedEntry.id = 100;
    savedEntry.specification = QStringLiteral("100*50");
    savedEntry.quantity = 10;
    savedEntry.pricePerSquareMeter = 2.15;
    savedEntry.pricePrecision = 2;
    savedEntry.formulaType = FormulaType::C;

    model.setEntries({savedEntry});
    QVERIFY(!model.data(model.index(0, EntryTableModel::SpecificationColumn), Qt::BackgroundRole).isValid());

    QVERIFY(model.setData(model.index(0, EntryTableModel::QuantityColumn), QStringLiteral("12"), Qt::EditRole));
    QVERIFY(model.data(model.index(0, EntryTableModel::SpecificationColumn), Qt::BackgroundRole).canConvert<QBrush>());

    QVERIFY(model.insertRow(1));
    QVERIFY(model.data(model.index(1, EntryTableModel::SpecificationColumn), Qt::BackgroundRole).canConvert<QBrush>());

    model.setEntries({savedEntry});
    QVERIFY(!model.data(model.index(0, EntryTableModel::SpecificationColumn), Qt::BackgroundRole).isValid());
}

void EntryTableModelTest::setsAndClearsRowBackgroundColor()
{
    EntryTableModel model;
    QUndoStack undoStack;
    model.setUndoStack(&undoStack);

    StatementEntry savedEntry;
    savedEntry.id = 100;
    savedEntry.specification = QStringLiteral("100*50");
    savedEntry.quantity = 10;
    savedEntry.pricePerSquareMeter = 2.15;
    savedEntry.pricePrecision = 2;
    savedEntry.formulaType = FormulaType::C;

    model.setEntries({savedEntry});
    QVERIFY(model.setRowBackgroundColor(0, QColor(QStringLiteral("#cce5ff"))));
    QCOMPARE(model.entries().constFirst().backgroundColorHex, QStringLiteral("#CCE5FF"));
    QVERIFY(model.isDirty());

    const QModelIndex index = model.index(0, EntryTableModel::SpecificationColumn);
    QBrush brush = qvariant_cast<QBrush>(model.data(index, Qt::BackgroundRole));
    QCOMPARE(brush.color().name(QColor::HexRgb).toUpper(), QStringLiteral("#CCE5FF"));

    QSet<int> searchRows;
    searchRows.insert(0);
    model.setSearchHighlightRows(searchRows);
    brush = qvariant_cast<QBrush>(model.data(index, Qt::BackgroundRole));
    QCOMPARE(brush.color().name(QColor::HexRgb).toUpper(), QStringLiteral("#FFFF64"));

    model.clearSearchHighlights();
    brush = qvariant_cast<QBrush>(model.data(index, Qt::BackgroundRole));
    QCOMPARE(brush.color().name(QColor::HexRgb).toUpper(), QStringLiteral("#CCE5FF"));

    undoStack.undo();
    QVERIFY(model.entries().constFirst().backgroundColorHex.isEmpty());
    QVERIFY(!model.data(model.index(0, EntryTableModel::SpecificationColumn), Qt::BackgroundRole).isValid());

    undoStack.redo();
    QCOMPARE(model.entries().constFirst().backgroundColorHex, QStringLiteral("#CCE5FF"));
    QVERIFY(model.clearRowBackgroundColor(0));
    QVERIFY(model.entries().constFirst().backgroundColorHex.isEmpty());
    QVERIFY(model.data(model.index(0, EntryTableModel::SpecificationColumn), Qt::BackgroundRole).canConvert<QBrush>());
}

void EntryTableModelTest::setsAndClearsMultipleRowBackgroundColors()
{
    EntryTableModel model;
    QUndoStack undoStack;
    model.setUndoStack(&undoStack);

    StatementEntry firstEntry;
    firstEntry.id = 101;
    firstEntry.specification = QStringLiteral("100*50");
    firstEntry.quantity = 10;
    firstEntry.pricePerSquareMeter = 2.15;
    firstEntry.pricePrecision = 2;
    firstEntry.formulaType = FormulaType::C;

    StatementEntry secondEntry = firstEntry;
    secondEntry.id = 102;
    secondEntry.specification = QStringLiteral("200*80");

    StatementEntry thirdEntry = firstEntry;
    thirdEntry.id = 103;
    thirdEntry.specification = QStringLiteral("300*90");

    model.setEntries({firstEntry, secondEntry, thirdEntry});

    QVERIFY(model.setRowsBackgroundColor({2, 0, 2, -1, 99}, QColor(QStringLiteral("#ffe0cc"))));
    QCOMPARE(model.entries().at(0).backgroundColorHex, QStringLiteral("#FFE0CC"));
    QVERIFY(model.entries().at(1).backgroundColorHex.isEmpty());
    QCOMPARE(model.entries().at(2).backgroundColorHex, QStringLiteral("#FFE0CC"));
    QCOMPARE(undoStack.count(), 1);

    undoStack.undo();
    QVERIFY(model.entries().at(0).backgroundColorHex.isEmpty());
    QVERIFY(model.entries().at(2).backgroundColorHex.isEmpty());

    undoStack.redo();
    QCOMPARE(model.entries().at(0).backgroundColorHex, QStringLiteral("#FFE0CC"));
    QCOMPARE(model.entries().at(2).backgroundColorHex, QStringLiteral("#FFE0CC"));

    QVERIFY(model.clearRowsBackgroundColor({0, 2}));
    QVERIFY(model.entries().at(0).backgroundColorHex.isEmpty());
    QVERIFY(model.entries().at(2).backgroundColorHex.isEmpty());
    QCOMPARE(undoStack.count(), 2);
}

void EntryTableModelTest::usesUpdatedColumnOrder()
{
    EntryTableModel model;

    QCOMPARE(EntryTableModel::FormulaColumn, 0);
    QCOMPARE(EntryTableModel::DeliveryDateColumn, 1);
    QCOMPARE(EntryTableModel::PricePerSquareMeterColumn, 5);
    QCOMPARE(model.headerData(EntryTableModel::PricePerSquareMeterColumn, Qt::Horizontal, Qt::DisplayRole).toString(), QStringLiteral("每平方单价"));
    QCOMPARE(model.headerData(EntryTableModel::FormulaColumn, Qt::Horizontal, Qt::DisplayRole).toString(), QStringLiteral("计算公式"));
}

void EntryTableModelTest::showsVerticalHeaderRowNumbers()
{
    EntryTableModel model;

    QCOMPARE(model.headerData(0, Qt::Vertical, Qt::DisplayRole).toInt(), 1);
    QCOMPARE(model.headerData(8, Qt::Vertical, Qt::DisplayRole).toInt(), 9);
    QCOMPARE(model.headerData(0, Qt::Vertical, Qt::TextAlignmentRole).toInt(), static_cast<int>(Qt::AlignCenter));
}

void EntryTableModelTest::showsFormulaGuideInHeaderTooltip()
{
    EntryTableModel model;

    const QString tooltip = model.headerData(EntryTableModel::FormulaColumn, Qt::Horizontal, Qt::ToolTipRole).toString();
    QVERIFY(tooltip.contains(QStringLiteral("A = ((长 + 宽 + 4) * (宽 + 高 + 2) * 2)")));
    QVERIFY(tooltip.contains(QStringLiteral("B = ((长 + 宽 + 4) * (宽 + 高 + 宽 + 2) * 2)")));
    QVERIFY(tooltip.contains(QStringLiteral("C = 长 * 宽 * 每平方单价 / 10000")));
    QVERIFY(tooltip.contains(QStringLiteral("手动 = 直接输入单价")));
}

QTEST_APPLESS_MAIN(EntryTableModelTest)

#include "test_entry_table_model.moc"
