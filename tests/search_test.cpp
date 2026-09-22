#include "TestWorld.h"
#include "core/WorldSearch.h"
#include "core/Worlds.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <cstdio>

using nbt::Tag;
using nbt::TagType;
using namespace justnbt;
using namespace testworld;

namespace {
int failures = 0;

void check(bool ok, const QString& what)
{
    std::printf("%-64s %s\n", qPrintable(what), ok ? "ok" : "FAILED");
    if (!ok)
        ++failures;
}

SearchResult search(const WorldInfo& world, SearchKind kind, QStringList patterns, std::vector<ChunkRange> areas = {},
                    int maxHits = 10000)
{
    SearchQuery q;
    q.kind = kind;
    q.patterns = std::move(patterns);
    q.areas = std::move(areas);
    q.maxHits = maxHits;
    return searchWorld(world, world.dimensions.first(), q);
}

bool hasHit(const SearchResult& r, int x, int y, int z, const char* id)
{
    for (const SearchHit& h : r.hits)
        if (h.x == x && h.y == y && h.z == z && h.id == id)
            return true;
    return false;
}

const SearchHit* hitWith(const SearchResult& r, const char* id)
{
    for (const SearchHit& h : r.hits)
        if (h.id == id)
            return &h;
    return nullptr;
}

bool copyFolder(const QString& from, const QString& to)
{
    QDirIterator it(from, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString source = it.next();
        const QString target = to + '/' + QDir(from).relativeFilePath(source);
        QDir().mkpath(QFileInfo(target).absolutePath());
        if (!QFile::copy(source, target))
            return false;
    }
    return true;
}
}

