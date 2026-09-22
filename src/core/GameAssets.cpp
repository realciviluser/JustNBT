#include "core/GameAssets.h"
#include "core/Tr.h"

#include "core/Compression.h"
#include "core/Config.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QMutex>
#include <QMutexLocker>
#include <QReadWriteLock>

#include <algorithm>
#include <vector>

namespace justnbt {
namespace {
#ifdef Q_OS_WIN
QString envPath(const char* name)
{
    return QString::fromLocal8Bit(qgetenv(name)).replace('\\', '/');
}
#endif

class Zip {
public:
    bool open(const QString& path, const QStringList& keepPrefixes);
    bool isOpen() const { return file_.isOpen(); }
    bool contains(const QString& name) const { return entries_.contains(name); }
    QByteArray read(const QString& name) const;
    QString path() const { return file_.fileName(); }

private:
    struct Entry {
        quint32 headerOffset = 0;
        quint32 compressedSize = 0;
        quint32 size = 0;
        quint16 method = 0;
    };

    mutable QFile file_;
    mutable QMutex mutex_;
    QHash<QString, Entry> entries_;
};

quint16 u16(const QByteArray& b, int at)
{
    return quint16(quint8(b[at])) | quint16(quint8(b[at + 1])) << 8;
}

quint32 u32(const QByteArray& b, int at)
{
    return quint32(quint8(b[at])) | quint32(quint8(b[at + 1])) << 8 | quint32(quint8(b[at + 2])) << 16
        | quint32(quint8(b[at + 3])) << 24;
}

bool Zip::open(const QString& path, const QStringList& keepPrefixes)
{
    entries_.clear();
    file_.close();
    file_.setFileName(path);
    if (!file_.open(QIODevice::ReadOnly))
        return false;

    const qint64 tailSize = std::min<qint64>(file_.size(), 64 * 1024 + 22);
    file_.seek(file_.size() - tailSize);
    const QByteArray tail = file_.read(tailSize);
    int eocd = -1;
    for (int i = int(tail.size()) - 22; i >= 0; --i) {
        if (u32(tail, i) == 0x06054b50) {
            eocd = i;
            break;
        }
    }
    if (eocd < 0) {
        file_.close();
        return false;
    }
    const quint32 dirSize = u32(tail, eocd + 12);
    const quint32 dirOffset = u32(tail, eocd + 16);
    if (dirOffset == 0xFFFFFFFFu) {
        file_.close();
        return false;
    }

    file_.seek(dirOffset);
    const QByteArray dir = file_.read(dirSize);
    int at = 0;
    while (at + 46 <= dir.size() && u32(dir, at) == 0x02014b50) {
        const quint16 method = u16(dir, at + 10);
        const quint32 compressedSize = u32(dir, at + 20);
        const quint32 size = u32(dir, at + 24);
        const int nameLen = u16(dir, at + 28);
        const int extraLen = u16(dir, at + 30);
        const int commentLen = u16(dir, at + 32);
        const quint32 headerOffset = u32(dir, at + 42);
        const QString name = QString::fromUtf8(dir.mid(at + 46, nameLen));
        at += 46 + nameLen + extraLen + commentLen;

        if (!keepPrefixes.isEmpty()) {
            bool wanted = false;
            for (const QString& prefix : keepPrefixes)
                wanted = wanted || name.startsWith(prefix);
            if (!wanted)
                continue;
        }
        entries_.insert(name, Entry{headerOffset, compressedSize, size, method});
    }
    return !entries_.isEmpty();
}

QByteArray Zip::read(const QString& name) const
{
    const auto it = entries_.constFind(name);
    if (it == entries_.constEnd())
        return {};

    QMutexLocker lock(&mutex_);
    if (!file_.seek(it->headerOffset))
        return {};
    const QByteArray local = file_.read(30);
    if (local.size() < 30 || u32(local, 0) != 0x04034b50)
        return {};
    const qint64 dataAt = it->headerOffset + 30 + u16(local, 26) + u16(local, 28);
    if (!file_.seek(dataAt))
        return {};
    const QByteArray raw = file_.read(it->compressedSize);
    if (raw.size() != qint64(it->compressedSize))
        return {};
    if (it->method == 0)
        return raw;
    if (it->method != 8)
        return {};
    try {
        const auto bytes = decompress({reinterpret_cast<const uint8_t*>(raw.constData()), size_t(raw.size())},
                                      Compression::Deflate);
        return QByteArray(reinterpret_cast<const char*>(bytes.data()), qsizetype(bytes.size()));
    } catch (const std::exception&) {
        return {};
    }
}

bool readVersionJson(const QString& path, MinecraftVersion& v)
{
    QFile json(path);
    if (!json.open(QIODevice::ReadOnly))
        return true;
    const QJsonObject o = QJsonDocument::fromJson(json.readAll()).object();
    if (!o.value(QStringLiteral("inheritsFrom")).toString().isEmpty())
        return false;
    for (const char* key : {"id", "version"})
        if (const QString id = o.value(QLatin1String(key)).toString(); !id.isEmpty()) {
            v.id = id;
            break;
        }
    v.assetIndex = o.value(QStringLiteral("assetIndex")).toObject().value(QStringLiteral("id")).toString();
    if (v.assetIndex.isEmpty())
        v.assetIndex = o.value(QStringLiteral("assets")).toString();
    const QDateTime released = QDateTime::fromString(o.value(QStringLiteral("releaseTime")).toString(), Qt::ISODate);
    if (released.isValid())
        v.released = released;
    if (o.contains(QStringLiteral("type")))
        v.release = o.value(QStringLiteral("type")).toString() == QLatin1String("release");
    return true;
}

QList<int> releaseNumbers(const QString& id)
{
    QList<int> numbers;
    for (const QString& part : id.split(QLatin1Char('.'))) {
        bool ok = false;
        const int n = part.toInt(&ok);
        if (!ok)
            return {};
        numbers << n;
    }
    return numbers;
}

bool betterVersion(const MinecraftVersion& a, const MinecraftVersion& b)
{
    if (a.release != b.release)
        return a.release;
    const QList<int> na = releaseNumbers(a.id), nb = releaseNumbers(b.id);
    if (a.release && !na.isEmpty() && !nb.isEmpty() && na != nb)
        return std::lexicographical_compare(nb.begin(), nb.end(), na.begin(), na.end());
    return a.released > b.released;
}
}

QList<MinecraftVersion> versionsIn(const QString& dir)
{
    QList<MinecraftVersion> found;
    const QDir versions(dir + QStringLiteral("/versions"));
    for (const QFileInfo& d : versions.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        MinecraftVersion v;
        v.jar = d.absoluteFilePath() + '/' + d.fileName() + QStringLiteral(".jar");
        if (!QFileInfo::exists(v.jar))
            continue;
        v.id = d.fileName();
        v.released = QFileInfo(v.jar).lastModified();
        v.assetsDir = dir + QStringLiteral("/assets");
        if (readVersionJson(d.absoluteFilePath() + '/' + d.fileName() + QStringLiteral(".json"), v))
            found << v;
    }
    const QDir libraries(dir + QStringLiteral("/libraries/com/mojang/minecraft"));
    for (const QFileInfo& d : libraries.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        MinecraftVersion v;
        v.jar = d.absoluteFilePath() + QStringLiteral("/minecraft-") + d.fileName() + QStringLiteral("-client.jar");
        if (!QFileInfo::exists(v.jar))
            continue;
        v.id = d.fileName();
        v.released = QFileInfo(v.jar).lastModified();
        v.assetsDir = dir + QStringLiteral("/assets");
        v.release = !v.id.contains(QLatin1Char('w')) && !v.id.contains(QLatin1Char('-'));
        readVersionJson(dir + QStringLiteral("/meta/net.minecraft/") + d.fileName() + QStringLiteral(".json"), v);
        found << v;
    }
    std::sort(found.begin(), found.end(), betterVersion);
    return found;
}

QString defaultMinecraftDir()
{
#if defined(Q_OS_WIN)
    return envPath("APPDATA") + QStringLiteral("/.minecraft");
#elif defined(Q_OS_MACOS)
    return QDir::homePath() + QStringLiteral("/Library/Application Support/minecraft");
#else
    const QString flatpak = QDir::homePath() + QStringLiteral("/.var/app/com.mojang.Minecraft/.minecraft");
    if (QFileInfo(flatpak).isDir())
        return flatpak;
    return QDir::homePath() + QStringLiteral("/.minecraft");
#endif
}

QString resolveMinecraftDir(const QString& dir)
{
    QDir d(dir);
    for (int up = 0; up < 4; ++up) {
        if (!versionsIn(d.absolutePath()).isEmpty())
            return d.absolutePath();
        if (!d.cdUp())
            break;
    }
    return dir;
}

QString minecraftDir()
{
    const QString chosen = settings().value(QStringLiteral("assets/minecraftDir")).toString();
    return chosen.isEmpty() ? defaultMinecraftDir() : chosen;
}

QString configuredMinecraftVersion()
{
    return settings().value(QStringLiteral("assets/version")).toString();
}

void setMinecraftSource(const QString& dir, const QString& version)
{
    QSettings& config = settings();
    if (dir.isEmpty())
        config.remove(QStringLiteral("assets/minecraftDir"));
    else
        config.setValue(QStringLiteral("assets/minecraftDir"), dir);
    if (version.isEmpty())
        config.remove(QStringLiteral("assets/version"));
    else
        config.setValue(QStringLiteral("assets/version"), version);
}

namespace {
QByteArray readAssetObject(const QString& assetsDir, const QString& indexId, const QString& objectName)
{
    QString indexPath = assetsDir + QStringLiteral("/indexes/") + indexId + QStringLiteral(".json");
    if (indexId.isEmpty() || !QFileInfo::exists(indexPath)) {
        const QDir indexes(assetsDir + QStringLiteral("/indexes"));
        const auto files = indexes.entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Time);
        if (files.isEmpty())
            return {};
        indexPath = files.first().absoluteFilePath();
    }
    QFile index(indexPath);
    if (!index.open(QIODevice::ReadOnly))
        return {};
    const QJsonObject objects =
        QJsonDocument::fromJson(index.readAll()).object().value(QStringLiteral("objects")).toObject();
    const QString hash = objects.value(objectName).toObject().value(QStringLiteral("hash")).toString();
    if (hash.size() < 2)
        return {};
    QFile object(assetsDir + QStringLiteral("/objects/") + hash.left(2) + '/' + hash);
    if (!object.open(QIODevice::ReadOnly))
        return {};
    return object.readAll();
}

void mergeLang(QHash<QString, QString>& into, const QByteArray& json)
{
    const QJsonObject o = QJsonDocument::fromJson(json).object();
    for (auto it = o.constBegin(); it != o.constEnd(); ++it)
        into.insert(it.key(), it.value().toString());
}

QString wantedLanguage()
{
    if (configuredLanguage() == QLatin1String("ru"))
        return QStringLiteral("ru_ru");
    if (configuredLanguage() == QLatin1String("en"))
        return QStringLiteral("en_us");
    const QString name = QLocale::system().name().toLower();
    return name.contains('_') ? name : QStringLiteral("en_us");
}
}

