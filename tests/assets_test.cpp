#include "core/GameAssets.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <cstdio>

namespace {
int failures = 0;

void check(bool ok, const char* what)
{
    std::printf("%-60s %s\n", what, ok ? "ok" : "FAILED");
    if (!ok)
        ++failures;
}

void write(const QString& path, const QByteArray& data = {})
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write(data);
}

QStringList ids(const QList<justnbt::MinecraftVersion>& versions)
{
    QStringList out;
    for (const auto& v : versions)
        out << v.id;
    return out;
}
}

int main(int argc, char** argv)
{
    QCoreApplication::setOrganizationName(QStringLiteral("JustNBT_AssetsTest"));
    QCoreApplication::setApplicationName(QStringLiteral("JustNBT_AssetsTest"));
    QCoreApplication app(argc, argv);
    QTemporaryDir temp;

    const QString official = temp.filePath(QStringLiteral("dot_minecraft"));
    write(official + "/versions/1.20.1/1.20.1.jar");
    write(official + "/versions/1.20.1/1.20.1.json",
          R"({"id":"1.20.1","type":"release","releaseTime":"2023-06-12T13:25:51+00:00","assetIndex":{"id":"5"}})");
    write(official + "/versions/23w45a/23w45a.jar");
    write(official + "/versions/23w45a/23w45a.json",
          R"({"id":"23w45a","type":"snapshot","releaseTime":"2023-11-08T12:00:00+00:00"})");
    write(official + "/versions/fabric-loader-1.20.1/fabric-loader-1.20.1.jar");
    write(official + "/versions/fabric-loader-1.20.1/fabric-loader-1.20.1.json", R"({"inheritsFrom":"1.20.1"})");
    const auto o = justnbt::versionsIn(official);
    check(ids(o) == QStringList({"1.20.1", "23w45a"}), "official: the release first, the snapshot after, no Fabric");
    check(!o.isEmpty() && o[0].assetIndex == "5" && o[0].assetsDir == official + "/assets",
          "  the asset index and the assets folder");

    const QString prism = temp.filePath(QStringLiteral("PrismLauncher"));
    const QString lib = prism + "/libraries/com/mojang/minecraft/";
    write(lib + "1.21.11/minecraft-1.21.11-client.jar");
    write(prism + "/meta/net.minecraft/1.21.11.json",
          R"({"version":"1.21.11","type":"release","releaseTime":"2025-12-09T12:23:30+00:00","assetIndex":{"id":"29"}})");
    write(lib + "26.2/minecraft-26.2-client.jar");
    write(lib + "26.1-rc1/minecraft-26.1-rc1-client.jar");
    write(lib + "1.12.2/readme.txt");
    write(prism + "/instances/MYSERVER/.minecraft/options.txt");
    const auto p = justnbt::versionsIn(prism);
    check(ids(p).size() == 3 && ids(p).last() == "26.1-rc1", "Prism: three versions, the release candidate last");
    check(ids(p).contains("1.21.11") && ids(p).contains("26.2"), "  1.21.11 from its meta file, 26.2 by its name");
    for (const auto& v : p)
        if (v.id == "1.21.11")
            check(v.assetIndex == "29" && v.release, "  1.21.11: asset index 29, a release");

    check(QDir(justnbt::resolveMinecraftDir(prism + "/instances/MYSERVER/.minecraft")) == QDir(prism),
          "an instance folder leads to the launcher folder");
    check(QDir(justnbt::resolveMinecraftDir(official)) == QDir(official), "a .minecraft folder stays itself");
    check(justnbt::versionsIn(temp.filePath("nothing")).isEmpty(), "a folder without Minecraft: no versions");

    check(justnbt::minecraftDir() == justnbt::defaultMinecraftDir(), "nothing chosen: the default .minecraft");
    justnbt::setMinecraftSource(prism, QStringLiteral("1.21.11"));
    check(justnbt::minecraftDir() == prism && justnbt::configuredMinecraftVersion() == "1.21.11",
          "Prism and 1.21.11 are kept in the config");
    justnbt::setMinecraftSource({}, {});
    check(justnbt::minecraftDir() == justnbt::defaultMinecraftDir() && justnbt::configuredMinecraftVersion().isEmpty(),
          "  and forgotten again");

    if (argc >= 2) {
        justnbt::setMinecraftSource(QString::fromLocal8Bit(argv[1]), argc >= 3 ? QString::fromLocal8Bit(argv[2]) : QString());
        auto& assets = justnbt::GameAssets::instance();
        assets.reload();
        std::printf("      %s\n      %s\n", qPrintable(assets.describeSource()), qPrintable(assets.jarPath()));
        check(assets.available(), "the real installation opens");
        check(!assets.translate(QStringLiteral("item.minecraft.diamond_sword")).isEmpty(), "  item names");
        check(!assets.texturePng(QStringLiteral("minecraft:diamond_sword")).isEmpty(), "  item pictures");
        check(!assets.textureFile(QStringLiteral("gui/sprites/hud/heart/full")).isEmpty(), "  the hearts");
        justnbt::setMinecraftSource({}, {});
    }

    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir(appData).removeRecursively();
    QDir().rmdir(QFileInfo(appData).absolutePath());
    std::printf("\n%d failures\n", failures);
    return failures == 0 ? 0 : 1;
}
