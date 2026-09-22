#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

class QLocalServer;

class SingleInstance : public QObject {
    Q_OBJECT

public:
    explicit SingleInstance(const QString& key, QObject* parent = nullptr);
    ~SingleInstance() override;

    bool handOver(const QStringList& paths);

    QString serverName() const { return name_; }

signals:
    void pathsReceived(const QStringList& paths);

private:
    bool listen();
    void acceptConnection();
    void readMessage(class QLocalSocket* socket);

    QString name_;
    QLocalServer* server_ = nullptr;
};