struct GameAssets::Impl {
    mutable QReadWriteLock lock;
    bool loaded = false;
    Zip jar;
    QString versionId;
    QString language;
    QHash<QString, QString> lang;
    mutable QHash<QString, QByteArray> textures;

    void load();
    QByteArray findTexture(const QString& name) const;
    QString textureFromModel(const QString& modelPath, int depth) const;
};

void GameAssets::Impl::load()
{
    loaded = true;
    QList<MinecraftVersion> candidates = versionsIn(resolveMinecraftDir(minecraftDir()));
    const QString wantedVersion = configuredMinecraftVersion();
    if (!wantedVersion.isEmpty())
        std::stable_partition(candidates.begin(), candidates.end(),
                              [&](const MinecraftVersion& v) { return v.id == wantedVersion; });
    const QStringList wanted{QStringLiteral("assets/minecraft/lang/"), QStringLiteral("assets/minecraft/textures/"),
                             QStringLiteral("assets/minecraft/models/"), QStringLiteral("assets/minecraft/items/")};
    MinecraftVersion version;
    for (const MinecraftVersion& candidate : std::as_const(candidates)) {
        if (!jar.open(candidate.jar, wanted))
            continue;
        if (!jar.contains(QStringLiteral("assets/minecraft/lang/en_us.json")))
            continue;
        version = candidate;
        break;
    }
    if (version.jar.isEmpty())
        return;
    versionId = version.id;

    mergeLang(lang, jar.read(QStringLiteral("assets/minecraft/lang/en_us.json")));
    language = QStringLiteral("en_us");

    const QString wantedLang = wantedLanguage();
    if (wantedLang != QLatin1String("en_us")) {
        const QByteArray json =
            readAssetObject(version.assetsDir, version.assetIndex, QStringLiteral("minecraft/lang/") + wantedLang + QStringLiteral(".json"));
        if (!json.isEmpty()) {
            mergeLang(lang, json);
            language = wantedLang;
        }
    }
}

