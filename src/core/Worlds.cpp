#include "core/Tr.h"
#include "core/Worlds.h"

#include "core/Backup.h"
#include "core/BedrockDb.h"
#include "core/NbtFile.h"

#include <QCollator>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTextStream>

#include <algorithm>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace justnbt {
namespace {
const QString kOverworld = QStringLiteral("minecraft:overworld");
const QString kNether = QStringLiteral("minecraft:the_nether");
const QString kEnd = QStringLiteral("minecraft:the_end");

#ifdef Q_OS_WIN
QString envPath(const char* name)
{
    return QDir::fromNativeSeparators(qEnvironmentVariable(name));
}
#endif

QString dimensionLabel(const QString& id)
{
    if (id == kOverworld)
        return Tr::tr("Overworld");
    if (id == kNether)
        return Tr::tr("Nether");
    if (id == kEnd)
        return Tr::tr("The End");
    return id;
}

void addDimension(QList<Dimension>& dims, const QString& id, const QString& path)
{
    if (!QFileInfo(path).isDir())
        return;
    for (const Dimension& d : dims)
        if (d.id == id)
            return;
    dims << Dimension{id, dimensionLabel(id), QDir(path).absolutePath(), -1};
}

void addNamespacedDimensions(QList<Dimension>& dims, const QDir& world)
{
    const QDir root(world.filePath(QStringLiteral("dimensions")));
    for (const QString& ns : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        const QDir nsDir(root.filePath(ns));
        for (const QString& id : nsDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
            addDimension(dims, ns + ':' + id, nsDir.filePath(id));
    }
}

QString readLevelName(const QString& serverProperties)
{
    QFile f(serverProperties);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    QTextStream in(&f);
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.startsWith(QLatin1String("level-name")) && line.contains('=')) {
            const QString value = line.section('=', 1).trimmed();
            return value.isEmpty() ? QStringLiteral("world") : value;
        }
    }
    return QStringLiteral("world");
}

void sortByLabel(QList<WorldFile>& files)
{
    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(files.begin(), files.end(),
              [&](const WorldFile& a, const WorldFile& b) { return collator.compare(a.label, b.label) < 0; });
}

QList<WorldFile> collectFiles(const QString& group, const QString& dir, const QStringList& filters)
{
    QList<WorldFile> files;
    const QDir base(dir);
    QDirIterator it(dir, filters, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        files << WorldFile{group, base.relativeFilePath(path), path, {}};
    }
    sortByLabel(files);
    return files;
}

bool isSingleNbt(std::string_view value)
{
    try {
        size_t used = 0;
        nbt::read({reinterpret_cast<const uint8_t*>(value.data()), value.size()}, nbt::Endian::Little, &used);
        return used == value.size();
    } catch (const nbt::ParseError&) {
        return false;
    }
}

QList<WorldFile> bedrockRecords(const WorldInfo& world)
{
    const QString dbDir = world.path + QStringLiteral("/db");
    const QString infoGroup = Tr::tr("World database");
    if (!QFileInfo(dbDir).isDir())
        return {};
    if (isWorldInUse(world))
        return {WorldFile{infoGroup, Tr::tr("The world is open in the game: close it to see the players and the data"), {}, {}}};

    QList<WorldFile> players, data, maps, villages, structures, other;
    try {
        const auto dbPtr = BedrockDb::shared(dbDir);
        const BedrockDb& db = *dbPtr;

        auto add = [&](QList<WorldFile>& list, const QString& group, const QString& label, std::string_view key,
                       std::string_view value) {
            if (isSingleNbt(value))
                list << WorldFile{group, label, dbDir, QByteArray(key.data(), qsizetype(key.size()))};
        };

        if (auto v = db.get("~local_player"))
            add(players, Tr::tr("Players"), Tr::tr("Local player (~local_player)"), "~local_player", *v);
        db.forEachWithPrefix("player_", [&](std::string_view k, std::string_view v) {
            const QString key = QString::fromUtf8(k.data(), qsizetype(k.size()));
            const bool server = key.startsWith(QLatin1String("player_server_"));
            add(players, Tr::tr("Players"),
                server ? Tr::tr("Player ") + key.mid(14) : Tr::tr("Link to player ") + key.mid(7), k, v);
            return true;
        });

        for (const char* key : {"AutonomousEntities", "BiomeData", "mobevents", "Overworld", "Nether", "TheEnd",
                                "portals", "schedulerWT", "scoreboard", "dimension0", "dimension1", "dimension2"}) {
            if (auto v = db.get(key))
                add(data, Tr::tr("Data"), QString::fromLatin1(key), key, *v);
        }

        struct Family {
            const char* prefix;
            QList<WorldFile>* list;
            QString group;
        };
        const Family families[] = {
            {"map_", &maps, Tr::tr("Maps")},
            {"VILLAGE_", &villages, Tr::tr("Villages")},
            {"structuretemplate_", &structures, Tr::tr("Structures")},
            {"tickingarea_", &other, Tr::tr("Other")},
        };
        for (const Family& f : families) {
            db.forEachWithPrefix(f.prefix, [&](std::string_view k, std::string_view v) {
                add(*f.list, f.group, keyToText(QByteArray(k.data(), qsizetype(k.size()))), k, v);
                return true;
            });
        }
    } catch (const std::exception& e) {
        return {WorldFile{infoGroup, QString::fromUtf8(e.what()), {}, {}}};
    }

    QList<WorldFile> all;
    for (QList<WorldFile>* list : {&players, &data, &maps, &villages, &structures, &other}) {
        if (list != &players)
            sortByLabel(*list);
        all += *list;
    }
    return all;
}

