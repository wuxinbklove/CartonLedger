#include <QtTest>

#include "export/ExcelExporter.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtEndian>
#include <QXmlStreamReader>

using namespace cartonledger;

class ExportersTest : public QObject {
    Q_OBJECT

private slots:
    void exportsXlsxWorkbook();
};

namespace {

StatementEntry sampleEntry()
{
    StatementEntry entry;
    entry.deliveryDate = QDate(2026, 4, 4);
    entry.orderNumber = QStringLiteral("PO-001");
    entry.specification = QStringLiteral("100*50");
    entry.quantity = 7;
    entry.pricePerSquareMeter = 2.15;
    entry.pricePrecision = 3;
    entry.formulaType = FormulaType::Manual;
    entry.manualUnitPrice = 8.75;
    entry.manualUnitPricePrecision = 2;
    return entry;
}

QByteArray readFileBytes(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }

    return file.readAll();
}

quint16 readLittleEndian16(const QByteArray &data, int offset)
{
    return qFromLittleEndian<quint16>(reinterpret_cast<const uchar *>(data.constData() + offset));
}

quint32 readLittleEndian32(const QByteArray &data, int offset)
{
    return qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(data.constData() + offset));
}

QHash<QString, QByteArray> readStoredZipEntries(const QByteArray &archive)
{
    QHash<QString, QByteArray> entries;
    int offset = 0;

    while (offset + 4 <= archive.size()) {
        const quint32 signature = readLittleEndian32(archive, offset);
        if (signature == 0x02014b50 || signature == 0x06054b50) {
            break;
        }

        if (signature != 0x04034b50 || offset + 30 > archive.size()) {
            return {};
        }

        const quint16 compressionMethod = readLittleEndian16(archive, offset + 8);
        const quint32 compressedSize = readLittleEndian32(archive, offset + 18);
        const quint16 fileNameLength = readLittleEndian16(archive, offset + 26);
        const quint16 extraFieldLength = readLittleEndian16(archive, offset + 28);
        const int dataStart = offset + 30 + fileNameLength + extraFieldLength;
        const int dataEnd = dataStart + static_cast<int>(compressedSize);

        if (compressionMethod != 0 || dataEnd > archive.size()) {
            return {};
        }

        const QByteArray fileName = archive.mid(offset + 30, fileNameLength);
        entries.insert(QString::fromUtf8(fileName), archive.mid(dataStart, compressedSize));
        offset = dataEnd;
    }

    return entries;
}

QString attributeValue(const QXmlStreamAttributes &attributes, const QString &name)
{
    for (const QXmlStreamAttribute &attribute : attributes) {
        if (attribute.name() == name) {
            return attribute.value().toString();
        }
    }

    return {};
}

} // namespace