QString texturePathOf(const QString& reference)
{
    QString rest = reference;
    const int colon = rest.indexOf(':');
    if (colon >= 0)
        rest = rest.mid(colon + 1);
    return QStringLiteral("assets/minecraft/textures/") + rest + QStringLiteral(".png");
}

QString GameAssets::Impl::textureFromModel(const QString& modelPath, int depth) const
{
    if (depth > 6 || !jar.contains(modelPath))
        return {};
    const QJsonObject model = QJsonDocument::fromJson(jar.read(modelPath)).object();
    const QJsonObject textures = model.value(QStringLiteral("textures")).toObject();
    for (const char* key : {"layer0", "all", "texture", "side", "north", "top", "particle"}) {
        const QString value = textures.value(QLatin1String(key)).toString();
        if (!value.isEmpty() && !value.startsWith('#'))
            return texturePathOf(value);
    }
    for (auto it = textures.constBegin(); it != textures.constEnd(); ++it) {
        const QString value = it.value().toString();
        if (!value.isEmpty() && !value.startsWith('#'))
            return texturePathOf(value);
    }
    const QString parent = model.value(QStringLiteral("parent")).toString();
    if (parent.isEmpty())
        return {};
    QString parentPath = parent;
    const int colon = parentPath.indexOf(':');
    if (colon >= 0)
        parentPath = parentPath.mid(colon + 1);
    return textureFromModel(QStringLiteral("assets/minecraft/models/") + parentPath + QStringLiteral(".json"),
                            depth + 1);
}