std::optional<WorldInfo> probeWorldFolder(const QString& dirPath)
{
    const QDir dir(dirPath);
    WorldInfo w;
    w.path = dir.absolutePath();
    w.folderName = dir.dirName();
    w.edition = dir.exists(QStringLiteral("db")) ? Edition::Bedrock : Edition::Java;
    w.lastPlayed = QFileInfo(dir.filePath(QStringLiteral("level.dat"))).lastModified();

    try {
        const auto file = NbtFile::load(dir.filePath(QStringLiteral("level.dat")));
        const nbt::Tag* data = w.edition == Edition::Java ? file->root->child("Data") : file->root.get();
        if (data) {
            if (const auto* n = data->child("LevelName"); n && n->type == nbt::TagType::String)
                w.name = QString::fromStdString(n->string);
            if (const auto* lp = data->child("LastPlayed"); lp && lp->type == nbt::TagType::Long && lp->integer > 0) {
                w.lastPlayed = w.edition == Edition::Java ? QDateTime::fromMSecsSinceEpoch(lp->integer)
                                                          : QDateTime::fromSecsSinceEpoch(lp->integer);
            }
        }
    } catch (const std::exception&) {
    }

    if (w.edition == Edition::Bedrock) {
        QFile txt(dir.filePath(QStringLiteral("levelname.txt")));
        if (txt.open(QIODevice::ReadOnly)) {
            const QString s = QString::fromUtf8(txt.readAll()).trimmed();
            if (!s.isEmpty())
                w.name = s;
        }
    }

    const QString icon = dir.filePath(w.edition == Edition::Java ? QStringLiteral("icon.png")
                                                                 : QStringLiteral("world_icon.jpeg"));
    if (QFile::exists(icon))
        w.iconPath = icon;

    if (w.edition == Edition::Java) {
        const QDir parent(QFileInfo(w.path).absolutePath());
        if (parent.exists(QStringLiteral("bukkit.yml")))
            w.serverKind = QStringLiteral("Paper/Spigot");
        else if (parent.exists(QStringLiteral("server.properties")))
            w.serverKind = Tr::tr("Server");

        const QString netherSibling = parent.filePath(w.folderName + QStringLiteral("_nether"));
        const QString endSibling = parent.filePath(w.folderName + QStringLiteral("_the_end"));

        if (QFileInfo(dir.filePath(QStringLiteral("dimensions/minecraft/overworld"))).isDir()
            || QFileInfo(dir.filePath(QStringLiteral("players/data"))).isDir()) {
            w.layout = JavaLayout::Modern;
            addDimension(w.dimensions, kOverworld, dir.filePath(QStringLiteral("dimensions/minecraft/overworld")));
            addDimension(w.dimensions, kNether, dir.filePath(QStringLiteral("dimensions/minecraft/the_nether")));
            addDimension(w.dimensions, kEnd, dir.filePath(QStringLiteral("dimensions/minecraft/the_end")));
        } else {
            const bool bukkit = QFile::exists(netherSibling + QStringLiteral("/level.dat"))
                || QFile::exists(endSibling + QStringLiteral("/level.dat"));
            w.layout = bukkit ? JavaLayout::BukkitLegacy : JavaLayout::Legacy;
            addDimension(w.dimensions, kOverworld, w.path);
            if (bukkit) {
                for (const QString& s : {netherSibling, endSibling})
                    if (QFile::exists(s + QStringLiteral("/level.dat")))
                        w.siblingFolders << QDir(s).absolutePath();
                addDimension(w.dimensions, kNether, netherSibling + QStringLiteral("/DIM-1"));
                addDimension(w.dimensions, kEnd, endSibling + QStringLiteral("/DIM1"));
            } else {
                addDimension(w.dimensions, kNether, dir.filePath(QStringLiteral("DIM-1")));
                addDimension(w.dimensions, kEnd, dir.filePath(QStringLiteral("DIM1")));
            }
        }
        addNamespacedDimensions(w.dimensions, dir);
    }

    if (w.edition == Edition::Bedrock) {
        const Dimension bedrockDims[] = {{kOverworld, dimensionLabel(kOverworld), w.path, 0},
                                         {kNether, dimensionLabel(kNether), w.path, 1},
                                         {kEnd, dimensionLabel(kEnd), w.path, 2}};
        for (const Dimension& d : bedrockDims)
            w.dimensions << d;
    }

    if (w.name.isEmpty())
        w.name = w.folderName;
    return w;
}
}