int main(int argc, char** argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    QCoreApplication::setOrganizationName(QStringLiteral("JustNBT_SearchTest"));
    QCoreApplication::setApplicationName(QStringLiteral("JustNBT_SearchTest"));
    QCoreApplication app(argc, argv);
    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);

    QTemporaryDir temp;
    const QString dir = temp.filePath(QStringLiteral("world"));
    check(buildWorld(dir), QStringLiteral("test world built"));
    const auto world = probeWorld(dir);
    check(world && world->edition == Edition::Java && !world->dimensions.isEmpty(), QStringLiteral("read as a Java world"));
    if (!world) {
        std::printf("\n%d failures\n", failures + 1);
        return 1;
    }

    auto r = search(*world, SearchKind::Block, {QStringLiteral("diamond_ore")});
    check(r.hits.size() == 4, QStringLiteral("part of a name finds every kind of diamond ore (4)"));
    check(hasHit(r, 2, 4, 3, "minecraft:diamond_ore") && hasHit(r, 10, 1, 12, "minecraft:deepslate_diamond_ore"),
          QStringLiteral("  at the right places"));
    check(hasHit(r, 3 * 16 + 4, -3, 16 + 4, "minecraft:diamond_ore"), QStringLiteral("  also below Y 0"));
    check(hasHit(r, -5 * 16, 40, 2 * 16, "minecraft:diamond_ore"), QStringLiteral("  and in another region file"));
    check(r.chunks == 3 && r.failedChunks == 0, QStringLiteral("  every chunk read, none failed"));

    r = search(*world, SearchKind::Block, {QStringLiteral("minecraft:diamond_ore")});
    check(r.hits.size() == 3, QStringLiteral("a full id finds only that block (3)"));

    r = search(*world, SearchKind::Block, {QStringLiteral("diamond_ore")}, {ChunkRange{0, 0, 0, 0}});
    check(r.hits.size() == 2 && r.chunks == 1, QStringLiteral("inside a selected area only (2, one chunk read)"));

    r = search(*world, SearchKind::Block, {QStringLiteral("spawner")});
    check(r.hits.size() == 1 && r.hits[0].subject == "minecraft:zombie",
          QStringLiteral("a spawner comes with the mob it makes"));
    r = search(*world, SearchKind::Block, {QStringLiteral("minecraft:chest")});
    check(r.hits.size() == 1 && r.hits[0].name == QLatin1String("Treasure"),
          QStringLiteral("a chest comes with its custom name"));

    r = search(*world, SearchKind::Block, {QStringLiteral("stone")}, {}, 100);
    check(r.hits.size() == 100 && r.truncated, QStringLiteral("a very common block stops at the limit and says so"));

    r = search(*world, SearchKind::Block, {QStringLiteral("spawner"), QStringLiteral("deepslate_diamond_ore")});
    check(r.hits.size() == 2, QStringLiteral("several names at once"));

    r = search(*world, SearchKind::Item, {QStringLiteral("minecraft:diamond")});
    const SearchHit* chest = hitWith(r, "minecraft:chest");
    const SearchHit* dropped = hitWith(r, "minecraft:item");
    check(chest && chest->count == 8 && chest->x == 5 && chest->y == 3 && chest->z == 7,
          QStringLiteral("diamonds in a chest, also inside a shulker box (5 + 3)"));
    check(dropped && dropped->count == 2 && dropped->x == 8 && dropped->z == 8,
          QStringLiteral("diamonds lying on the ground"));
    check(!hitWith(r, "minecraft:villager"), QStringLiteral("a villager's trades are not what he carries"));
    check(r.hits.size() == 2, QStringLiteral("nothing else holds exactly diamonds (2)"));

    r = search(*world, SearchKind::Item, {QStringLiteral("diamond")});
    const SearchHit* zombie = hitWith(r, "minecraft:zombie");
    check(zombie && zombie->subject == "minecraft:diamond_sword", QStringLiteral("part of a name: the zombie's diamond sword too"));

    r = search(*world, SearchKind::Entity, {QStringLiteral("villager")});
    check(r.hits.size() == 1 && r.hits[0].subject == "minecraft:librarian" && r.hits[0].name == QLatin1String("Bob"),
          QStringLiteral("a villager comes with profession and name"));
    check(r.hits.size() == 1 && r.hits[0].x == 4 && r.hits[0].y == 64 && r.hits[0].z == 4,
          QStringLiteral("  at the block it stands in"));
    r = search(*world, SearchKind::Entity, {QStringLiteral("chicken")});
    check(r.hits.size() == 1 && r.hits[0].x == -3 && r.hits[0].source == SearchSource::Entities,
          QStringLiteral("a chicken riding a zombie is found too"));
    r = search(*world, SearchKind::Entity, {QStringLiteral("minecraft:item")});
    check(r.hits.size() == 1 && r.hits[0].subject == "minecraft:diamond", QStringLiteral("a dropped item says what it is"));

    r = search(*world, SearchKind::Poi, {QStringLiteral("minecraft:home")});
    check(r.hits.size() == 1 && r.hits[0].x == 3 && r.hits[0].y == 70 && r.hits[0].count == 1
              && r.hits[0].source == SearchSource::Poi,
          QStringLiteral("a bed as a point of interest, still free"));
    r = search(*world, SearchKind::Poi, {QStringLiteral("librarian")});
    check(r.hits.size() == 1 && r.hits[0].count == 0, QStringLiteral("a lectern that a librarian has claimed"));

    SearchQuery q;
    q.kind = SearchKind::Block;
    q.patterns = {QStringLiteral("stone")};
    const SearchResult stopped = searchWorld(*world, world->dimensions.first(), q, [](int, int) { return false; });
    check(stopped.cancelled, QStringLiteral("the search can be stopped"));

    check(idMatches("minecraft:trapped_chest", {QStringLiteral("chest")}), QStringLiteral("names: 'chest' finds trapped_chest"));
    check(!idMatches("minecraft:trapped_chest", {QStringLiteral("minecraft:chest")}),
          QStringLiteral("names: 'minecraft:chest' does not"));
    check(idMatches("diamond_ore", {QStringLiteral("minecraft:diamond_ore")}), QStringLiteral("names: ids without a namespace"));
    check(!idMatches("mymod:diamond_ore", {QStringLiteral("minecraft:diamond_ore")}),
          QStringLiteral("names: another mod's block is not vanilla's"));
    check(idMatches("minecraft:diamond_ore", {QStringLiteral("DIAMOND")}), QStringLiteral("names: patterns ignore case"));

    WorldInfo bedrock;
    bedrock.edition = Edition::Bedrock;
    check(!searchSupported(bedrock, SearchKind::Poi) && searchSupported(bedrock, SearchKind::Block),
          QStringLiteral("Bedrock: no points of interest, everything else"));
    if (argc >= 2) {
        const QString copy = temp.filePath(QStringLiteral("bedrock"));
        check(copyFolder(QString::fromLocal8Bit(argv[1]), copy), QStringLiteral("Bedrock: world copied"));
        const auto bw = probeWorld(copy);
        check(bw && bw->edition == Edition::Bedrock, QStringLiteral("Bedrock: read as a Bedrock world"));
        if (bw) {
            QElapsedTimer timer;
            timer.start();
            SearchQuery bq;
            bq.kind = SearchKind::Block;
            bq.patterns = {QStringLiteral("minecraft:bedrock")};
            bq.maxHits = 1000000;
            const SearchResult br = searchWorld(*bw, bw->dimensions.first(), bq);
            std::printf("      bedrock blocks: %zu in %d chunks, %d failed, %lld ms%s\n", br.hits.size(), br.chunks,
                        br.failedChunks, qlonglong(timer.elapsed()), qPrintable(br.error));
            check(br.error.isEmpty() && br.hits.size() >= size_t(br.chunks) * 16 * 16 / 2,
                  QStringLiteral("Bedrock: the bedrock floor is found under its chunks"));
            bool allLow = true;
            for (const SearchHit& h : br.hits)
                allLow = allLow && h.y <= -59 && h.source == SearchSource::Bedrock;
            check(allLow, QStringLiteral("Bedrock: all of it at the bottom of the world"));
            bq.kind = SearchKind::Entity;
            bq.patterns = {QStringLiteral("minecraft:")};
            const SearchResult be = searchWorld(*bw, bw->dimensions.first(), bq);
            std::printf("      entities: %zu\n", be.hits.size());
            check(be.error.isEmpty() && be.failedChunks == 0, QStringLiteral("Bedrock: entity search reads every chunk"));
        }
    }

    QDir(appData).removeRecursively();
    QDir().rmdir(QFileInfo(appData).absolutePath());
    std::printf("\n%d failures\n", failures);
    return failures == 0 ? 0 : 1;
}
