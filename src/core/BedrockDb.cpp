#include "core/BedrockDb.h"
#include "core/Tr.h"

#include <leveldb/cache.h>
#include <leveldb/db.h>
#include <leveldb/env.h>
#include <leveldb/filter_policy.h>
#include <leveldb/write_batch.h>

#include "core/Backup.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QMutex>

#include <cstdarg>
#include <stdexcept>

namespace justnbt {
namespace {
class NullLogger : public leveldb::Logger {
public:
    void Logv(const char*, std::va_list) override {}
};

leveldb::Slice slice(std::string_view s)
{
    return {s.data(), s.size()};
}
}

struct BedrockDb::Impl {
    NullLogger logger;
    std::unique_ptr<const leveldb::FilterPolicy> filter;
    std::unique_ptr<leveldb::Cache> cache;
    std::unique_ptr<leveldb::DB> db;
};

BedrockDb::BedrockDb(const QString& dbDir) : d(std::make_unique<Impl>())
{
    d->filter.reset(leveldb::NewBloomFilterPolicy(10));
    d->cache.reset(leveldb::NewLRUCache(40 << 20));
    leveldb::Options options;
    options.create_if_missing = false;
    options.filter_policy = d->filter.get();
    options.block_cache = d->cache.get();
    options.write_buffer_size = 4 << 20;
    options.block_size = 160 * 1024;
    options.compression = leveldb::kZlibRawCompression;
    options.info_log = &d->logger;

    leveldb::DB* db = nullptr;
    const leveldb::Status s = leveldb::DB::Open(options, QDir::cleanPath(dbDir).toStdString(), &db);
    if (!s.ok()) {
        const std::string msg = s.ToString();
        if (msg.find("lock") != std::string::npos)
            throw std::runtime_error(Tr::tr("The world database is busy: the world is most likely open in the game. "
                                            "Close it and try again.").toStdString());
        throw std::runtime_error(Tr::tr("Cannot open the world database: ").toStdString() + msg);
    }
    d->db.reset(db);
}

BedrockDb::~BedrockDb() = default;

namespace {
QMutex g_registryMutex;
QHash<QString, std::weak_ptr<BedrockDb>> g_registry;

QString registryKey(const QString& dbDir)
{
    const QString path = QDir::cleanPath(QDir(dbDir).absolutePath());
#ifdef Q_OS_WIN
    return path.toLower();
#else
    return path;
#endif
}
}

std::shared_ptr<BedrockDb> BedrockDb::shared(const QString& dbDir)
{
    QMutexLocker lock(&g_registryMutex);
    const QString key = registryKey(dbDir);
    if (auto db = g_registry.value(key).lock())
        return db;
    backupFolderOncePerSession(dbDir, QFileInfo(QDir(dbDir).absolutePath()).dir().dirName());
    auto db = std::make_shared<BedrockDb>(dbDir);
    g_registry.insert(key, db);
    return db;
}

bool BedrockDb::isOpenInThisProcess(const QString& dbDir)
{
    QMutexLocker lock(&g_registryMutex);
    return !g_registry.value(registryKey(dbDir)).expired();
}

std::optional<std::string> BedrockDb::get(std::string_view key) const
{
    std::string value;
    const leveldb::Status s = d->db->Get(leveldb::ReadOptions(), slice(key), &value);
    if (s.IsNotFound())
        return std::nullopt;
    if (!s.ok())
        throw std::runtime_error(Tr::tr("Database read error: ").toStdString() + s.ToString());
    return value;
}

void BedrockDb::put(std::string_view key, std::string_view value)
{
    leveldb::WriteOptions options;
    options.sync = true;
    const leveldb::Status s = d->db->Put(options, slice(key), slice(value));
    if (!s.ok())
        throw std::runtime_error(Tr::tr("Database write error: ").toStdString() + s.ToString());
}

void BedrockDb::apply(const std::vector<Change>& changes)
{
    leveldb::WriteBatch batch;
    for (const auto& [key, value] : changes) {
        if (value)
            batch.Put(key, *value);
        else
            batch.Delete(key);
    }
    leveldb::WriteOptions options;
    options.sync = true;
    const leveldb::Status s = d->db->Write(options, &batch);
    if (!s.ok())
        throw std::runtime_error(Tr::tr("Database write error: ").toStdString() + s.ToString());
}

void BedrockDb::compactAll()
{
    d->db->CompactRange(nullptr, nullptr);
}

void BedrockDb::forEachWithPrefix(std::string_view prefix,
                                  const std::function<bool(std::string_view, std::string_view)>& fn) const
{
    std::unique_ptr<leveldb::Iterator> it(d->db->NewIterator(leveldb::ReadOptions()));
    for (it->Seek(slice(prefix)); it->Valid(); it->Next()) {
        const leveldb::Slice k = it->key();
        if (!k.starts_with(slice(prefix)))
            break;
        const leveldb::Slice v = it->value();
        if (!fn({k.data(), k.size()}, {v.data(), v.size()}))
            break;
    }
    if (!it->status().ok())
        throw std::runtime_error(Tr::tr("Database read error: ").toStdString() + it->status().ToString());
}
}