QString WorldInfo::lockFilePath() const
{
    return edition == Edition::Java ? path + QStringLiteral("/session.lock") : path + QStringLiteral("/db/LOCK");
}

QString WorldInfo::description() const
{
    if (edition == Edition::Bedrock)
        return QStringLiteral("Bedrock");
    QString s = QStringLiteral("Java");
    if (layout == JavaLayout::Modern)
        s += Tr::tr(" · 26.1+ layout");
    if (!serverKind.isEmpty())
        s += QStringLiteral(" · ") + serverKind;
    return s;
}

QStringList defaultJavaRoots()
{
    QStringList roots;
#if defined(Q_OS_WIN)
    roots << envPath("APPDATA") + QStringLiteral("/.minecraft/saves");
#elif defined(Q_OS_MACOS)
    roots << QDir::homePath() + QStringLiteral("/Library/Application Support/minecraft/saves");
#else
    roots << QDir::homePath() + QStringLiteral("/.minecraft/saves")
          << QDir::homePath() + QStringLiteral("/.var/app/com.mojang.Minecraft/.minecraft/saves");
#endif
    return roots;
}

QStringList defaultBedrockRoots()
{
    QStringList roots;
#ifdef Q_OS_WIN
    const QString local = envPath("LOCALAPPDATA");
    const QString roaming = envPath("APPDATA");
    for (const char* pkg : {"Microsoft.MinecraftUWP_8wekyb3d8bbwe", "Microsoft.MinecraftWindowsBeta_8wekyb3d8bbwe"})
        roots << local + QStringLiteral("/Packages/") + pkg + QStringLiteral("/LocalState/games/com.mojang/minecraftWorlds");
    for (const char* base : {"Minecraft Bedrock", "Minecraft Bedrock Preview"}) {
        QDir users(roaming + '/' + base + QStringLiteral("/Users"));
        for (const QString& user : users.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
            roots << users.filePath(user) + QStringLiteral("/games/com.mojang/minecraftWorlds");
    }
#else
    roots << QDir::homePath() + QStringLiteral("/.local/share/mcpelauncher/games/com.mojang/minecraftWorlds");
#endif
    return roots;
}

std::optional<WorldInfo> probeWorld(const QString& dirPath)
{
    const QDir dir(dirPath);
    if (dir.exists(QStringLiteral("level.dat")))
        return probeWorldFolder(dir.absolutePath());

    if (dir.exists(QStringLiteral("server.properties"))) {
        const QString level = readLevelName(dir.filePath(QStringLiteral("server.properties")));
        if (!level.isEmpty() && QFile::exists(dir.filePath(level + QStringLiteral("/level.dat"))))
            return probeWorldFolder(dir.filePath(level));
    }
    return std::nullopt;
}

QList<WorldInfo> scanRoot(const QString& root)
{
    QList<WorldInfo> found;
    const QDir dir(root);
    for (const QString& sub : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (auto w = probeWorld(dir.filePath(sub)))
            found << *w;
    }

    QSet<QString> merged;
    for (const WorldInfo& w : found)
        for (const QString& s : w.siblingFolders)
            merged.insert(s);

    QList<WorldInfo> worlds;
    QSet<QString> seen;
    for (const WorldInfo& w : found) {
        if (merged.contains(w.path) || seen.contains(w.path))
            continue;
        seen.insert(w.path);
        worlds << w;
    }
    std::sort(worlds.begin(), worlds.end(),
              [](const WorldInfo& a, const WorldInfo& b) { return a.lastPlayed > b.lastPlayed; });
    return worlds;
}

QHash<QString, QString> playerNames(const WorldInfo& world)
{
    QHash<QString, QString> names;
    QDir dir(world.path);
    for (int up = 0; up < 2 && dir.cdUp(); ++up) {
        QFile f(dir.filePath(QStringLiteral("usercache.json")));
        if (!f.open(QIODevice::ReadOnly))
            continue;
        const QJsonArray arr = QJsonDocument::fromJson(f.readAll()).array();
        for (const QJsonValue& v : arr) {
            const QJsonObject o = v.toObject();
            const QString uuid = o.value(QStringLiteral("uuid")).toString().toLower();
            if (!uuid.isEmpty() && !names.contains(uuid))
                names.insert(uuid, o.value(QStringLiteral("name")).toString());
        }
    }
    return names;
}

QList<WorldFile> editableFiles(const WorldInfo& world)
{
    QList<WorldFile> files;
    const QDir dir(world.path);

    const QString worldGroup = Tr::tr("World");
    files << WorldFile{worldGroup, QStringLiteral("level.dat"), dir.filePath(QStringLiteral("level.dat")), {}};
    if (world.edition == Edition::Bedrock)
        return files + bedrockRecords(world);
    for (const QString& sibling : world.siblingFolders)
        files << WorldFile{worldGroup, QDir(sibling).dirName() + QStringLiteral("/level.dat"),
                           sibling + QStringLiteral("/level.dat"), {}};

    const auto names = playerNames(world);
    const QDir playersDir(dir.filePath(world.layout == JavaLayout::Modern ? QStringLiteral("players/data")
                                                                          : QStringLiteral("playerdata")));
    QList<WorldFile> players;
    for (const QFileInfo& fi : playersDir.entryInfoList({QStringLiteral("*.dat")}, QDir::Files)) {
        const QString uuid = fi.completeBaseName().toLower();
        const QString name = names.value(uuid);
        players << WorldFile{Tr::tr("Players"),
                             name.isEmpty() ? uuid : name + QStringLiteral("  (") + uuid + ')', fi.absoluteFilePath(), {}};
    }
    sortByLabel(players);
    files += players;

    const QString dataDir = dir.filePath(QStringLiteral("data"));
    QList<WorldFile> data, maps;
    for (WorldFile f : collectFiles(Tr::tr("Data"), dataDir, {QStringLiteral("*.dat")})) {
        if (f.label.startsWith(QLatin1String("minecraft/maps/")) || f.label.startsWith(QLatin1String("map_"))) {
            f.group = Tr::tr("Maps");
            f.label = QFileInfo(f.label).fileName();
            maps << f;
        } else {
            if (f.label.startsWith(QLatin1String("minecraft/")))
                f.label = f.label.mid(10);
            data << f;
        }
    }
    files += data;

    for (const Dimension& d : world.dimensions) {
        const QString dimData = d.path + QStringLiteral("/data");
        if (QDir(dimData) == QDir(dataDir))
            continue;
        for (WorldFile f : collectFiles(Tr::tr("Data — ") + d.label, dimData, {QStringLiteral("*.dat")})) {
            if (f.label.startsWith(QLatin1String("minecraft/")))
                f.label = f.label.mid(10);
            files << f;
        }
    }

    files += maps;
    files += collectFiles(Tr::tr("Structures"), dir.filePath(QStringLiteral("generated")),
                          {QStringLiteral("*.nbt")});
    return files;
}

bool isLockedByGame(const QString& lockPath)
{
    const QFileInfo fi(lockPath);
    if (fi.fileName() == QLatin1String("LOCK") && BedrockDb::isOpenInThisProcess(fi.absolutePath()))
        return false;
    return isFileLocked(lockPath);
}

bool isFileLocked(const QString& path)
{
    if (!QFile::exists(path))
        return false;
#ifdef Q_OS_WIN
    HANDLE h = CreateFileW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(path).utf16()), GENERIC_READ, 0,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return GetLastError() == ERROR_SHARING_VIOLATION;
    CloseHandle(h);
    return false;
#else
    const int fd = ::open(QFile::encodeName(path).constData(), O_RDWR);
    if (fd < 0)
        return false;
    struct flock fl {};
    fl.l_type = F_WRLCK;
    fl.l_whence = SEEK_SET;
    const bool locked = fcntl(fd, F_GETLK, &fl) == 0 && fl.l_type != F_UNLCK;
    ::close(fd);
    return locked;
#endif
}
}
