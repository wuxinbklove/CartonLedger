#include "export/ExcelExporter.h"

#include "export/EntryExportFormatter.h"

#include <QBuffer>
#include <QDataStream>
#include <QDateTime>
#include <QFile>
#include <QXmlStreamWriter>

#include <array>
#include <algorithm>

namespace cartonledger {

namespace {

constexpr quint16 kZipVersion = 20;
constexpr quint16 kUtf8Flag = 0x0800;
constexpr quint16 kStoredMethod = 0;
constexpr quint32 kLocalHeaderSignature = 0x04034b50;
constexpr quint32 kCentralDirectorySignature = 0x02014b50;
constexpr quint32 kEndOfCentralDirectorySignature = 0x06054b50;
constexpr int kMaxDecimalPlaces = 6;

const QString kSpreadsheetNamespace = QStringLiteral("http://schemas.openxmlformats.org/spreadsheetml/2006/main");
const QString kPackageRelationshipsNamespace = QStringLiteral("http://schemas.openxmlformats.org/package/2006/relationships");
const QString kDocumentRelationshipsNamespace = QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships");
const QString kContentTypesNamespace = QStringLiteral("http://schemas.openxmlformats.org/package/2006/content-types");
const QString kCorePropertiesNamespace = QStringLiteral("http://schemas.openxmlformats.org/package/2006/metadata/core-properties");
const QString kDublinCoreNamespace = QStringLiteral("http://purl.org/dc/elements/1.1/");
const QString kDublinTermsNamespace = QStringLiteral("http://purl.org/dc/terms/");
const QString kDublinTypeNamespace = QStringLiteral("http://purl.org/dc/dcmitype/");
const QString kXmlSchemaInstanceNamespace = QStringLiteral("http://www.w3.org/2001/XMLSchema-instance");
const QString kExtendedPropertiesNamespace = QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/extended-properties");
const QString kDocPropsVTypesNamespace = QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/docPropsVTypes");
const QString kWorksheetName = QStringLiteral("对账单");

struct ZipEntry {
    QString fileName;
    QByteArray data;
};

struct CentralDirectoryEntry {
    QByteArray fileName;
    quint32 crc32 = 0;
    quint32 size = 0;
    quint32 localHeaderOffset = 0;
};

template <typename Callback>
QByteArray buildXmlDocument(Callback &&callback)
{
    QByteArray data;
    QBuffer buffer(&data);
    buffer.open(QIODevice::WriteOnly);

    QXmlStreamWriter xml(&buffer);
    xml.writeStartDocument(QStringLiteral("1.0"));
    callback(xml);
    xml.writeEndDocument();

    return data;
}

std::array<quint32, 256> buildCrc32Table()
{
    std::array<quint32, 256> table = {};

    for (quint32 index = 0; index < table.size(); ++index) {
        quint32 value = index;
        for (int bit = 0; bit < 8; ++bit) {
            value = (value & 1U) != 0U ? 0xEDB88320U ^ (value >> 1U) : (value >> 1U);
        }
        table[index] = value;
    }

    return table;
}

quint32 crc32(const QByteArray &data)
{
    static const std::array<quint32, 256> table = buildCrc32Table();

    quint32 crc = 0xFFFFFFFFU;
    for (const auto byte : data) {
        const auto value = static_cast<quint8>(byte);
        crc = table[(crc ^ value) & 0xFFU] ^ (crc >> 8U);
    }

    return crc ^ 0xFFFFFFFFU;
}

QString columnName(int oneBasedColumn)
{
    QString name;
    int column = oneBasedColumn;
    while (column > 0) {
        const int remainder = (column - 1) % 26;
        name.prepend(QChar(static_cast<char>('A' + remainder)));
        column = (column - 1) / 26;
    }
    return name;
}

QString cellReference(int row, int column)
{
    return columnName(column) + QString::number(row);
}

int decimalPlaces(const QString &value)
{
    const int dotIndex = value.indexOf(QLatin1Char('.'));
    if (dotIndex < 0) {
        return 0;
    }

    const int places = static_cast<int>(value.size()) - dotIndex - 1;
    return std::clamp(places, 0, kMaxDecimalPlaces);
}

quint32 styleIndexForCell(const ExportCell &cell)
{
    if (cell.type != ExportCellType::Number) {
        return 0;
    }

    return static_cast<quint32>(2 + decimalPlaces(cell.value));
}

QString numberFormatCode(int decimals)
{
    if (decimals <= 0) {
        return QStringLiteral("0");
    }

    return QStringLiteral("0.") + QString(decimals, QLatin1Char('0'));
}

void writeInlineStringCell(QXmlStreamWriter &xml, const QString &reference, const QString &value, int styleIndex)
{
    xml.writeStartElement(QStringLiteral("c"));
    xml.writeAttribute(QStringLiteral("r"), reference);
    xml.writeAttribute(QStringLiteral("t"), QStringLiteral("inlineStr"));
    if (styleIndex > 0) {
        xml.writeAttribute(QStringLiteral("s"), QString::number(styleIndex));
    }
    xml.writeStartElement(QStringLiteral("is"));
    xml.writeTextElement(QStringLiteral("t"), value);
    xml.writeEndElement();
    xml.writeEndElement();
}

void writeNumberCell(QXmlStreamWriter &xml, const QString &reference, const QString &value, int styleIndex)
{
    xml.writeStartElement(QStringLiteral("c"));
    xml.writeAttribute(QStringLiteral("r"), reference);
    xml.writeAttribute(QStringLiteral("s"), QString::number(styleIndex));
    xml.writeTextElement(QStringLiteral("v"), value);
    xml.writeEndElement();
}

QByteArray buildContentTypesXml()
{
    return buildXmlDocument([](QXmlStreamWriter &xml) {
        xml.writeStartElement(QStringLiteral("Types"));
        xml.writeDefaultNamespace(kContentTypesNamespace);
        xml.writeEmptyElement(QStringLiteral("Default"));
        xml.writeAttribute(QStringLiteral("Extension"), QStringLiteral("rels"));
        xml.writeAttribute(QStringLiteral("ContentType"), QStringLiteral("application/vnd.openxmlformats-package.relationships+xml"));
        xml.writeEmptyElement(QStringLiteral("Default"));
        xml.writeAttribute(QStringLiteral("Extension"), QStringLiteral("xml"));
        xml.writeAttribute(QStringLiteral("ContentType"), QStringLiteral("application/xml"));
        xml.writeEmptyElement(QStringLiteral("Override"));
        xml.writeAttribute(QStringLiteral("PartName"), QStringLiteral("/xl/workbook.xml"));
        xml.writeAttribute(QStringLiteral("ContentType"), QStringLiteral("application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"));
        xml.writeEmptyElement(QStringLiteral("Override"));
        xml.writeAttribute(QStringLiteral("PartName"), QStringLiteral("/xl/worksheets/sheet1.xml"));
        xml.writeAttribute(QStringLiteral("ContentType"), QStringLiteral("application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"));
        xml.writeEmptyElement(QStringLiteral("Override"));
        xml.writeAttribute(QStringLiteral("PartName"), QStringLiteral("/xl/styles.xml"));
        xml.writeAttribute(QStringLiteral("ContentType"), QStringLiteral("application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml"));
        xml.writeEmptyElement(QStringLiteral("Override"));
        xml.writeAttribute(QStringLiteral("PartName"), QStringLiteral("/docProps/core.xml"));
        xml.writeAttribute(QStringLiteral("ContentType"), QStringLiteral("application/vnd.openxmlformats-package.core-properties+xml"));
        xml.writeEmptyElement(QStringLiteral("Override"));
        xml.writeAttribute(QStringLiteral("PartName"), QStringLiteral("/docProps/app.xml"));
        xml.writeAttribute(QStringLiteral("ContentType"), QStringLiteral("application/vnd.openxmlformats-officedocument.extended-properties+xml"));
        xml.writeEndElement();
    });
}

QByteArray buildRootRelationshipsXml()
{
    return buildXmlDocument([](QXmlStreamWriter &xml) {
        xml.writeStartElement(QStringLiteral("Relationships"));
        xml.writeDefaultNamespace(kPackageRelationshipsNamespace);
        xml.writeEmptyElement(QStringLiteral("Relationship"));
        xml.writeAttribute(QStringLiteral("Id"), QStringLiteral("rId1"));
        xml.writeAttribute(QStringLiteral("Type"), QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument"));
        xml.writeAttribute(QStringLiteral("Target"), QStringLiteral("xl/workbook.xml"));
        xml.writeEmptyElement(QStringLiteral("Relationship"));
        xml.writeAttribute(QStringLiteral("Id"), QStringLiteral("rId2"));
        xml.writeAttribute(QStringLiteral("Type"), QStringLiteral("http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties"));
        xml.writeAttribute(QStringLiteral("Target"), QStringLiteral("docProps/core.xml"));
        xml.writeEmptyElement(QStringLiteral("Relationship"));
        xml.writeAttribute(QStringLiteral("Id"), QStringLiteral("rId3"));
        xml.writeAttribute(QStringLiteral("Type"), QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/extended-properties"));
        xml.writeAttribute(QStringLiteral("Target"), QStringLiteral("docProps/app.xml"));
        xml.writeEndElement();
    });
}

QByteArray buildWorkbookXml()
{
    return buildXmlDocument([](QXmlStreamWriter &xml) {
        xml.writeStartElement(QStringLiteral("workbook"));
        xml.writeDefaultNamespace(kSpreadsheetNamespace);
        xml.writeNamespace(kDocumentRelationshipsNamespace, QStringLiteral("r"));
        xml.writeStartElement(QStringLiteral("sheets"));
        xml.writeEmptyElement(QStringLiteral("sheet"));
        xml.writeAttribute(QStringLiteral("name"), kWorksheetName);
        xml.writeAttribute(QStringLiteral("sheetId"), QStringLiteral("1"));
        xml.writeAttribute(kDocumentRelationshipsNamespace, QStringLiteral("id"), QStringLiteral("rId1"));
        xml.writeEndElement();
        xml.writeEndElement();
    });
}

QByteArray buildWorkbookRelationshipsXml()
{
    return buildXmlDocument([](QXmlStreamWriter &xml) {
        xml.writeStartElement(QStringLiteral("Relationships"));
        xml.writeDefaultNamespace(kPackageRelationshipsNamespace);
        xml.writeEmptyElement(QStringLiteral("Relationship"));
        xml.writeAttribute(QStringLiteral("Id"), QStringLiteral("rId1"));
        xml.writeAttribute(QStringLiteral("Type"), QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet"));
        xml.writeAttribute(QStringLiteral("Target"), QStringLiteral("worksheets/sheet1.xml"));
        xml.writeEmptyElement(QStringLiteral("Relationship"));
        xml.writeAttribute(QStringLiteral("Id"), QStringLiteral("rId2"));
        xml.writeAttribute(QStringLiteral("Type"), QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles"));
        xml.writeAttribute(QStringLiteral("Target"), QStringLiteral("styles.xml"));
        xml.writeEndElement();
    });
}

QByteArray buildStylesXml()
{
    return buildXmlDocument([](QXmlStreamWriter &xml) {
        xml.writeStartElement(QStringLiteral("styleSheet"));
        xml.writeDefaultNamespace(kSpreadsheetNamespace);

        xml.writeStartElement(QStringLiteral("numFmts"));
        xml.writeAttribute(QStringLiteral("count"), QString::number(kMaxDecimalPlaces + 1));
        for (int precision = 0; precision <= kMaxDecimalPlaces; ++precision) {
            xml.writeEmptyElement(QStringLiteral("numFmt"));
            xml.writeAttribute(QStringLiteral("numFmtId"), QString::number(164 + precision));
            xml.writeAttribute(QStringLiteral("formatCode"), numberFormatCode(precision));
        }
        xml.writeEndElement();

        xml.writeStartElement(QStringLiteral("fonts"));
        xml.writeAttribute(QStringLiteral("count"), QStringLiteral("2"));
        xml.writeStartElement(QStringLiteral("font"));
        xml.writeEmptyElement(QStringLiteral("sz"));
        xml.writeAttribute(QStringLiteral("val"), QStringLiteral("11"));
        xml.writeEmptyElement(QStringLiteral("name"));
        xml.writeAttribute(QStringLiteral("val"), QStringLiteral("Calibri"));
        xml.writeEndElement();
        xml.writeStartElement(QStringLiteral("font"));
        xml.writeEmptyElement(QStringLiteral("b"));
        xml.writeEmptyElement(QStringLiteral("sz"));
        xml.writeAttribute(QStringLiteral("val"), QStringLiteral("11"));
        xml.writeEmptyElement(QStringLiteral("name"));
        xml.writeAttribute(QStringLiteral("val"), QStringLiteral("Calibri"));
        xml.writeEndElement();
        xml.writeEndElement();

        xml.writeStartElement(QStringLiteral("fills"));
        xml.writeAttribute(QStringLiteral("count"), QStringLiteral("2"));
        xml.writeStartElement(QStringLiteral("fill"));
        xml.writeEmptyElement(QStringLiteral("patternFill"));
        xml.writeAttribute(QStringLiteral("patternType"), QStringLiteral("none"));
        xml.writeEndElement();
        xml.writeStartElement(QStringLiteral("fill"));
        xml.writeEmptyElement(QStringLiteral("patternFill"));
        xml.writeAttribute(QStringLiteral("patternType"), QStringLiteral("gray125"));
        xml.writeEndElement();
        xml.writeEndElement();

        xml.writeStartElement(QStringLiteral("borders"));
        xml.writeAttribute(QStringLiteral("count"), QStringLiteral("1"));
        xml.writeStartElement(QStringLiteral("border"));
        xml.writeEmptyElement(QStringLiteral("left"));
        xml.writeEmptyElement(QStringLiteral("right"));
        xml.writeEmptyElement(QStringLiteral("top"));
        xml.writeEmptyElement(QStringLiteral("bottom"));
        xml.writeEmptyElement(QStringLiteral("diagonal"));
        xml.writeEndElement();
        xml.writeEndElement();

        xml.writeStartElement(QStringLiteral("cellStyleXfs"));
        xml.writeAttribute(QStringLiteral("count"), QStringLiteral("1"));
        xml.writeEmptyElement(QStringLiteral("xf"));
        xml.writeAttribute(QStringLiteral("numFmtId"), QStringLiteral("0"));
        xml.writeAttribute(QStringLiteral("fontId"), QStringLiteral("0"));
        xml.writeAttribute(QStringLiteral("fillId"), QStringLiteral("0"));
        xml.writeAttribute(QStringLiteral("borderId"), QStringLiteral("0"));
        xml.writeEndElement();

        xml.writeStartElement(QStringLiteral("cellXfs"));
        xml.writeAttribute(QStringLiteral("count"), QString::number(kMaxDecimalPlaces + 3));
        xml.writeEmptyElement(QStringLiteral("xf"));
        xml.writeAttribute(QStringLiteral("numFmtId"), QStringLiteral("0"));
        xml.writeAttribute(QStringLiteral("fontId"), QStringLiteral("0"));
        xml.writeAttribute(QStringLiteral("fillId"), QStringLiteral("0"));
        xml.writeAttribute(QStringLiteral("borderId"), QStringLiteral("0"));
        xml.writeAttribute(QStringLiteral("xfId"), QStringLiteral("0"));
        xml.writeEmptyElement(QStringLiteral("xf"));
        xml.writeAttribute(QStringLiteral("numFmtId"), QStringLiteral("0"));
        xml.writeAttribute(QStringLiteral("fontId"), QStringLiteral("1"));
        xml.writeAttribute(QStringLiteral("fillId"), QStringLiteral("0"));
        xml.writeAttribute(QStringLiteral("borderId"), QStringLiteral("0"));
        xml.writeAttribute(QStringLiteral("xfId"), QStringLiteral("0"));
        xml.writeAttribute(QStringLiteral("applyFont"), QStringLiteral("1"));
        for (int precision = 0; precision <= kMaxDecimalPlaces; ++precision) {
            xml.writeEmptyElement(QStringLiteral("xf"));
            xml.writeAttribute(QStringLiteral("numFmtId"), QString::number(164 + precision));
            xml.writeAttribute(QStringLiteral("fontId"), QStringLiteral("0"));
            xml.writeAttribute(QStringLiteral("fillId"), QStringLiteral("0"));
            xml.writeAttribute(QStringLiteral("borderId"), QStringLiteral("0"));
            xml.writeAttribute(QStringLiteral("xfId"), QStringLiteral("0"));
            xml.writeAttribute(QStringLiteral("applyNumberFormat"), QStringLiteral("1"));
        }
        xml.writeEndElement();

        xml.writeStartElement(QStringLiteral("cellStyles"));
        xml.writeAttribute(QStringLiteral("count"), QStringLiteral("1"));
        xml.writeEmptyElement(QStringLiteral("cellStyle"));
        xml.writeAttribute(QStringLiteral("name"), QStringLiteral("Normal"));
        xml.writeAttribute(QStringLiteral("xfId"), QStringLiteral("0"));
        xml.writeAttribute(QStringLiteral("builtinId"), QStringLiteral("0"));
        xml.writeEndElement();

        xml.writeEmptyElement(QStringLiteral("dxfs"));
        xml.writeAttribute(QStringLiteral("count"), QStringLiteral("0"));
        xml.writeEmptyElement(QStringLiteral("tableStyles"));
        xml.writeAttribute(QStringLiteral("count"), QStringLiteral("0"));
        xml.writeAttribute(QStringLiteral("defaultTableStyle"), QStringLiteral("TableStyleMedium2"));
        xml.writeAttribute(QStringLiteral("defaultPivotStyle"), QStringLiteral("PivotStyleLight16"));

        xml.writeEndElement();
    });
}

QByteArray buildWorksheetXml(const QStringList &headers, const QVector<ExportRow> &rows)
{
    return buildXmlDocument([&headers, &rows](QXmlStreamWriter &xml) {
        xml.writeStartElement(QStringLiteral("worksheet"));
        xml.writeDefaultNamespace(kSpreadsheetNamespace);
        xml.writeEmptyElement(QStringLiteral("dimension"));
        xml.writeAttribute(QStringLiteral("ref"), QStringLiteral("A1:%1%2").arg(columnName(headers.size())).arg(rows.size() + 1));
        xml.writeStartElement(QStringLiteral("sheetData"));

        xml.writeStartElement(QStringLiteral("row"));
        xml.writeAttribute(QStringLiteral("r"), QStringLiteral("1"));
        for (int column = 0; column < headers.size(); ++column) {
            writeInlineStringCell(xml, cellReference(1, column + 1), headers.at(column), 1);
        }
        xml.writeEndElement();

        for (int rowIndex = 0; rowIndex < rows.size(); ++rowIndex) {
            const ExportRow &row = rows.at(rowIndex);
            const int rowNumber = rowIndex + 2;
            xml.writeStartElement(QStringLiteral("row"));
            xml.writeAttribute(QStringLiteral("r"), QString::number(rowNumber));

            for (int column = 0; column < row.size(); ++column) {
                const ExportCell &cell = row.at(column);
                const QString reference = cellReference(rowNumber, column + 1);
                if (cell.type == ExportCellType::Number) {
                    writeNumberCell(xml, reference, cell.value, static_cast<int>(styleIndexForCell(cell)));
                } else {
                    writeInlineStringCell(xml, reference, cell.value, 0);
                }
            }

            xml.writeEndElement();
        }

        xml.writeEndElement();
        xml.writeEndElement();
    });
}

QByteArray buildAppPropertiesXml()
{
    return buildXmlDocument([](QXmlStreamWriter &xml) {
        xml.writeStartElement(QStringLiteral("Properties"));
        xml.writeDefaultNamespace(kExtendedPropertiesNamespace);
        xml.writeNamespace(kDocPropsVTypesNamespace, QStringLiteral("vt"));
        xml.writeTextElement(QStringLiteral("Application"), QStringLiteral("Carton Ledger"));
        xml.writeEndElement();
    });
}

QByteArray buildCorePropertiesXml()
{
    const QString timestamp = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    return buildXmlDocument([&timestamp](QXmlStreamWriter &xml) {
        xml.writeStartElement(QStringLiteral("cp:coreProperties"));
        xml.writeNamespace(kCorePropertiesNamespace, QStringLiteral("cp"));
        xml.writeNamespace(kDublinCoreNamespace, QStringLiteral("dc"));
        xml.writeNamespace(kDublinTermsNamespace, QStringLiteral("dcterms"));
        xml.writeNamespace(kDublinTypeNamespace, QStringLiteral("dcmitype"));
        xml.writeNamespace(kXmlSchemaInstanceNamespace, QStringLiteral("xsi"));
        xml.writeTextElement(QStringLiteral("dc:creator"), QStringLiteral("Carton Ledger"));
        xml.writeTextElement(QStringLiteral("cp:lastModifiedBy"), QStringLiteral("Carton Ledger"));
        xml.writeStartElement(QStringLiteral("dcterms:created"));
        xml.writeAttribute(QStringLiteral("xsi:type"), QStringLiteral("dcterms:W3CDTF"));
        xml.writeCharacters(timestamp);
        xml.writeEndElement();
        xml.writeStartElement(QStringLiteral("dcterms:modified"));
        xml.writeAttribute(QStringLiteral("xsi:type"), QStringLiteral("dcterms:W3CDTF"));
        xml.writeCharacters(timestamp);
        xml.writeEndElement();
        xml.writeEndElement();
    });
}

bool writeBytes(QFile &file, const QByteArray &data, QString *errorMessage)
{
    if (file.write(data) != data.size()) {
        if (errorMessage != nullptr) {
            *errorMessage = file.errorString();
        }
        return false;
    }

    return true;
}

bool writeXlsxArchive(QFile &file, const QVector<ZipEntry> &entries, QString *errorMessage)
{
    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);

    QVector<CentralDirectoryEntry> directoryEntries;
    directoryEntries.reserve(entries.size());

    for (const ZipEntry &entry : entries) {
        const QByteArray fileName = entry.fileName.toUtf8();
        const quint32 dataSize = static_cast<quint32>(entry.data.size());
        const quint32 entryCrc32 = crc32(entry.data);
        const quint32 localHeaderOffset = static_cast<quint32>(file.pos());

        stream << kLocalHeaderSignature
               << kZipVersion
               << kUtf8Flag
               << kStoredMethod
               << quint16(0)
               << quint16(0)
               << entryCrc32
               << dataSize
               << dataSize
               << static_cast<quint16>(fileName.size())
               << quint16(0);

        if (stream.status() != QDataStream::Ok
            || !writeBytes(file, fileName, errorMessage)
            || !writeBytes(file, entry.data, errorMessage)) {
            if (errorMessage != nullptr && errorMessage->isEmpty()) {
                *errorMessage = file.errorString();
            }
            return false;
        }

        directoryEntries.append({fileName, entryCrc32, dataSize, localHeaderOffset});
    }

    const quint32 centralDirectoryOffset = static_cast<quint32>(file.pos());

    for (const CentralDirectoryEntry &entry : directoryEntries) {
        stream << kCentralDirectorySignature
               << kZipVersion
               << kZipVersion
               << kUtf8Flag
               << kStoredMethod
               << quint16(0)
               << quint16(0)
               << entry.crc32
               << entry.size
               << entry.size
               << static_cast<quint16>(entry.fileName.size())
               << quint16(0)
               << quint16(0)
               << quint16(0)
               << quint16(0)
               << quint32(0)
               << entry.localHeaderOffset;

        if (stream.status() != QDataStream::Ok || !writeBytes(file, entry.fileName, errorMessage)) {
            if (errorMessage != nullptr && errorMessage->isEmpty()) {
                *errorMessage = file.errorString();
            }
            return false;
        }
    }

    const quint32 centralDirectorySize = static_cast<quint32>(file.pos()) - centralDirectoryOffset;
    stream << kEndOfCentralDirectorySignature
           << quint16(0)
           << quint16(0)
           << static_cast<quint16>(directoryEntries.size())
           << static_cast<quint16>(directoryEntries.size())
           << centralDirectorySize
           << centralDirectoryOffset
           << quint16(0);

    if (stream.status() != QDataStream::Ok) {
        if (errorMessage != nullptr) {
            *errorMessage = file.errorString();
        }
        return false;
    }

    return true;
}

} // namespace

bool ExcelExporter::exportEntries(const QString &filePath, const QVector<StatementEntry> &entries, QString *errorMessage)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorMessage != nullptr) {
            *errorMessage = file.errorString();
        }
        return false;
    }

    const QStringList headers = exportColumnHeaders();
    const QVector<ExportRow> rows = buildExportRows(entries);
    const QVector<ZipEntry> archiveEntries = {
        {QStringLiteral("[Content_Types].xml"), buildContentTypesXml()},
        {QStringLiteral("_rels/.rels"), buildRootRelationshipsXml()},
        {QStringLiteral("docProps/app.xml"), buildAppPropertiesXml()},
        {QStringLiteral("docProps/core.xml"), buildCorePropertiesXml()},
        {QStringLiteral("xl/workbook.xml"), buildWorkbookXml()},
        {QStringLiteral("xl/_rels/workbook.xml.rels"), buildWorkbookRelationshipsXml()},
        {QStringLiteral("xl/styles.xml"), buildStylesXml()},
        {QStringLiteral("xl/worksheets/sheet1.xml"), buildWorksheetXml(headers, rows)},
    };

    if (!writeXlsxArchive(file, archiveEntries, errorMessage)) {
        return false;
    }

    file.close();
    if (file.error() != QFileDevice::NoError) {
        if (errorMessage != nullptr) {
            *errorMessage = file.errorString();
        }
        return false;
    }

    return true;
}

} // namespace cartonledger