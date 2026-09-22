#pragma once

#include <QList>
#include <QString>
#include <QStringList>

namespace justnbt {
QStringList nbtFileExtensions();

struct RegistryEntry {
    QString key;
    QString name;
    QString value;
};

struct AssociationPlan {
    QList<RegistryEntry> entries;
    QStringList ownKeys;
    QList<RegistryEntry> sharedValues;
};

AssociationPlan associationPlan(const QString& exePath);

bool fileAssociationSupported();
bool isFileAssociationSet(const QString& exePath);
bool setFileAssociation(bool on, const QString& exePath, QString* error = nullptr);
}