QString modelReference(const QJsonValue& value)
{
    if (value.isObject()) {
        const QJsonObject o = value.toObject();
        for (const char* key : {"model", "base"}) {
            const QJsonValue v = o.value(QLatin1String(key));
            if (v.isString())
                return v.toString();
        }
        for (auto it = o.constBegin(); it != o.constEnd(); ++it) {
            const QString found = modelReference(it.value());
            if (!found.isEmpty())
                return found;
        }
    } else if (value.isArray()) {
        for (const QJsonValue& v : value.toArray()) {
            const QString found = modelReference(v);
            if (!found.isEmpty())
                return found;
        }
    }
    return {};
}

QByteArray GameAssets::Impl::findTexture(const QString& name) const
{
    for (const char* folder : {"item", "block"}) {
        const QString path =
            QStringLiteral("assets/minecraft/textures/%1/%2.png").arg(QLatin1String(folder), name);
        if (jar.contains(path))
            return jar.read(path);
    }
    QStringList models;
    const QString definition = QStringLiteral("assets/minecraft/items/") + name + QStringLiteral(".json");
    if (jar.contains(definition)) {
        QString reference = modelReference(QJsonDocument::fromJson(jar.read(definition)).object());
        const int colon = reference.indexOf(':');
        if (colon >= 0)
            reference = reference.mid(colon + 1);
        if (!reference.isEmpty())
            models << QStringLiteral("assets/minecraft/models/") + reference + QStringLiteral(".json");
    }
    models << QStringLiteral("assets/minecraft/models/item/") + name + QStringLiteral(".json")
           << QStringLiteral("assets/minecraft/models/block/") + name + QStringLiteral(".json");
    for (const QString& model : std::as_const(models)) {
        const QString texture = textureFromModel(model, 0);
        if (!texture.isEmpty() && jar.contains(texture))
            return jar.read(texture);
    }

    for (const char* suffix : {"_side", "_top", "_front", "_0"}) {
        for (const char* folder : {"block", "item"}) {
            const QString path = QStringLiteral("assets/minecraft/textures/%1/%2%3.png")
                                     .arg(QLatin1String(folder), name, QLatin1String(suffix));
            if (jar.contains(path))
                return jar.read(path);
        }
    }
    return {};
}