void ExportersTest::exportsXlsxWorkbook()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString filePath = dir.filePath(QStringLiteral("statement.xlsx"));
    QString errorMessage;
    QVERIFY(ExcelExporter::exportEntries(filePath, {sampleEntry()}, &errorMessage));
    QVERIFY2(errorMessage.isEmpty(), qPrintable(errorMessage));

    const QByteArray archiveBytes = readFileBytes(filePath);
    QVERIFY(archiveBytes.startsWith("PK"));

    const QHash<QString, QByteArray> zipEntries = readStoredZipEntries(archiveBytes);
    QVERIFY(zipEntries.contains(QStringLiteral("[Content_Types].xml")));
    QVERIFY(zipEntries.contains(QStringLiteral("_rels/.rels")));
    QVERIFY(zipEntries.contains(QStringLiteral("xl/workbook.xml")));
    QVERIFY(zipEntries.contains(QStringLiteral("xl/worksheets/sheet1.xml")));
    QVERIFY(zipEntries.contains(QStringLiteral("xl/styles.xml")));

    QString worksheetName;
    {
        QXmlStreamReader workbookXml(zipEntries.value(QStringLiteral("xl/workbook.xml")));
        while (!workbookXml.atEnd()) {
            workbookXml.readNext();
            if (workbookXml.isStartElement() && workbookXml.name() == QStringLiteral("sheet")) {
                worksheetName = attributeValue(workbookXml.attributes(), QStringLiteral("name"));
                break;
            }
        }
        QVERIFY2(!workbookXml.hasError(), qPrintable(workbookXml.errorString()));
    }
    QCOMPARE(worksheetName, QStringLiteral("对账单"));

    QStringList cellReferences;
    QStringList cellTypes;
    QStringList cellValues;
    {
        QXmlStreamReader sheetXml(zipEntries.value(QStringLiteral("xl/worksheets/sheet1.xml")));
        QString currentReference;
        QString currentType;
        QString currentValue;
        bool insideCell = false;

        while (!sheetXml.atEnd()) {
            sheetXml.readNext();
            if (sheetXml.isStartElement()) {
                if (sheetXml.name() == QStringLiteral("c")) {
                    insideCell = true;
                    currentReference = attributeValue(sheetXml.attributes(), QStringLiteral("r"));
                    currentType = attributeValue(sheetXml.attributes(), QStringLiteral("t"));
                    currentValue.clear();
                } else if (insideCell && (sheetXml.name() == QStringLiteral("v") || sheetXml.name() == QStringLiteral("t"))) {
                    currentValue = sheetXml.readElementText();
                }
            } else if (sheetXml.isEndElement() && sheetXml.name() == QStringLiteral("c") && insideCell) {
                cellReferences.append(currentReference);
                cellTypes.append(currentType);
                cellValues.append(currentValue);
                insideCell = false;
            }
        }

        QVERIFY2(!sheetXml.hasError(), qPrintable(sheetXml.errorString()));
    }

    const QStringList expectedReferences = {
        QStringLiteral("A1"),
        QStringLiteral("B1"),
        QStringLiteral("C1"),
        QStringLiteral("D1"),
        QStringLiteral("E1"),
        QStringLiteral("F1"),
        QStringLiteral("G1"),
        QStringLiteral("H1"),
        QStringLiteral("A2"),
        QStringLiteral("B2"),
        QStringLiteral("C2"),
        QStringLiteral("D2"),
        QStringLiteral("E2"),
        QStringLiteral("F2"),
        QStringLiteral("G2"),
        QStringLiteral("H2"),
    };
    QCOMPARE(cellReferences, expectedReferences);
    const QStringList expectedValues = {
        QStringLiteral("计算公式"),
        QStringLiteral("送货日期"),
        QStringLiteral("订单号"),
        QStringLiteral("规格"),
        QStringLiteral("数量"),
        QStringLiteral("每平方单价"),
        QStringLiteral("单价"),
        QStringLiteral("总价"),
        QStringLiteral("手动"),
        QStringLiteral("2026-04-04"),
        QStringLiteral("PO-001"),
        QStringLiteral("100*50"),
        QStringLiteral("7"),
        QStringLiteral("2.150"),
        QStringLiteral("8.75"),
        QStringLiteral("61.25"),
    };
    QCOMPARE(cellValues, expectedValues);

    const QStringList expectedTypes = {
        QStringLiteral("inlineStr"),
        QStringLiteral("inlineStr"),
        QStringLiteral("inlineStr"),
        QStringLiteral("inlineStr"),
        QStringLiteral("inlineStr"),
        QStringLiteral("inlineStr"),
        QStringLiteral("inlineStr"),
        QStringLiteral("inlineStr"),
        QStringLiteral("inlineStr"),
        QStringLiteral("inlineStr"),
        QStringLiteral("inlineStr"),
        QStringLiteral("inlineStr"),
        QString(),
        QString(),
        QString(),
        QString(),
    };
    QCOMPARE(cellTypes, expectedTypes);
}

QTEST_APPLESS_MAIN(ExportersTest)

#include "test_exporters.moc"