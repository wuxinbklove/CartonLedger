#pragma once

#include <QSqlDatabase>
#include <QString>

namespace cartonledger {

class DatabaseManager {
public:
    bool open(QString *errorMessage = nullptr);
    void close();
    QSqlDatabase database() const;
    QString databasePath() const;

private:
    bool initializeSchema(QString *errorMessage = nullptr);

    QString m_connectionName = QStringLiteral("carton_ledger_connection");
};

} // namespace cartonledger