GameAssets::GameAssets() : d(std::make_unique<Impl>()) {}
GameAssets::~GameAssets() = default;

GameAssets& GameAssets::instance()
{
    static GameAssets assets;
    return assets;
}

bool GameAssets::available() const
{
    QWriteLocker lock(&d->lock);
    if (!d->loaded)
        d->load();
    return d->jar.isOpen();
}

QString GameAssets::versionName() const
{
    available();
    QReadLocker lock(&d->lock);
    return d->versionId;
}

QString GameAssets::languageCode() const
{
    available();
    QReadLocker lock(&d->lock);
    return d->language;
}

QString GameAssets::jarPath() const
{
    available();
    QReadLocker lock(&d->lock);
    return d->jar.path();
}

QString GameAssets::describeSource() const
{
    if (!available())
        return Tr::tr("Minecraft Java was not found: items are shown by their ids");
    QReadLocker lock(&d->lock);
    return Tr::tr("Item names and icons: Minecraft %1 (%2)").arg(d->versionId, d->language);
}

QString GameAssets::translate(const QString& key) const
{
    available();
    QReadLocker lock(&d->lock);
    return d->lang.value(key);
}

QList<QPair<QString, QString>> GameAssets::namedIds(const QString& kind) const
{
    available();
    QReadLocker lock(&d->lock);
    const QString prefix = kind + QStringLiteral(".minecraft.");
    QList<QPair<QString, QString>> ids;
    for (auto it = d->lang.constBegin(); it != d->lang.constEnd(); ++it) {
        if (!it.key().startsWith(prefix))
            continue;
        const QString path = it.key().mid(prefix.size());
        if (!path.isEmpty() && !path.contains(QLatin1Char('.')))
            ids.append({QStringLiteral("minecraft:") + path, it.value()});
    }
    return ids;
}

QByteArray GameAssets::texturePng(const QString& id) const
{
    if (!available())
        return {};
    QString name = id;
    const int colon = name.indexOf(':');
    if (colon >= 0)
        name = name.mid(colon + 1);

    {
        QReadLocker lock(&d->lock);
        const auto it = d->textures.constFind(name);
        if (it != d->textures.constEnd())
            return *it;
    }
    QWriteLocker lock(&d->lock);
    const QByteArray png = d->findTexture(name);
    d->textures.insert(name, png);
    return png;
}

QByteArray GameAssets::textureFile(const QString& path) const
{
    if (!available())
        return {};
    const QString file = QStringLiteral("assets/minecraft/textures/") + path + QStringLiteral(".png");
    QWriteLocker lock(&d->lock);
    return d->jar.contains(file) ? d->jar.read(file) : QByteArray();
}

void GameAssets::reload()
{
    QWriteLocker lock(&d->lock);
    d->loaded = false;
    d->lang.clear();
    d->textures.clear();
    d->versionId.clear();
    d->language.clear();
}
}
