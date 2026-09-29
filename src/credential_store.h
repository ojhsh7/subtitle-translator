#pragma once
#include <QString>
namespace Credentials {
bool save(const QString& key, QString& error);
QString load(QString& error);
bool remove(QString& error);
}
