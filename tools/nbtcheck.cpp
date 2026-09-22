#include "core/BedrockDb.h"
#include "core/GameAssets.h"
#include "core/ItemInfo.h"
#include "core/Maintenance.h"
#include "core/MapRender.h"
#include "core/Region.h"
#include "core/WorldEdit.h"
#include "core/WorldSearch.h"
#include "core/NbtFile.h"
#include "core/Worlds.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QImage>
#include <QSet>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QMap>

#include <cstdio>
#include <cstring>
#include <map>
#include <random>

using namespace justnbt;

static bool checkFile(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        std::printf("SKIP %s (cannot open)\n", qPrintable(path));
        return true;
    }
    const QByteArray bytes = f.readAll();
    const std::span<const uint8_t> data(reinterpret_cast<const uint8_t*>(bytes.constData()), size_t(bytes.size()));

    try {
        NbtFile file;
        file.path = path;
        file.root = NbtFile::parse(data, file.format);
        const auto original = decompress(data, file.format.compression);
        const auto written = file.encode(false);
        const bool same = original == written;
        std::printf("%s %s [%s]\n", same ? "OK  " : "DIFF", qPrintable(path), qPrintable(describe(file.format)));
        return same;
    } catch (const std::exception& e) {
        std::printf("FAIL %s: %s\n", qPrintable(path), e.what());
        return false;
    }
}

static int listWorlds(const QStringList& roots)
{
    for (const QString& root : roots) {
        QList<WorldInfo> worlds = scanRoot(root);
        if (auto single = probeWorld(root))
            worlds.prepend(*single);
        for (const WorldInfo& w : worlds) {
            std::printf("WORLD %s  [%s]\n  %s\n", qPrintable(w.name), qPrintable(w.description()), qPrintable(w.path));
            for (const Dimension& d : w.dimensions)
                std::printf("  dim %-22s %s\n", qPrintable(d.id), qPrintable(d.path));
            QMap<QString, QStringList> groups;
            for (const WorldFile& f : editableFiles(w))
                groups[f.group] << f.label;
            for (auto it = groups.cbegin(); it != groups.cend(); ++it)
                std::printf("  %-26s %4lld  e.g. %s\n", qPrintable(it.key()), qlonglong(it.value().size()),
                            qPrintable(it.value().first()));
        }
    }
    return 0;
}

static std::pair<size_t, QByteArray> dbDigest(const BedrockDb& db)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    size_t count = 0;
    db.forEachWithPrefix("", [&](std::string_view k, std::string_view v) {
        const quint32 kl = quint32(k.size()), vl = quint32(v.size());
        hash.addData(QByteArrayView(reinterpret_cast<const char*>(&kl), 4));
        hash.addData(QByteArrayView(k.data(), qsizetype(k.size())));
        hash.addData(QByteArrayView(reinterpret_cast<const char*>(&vl), 4));
        hash.addData(QByteArrayView(v.data(), qsizetype(v.size())));
        ++count;
        return true;
    });
    return {count, hash.result().toHex().left(16)};
}

static int checkDb(const QString& worldDir)
{
    const auto world = probeWorld(worldDir);
    if (!world || world->edition != Edition::Bedrock) {
        std::printf("not a Bedrock world: %s\n", qPrintable(worldDir));
        return 1;
    }
    const QString dbDir = world->path + QStringLiteral("/db");
    int bad = 0;

    QMap<QString, int> groups;
    for (const WorldFile& f : editableFiles(*world)) {
        groups[f.group]++;
        if (f.dbKey.isEmpty())
            continue;
        try {
            const auto rec = NbtFile::loadDbRecord(dbDir, f.dbKey);
            const BedrockDb db(dbDir);
            const auto raw = db.get({f.dbKey.constData(), size_t(f.dbKey.size())});
            const auto enc = rec->encode(false);
            const bool same = raw && raw->size() == enc.size() && std::equal(enc.begin(), enc.end(), raw->begin(),
                                  [](uint8_t a, char b) { return a == uint8_t(b); });
            if (!same) {
                ++bad;
                std::printf("DIFF %s\n", qPrintable(keyToText(f.dbKey)));
            }
        } catch (const std::exception& e) {
            ++bad;
            std::printf("FAIL %s: %s\n", qPrintable(keyToText(f.dbKey)), e.what());
        }
    }
    for (auto it = groups.cbegin(); it != groups.cend(); ++it)
        std::printf("  %-12s %d\n", qPrintable(it.key()), it.value());

    try {
        BedrockDb db(dbDir);
        const auto before = dbDigest(db);
        db.compactAll();
        const auto after = dbDigest(db);
        std::printf("records %zu, digest %s -> after compaction %zu, %s\n", before.first, before.second.constData(),
                    after.first, after.second.constData());
        if (before != after)
            ++bad;

        if (auto v = db.get("~local_player")) {
            NbtFormat fmt;
            auto root = NbtFile::parse({reinterpret_cast<const uint8_t*>(v->data()), v->size()}, fmt);
            auto tag = std::make_unique<nbt::Tag>(nbt::TagType::String, "JustNBTCheck");
            tag->string = "проверка ✓";
            root->append(std::move(tag));
            std::vector<uint8_t> out;
            nbt::write(*root, nbt::Endian::Little, out);
            db.put("~local_player", {reinterpret_cast<const char*>(out.data()), out.size()});
        }
    } catch (const std::exception& e) {
        std::printf("FAIL db: %s\n", e.what());
        return 1;
    }
    try {
        const BedrockDb db(dbDir);
        if (auto v = db.get("~local_player")) {
            NbtFormat fmt;
            auto root = NbtFile::parse({reinterpret_cast<const uint8_t*>(v->data()), v->size()}, fmt);
            const auto* t = root->child("JustNBTCheck");
            const bool ok = t && t->string == "проверка ✓";
            std::printf("write/reopen: %s\n", ok ? "OK" : "FAILED");
            bad += ok ? 0 : 1;
        }
    } catch (const std::exception& e) {
        std::printf("FAIL reopen: %s\n", e.what());
        return 1;
    }
    std::printf("%d problems\n", bad);
    return bad == 0 ? 0 : 1;
}

static int renderTest(const QString& png, const QStringList& regions)
{
    QSet<QString> unknown;
    QElapsedTimer total;
    total.start();
    std::shared_ptr<RenderedTile> last;
    for (const QString& path : regions) {
        QElapsedTimer t;
        t.start();
        last = renderRegion(path, RenderOptions{});
        std::printf("%-16s %5lld ms  chunks %4d  failed %d  legacy %d  names %zu  %s\n",
                    qPrintable(QFileInfo(path).fileName()), qlonglong(t.elapsed()), last->chunks, last->failedChunks,
                    last->legacyChunks, last->names.size(), last->firstError.c_str());
        for (const auto& u : last->unknownBlocks)
            unknown.insert(QString::fromStdString(u));
    }
    std::printf("total %lld ms for %lld regions\n", qlonglong(total.elapsed()), qlonglong(regions.size()));
    if (!unknown.isEmpty()) {
        QStringList list(unknown.begin(), unknown.end());
        list.sort();
        std::printf("unknown blocks (%lld): %s\n", qlonglong(list.size()), qPrintable(list.join(QStringLiteral(", "))));
    }
    if (last) {
        QImage img(reinterpret_cast<const uchar*>(last->pixels.data()), RenderedTile::kSize, RenderedTile::kSize,
                   QImage::Format_ARGB32);
        img.copy().save(png);
    }
    return 0;
}

static void dumpTag(const nbt::Tag& t, int depth, int maxDepth)
{
    std::printf("%*s%s %s", depth * 2, "", nbt::typeName(t.type), t.name.c_str());
    if (nbt::isInteger(t.type))
        std::printf(" = %lld", static_cast<long long>(t.integer));
    else if (nbt::isFloating(t.type))
        std::printf(" = %g", t.floating);
    else if (t.type == nbt::TagType::String)
        std::printf(" = \"%s\"", t.string.c_str());
    else if (nbt::isArray(t.type))
        std::printf(" [%zu]", t.arraySize());
    else
        std::printf(" (%zu)", t.children.size());
    std::printf("\n");
    if (depth >= maxDepth)
        return;
    size_t shown = 0;
    for (const auto& c : t.children) {
        if (t.type == nbt::TagType::List && ++shown > 3) {
            std::printf("%*s...\n", (depth + 1) * 2, "");
            break;
        }
        dumpTag(*c, depth + 1, maxDepth);
    }
}

static int bedrockStats(const QString& dbDir)
{
    BedrockDb db(dbDir);
    QMap<QString, int> counts;
    QMap<int, int> finalized;
    QMap<int, int> subVersions;
    db.forEachWithPrefix("", [&](std::string_view k, std::string_view v) {
        int tagPos = -1;
        if (k.size() == 9 || k.size() == 10)
            tagPos = 8;
        else if (k.size() == 13 || k.size() == 14)
            tagPos = 12;
        if (tagPos < 0) {
            counts[QStringLiteral("other")]++;
            return true;
        }
        const int tag = uint8_t(k[size_t(tagPos)]);
        const bool dim0 = tagPos == 8;
        counts[QStringLiteral("%1 tag %2").arg(dim0 ? "ow" : "dim").arg(tag)]++;
        if (tag == 54 && v.size() >= 4 && dim0)
            finalized[int(uint8_t(v[0]))]++;
        if (tag == 47 && !v.empty() && dim0)
            subVersions[int(uint8_t(v[0]))]++;
        return true;
    });
    for (auto it = counts.cbegin(); it != counts.cend(); ++it)
        std::printf("%-16s %d\n", qPrintable(it.key()), it.value());
    for (auto it = finalized.cbegin(); it != finalized.cend(); ++it)
        std::printf("overworld FinalizedState %d: %d chunks\n", it.key(), it.value());
    for (auto it = subVersions.cbegin(); it != subVersions.cend(); ++it)
        std::printf("overworld subchunk version %d: %d\n", it.key(), it.value());

    QSet<QPair<int, int>> present;
    std::vector<int> xs, zs;
    db.forEachWithPrefix("", [&](std::string_view k, std::string_view) {
        if (k.size() == 9 && uint8_t(k[8]) == 44) {
            int x, z;
            std::memcpy(&x, k.data(), 4);
            std::memcpy(&z, k.data() + 4, 4);
            present.insert({x, z});
            xs.push_back(x);
            zs.push_back(z);
        }
        return true;
    });
    if (!xs.empty()) {
        std::nth_element(xs.begin(), xs.begin() + ptrdiff_t(xs.size() / 2), xs.end());
        std::nth_element(zs.begin(), zs.begin() + ptrdiff_t(zs.size() / 2), zs.end());
        const int mx = xs[xs.size() / 2], mz = zs[zs.size() / 2];
        for (int z = mz - 12; z <= mz + 12; ++z) {
            for (int x = mx - 40; x <= mx + 40; ++x)
                std::printf("%c", present.contains({x, z}) ? '#' : '.');
            std::printf("\n");
        }
    }
    return 0;
}

static int sharedTest(const QString& worldDir)
{
    const QString dbDir = worldDir + QStringLiteral("/db");
    int bad = 0;
    auto check = [&](bool ok, const char* what) {
        std::printf("%s %s\n", ok ? "OK  " : "FAIL", what);
        bad += ok ? 0 : 1;
    };
    try {
        auto source = makeBedrockTileSource(worldDir, 0);
        check(!source->listTiles().empty(), "map source lists tiles");
        check(BedrockDb::isOpenInThisProcess(dbDir), "db registered as open by us");
        check(isFileLocked(dbDir + QStringLiteral("/LOCK")), "LOCK is held (by us)");
        check(!isLockedByGame(dbDir + QStringLiteral("/LOCK")), "our own lock is not reported as the game");
        auto rec = NbtFile::loadDbRecord(dbDir, QByteArrayLiteral("~local_player"));
        check(rec != nullptr, "record loads while the map holds the db");
        rec->save(QStringLiteral("shared-test"));
        check(true, "record saves while the map holds the db");
        source.reset();
        check(!BedrockDb::isOpenInThisProcess(dbDir), "db closed when the last user is gone");
        check(!isFileLocked(dbDir + QStringLiteral("/LOCK")), "LOCK released");
    } catch (const std::exception& e) {
        std::printf("FAIL exception: %s\n", e.what());
        ++bad;
    }
    std::printf("%d problems\n", bad);
    return bad == 0 ? 0 : 1;
}

static int chunkTest(const QString& regionPath)
{
    int bad = 0;
    auto check = [&](bool ok, const QString& what) {
        std::printf("%s %s\n", ok ? "OK  " : "FAIL", qPrintable(what));
        bad += ok ? 0 : 1;
    };
    auto snapshot = [&] {
        std::map<int, std::vector<uint8_t>> m;
        const RegionFile r(regionPath);
        for (int i = 0; i < 1024; ++i)
            if (r.hasChunk(i % 32, i / 32))
                m[i] = r.chunkData(i % 32, i / 32);
        return m;
    };
    const RegionFile first(regionPath);
    std::vector<int> present;
    for (int i = 0; i < 1024; ++i)
        if (first.hasChunk(i % 32, i / 32))
            present.push_back(i);
    if (present.size() < 3) {
        std::printf("need a region with at least 3 chunks\n");
        return 1;
    }
    const int rx = first.position().x, rz = first.position().z;
    std::mt19937 rng(12345);

    auto step = [&](int index, size_t size, const QString& what) {
        const auto before = snapshot();
        const int cx = rx * 32 + index % 32, cz = rz * 32 + index / 32;
        auto file = NbtFile::loadRegionChunk(regionPath, cx, cz);
        if (nbt::Tag* old = file->root->child("JustNBTTest"))
            file->root->take(size_t(file->root->indexOf(old)));
        if (size > 0) {
            auto blob = std::make_unique<nbt::Tag>(nbt::TagType::ByteArray, "JustNBTTest");
            blob->bytes.resize(size);
            for (auto& b : blob->bytes)
                b = int8_t(rng());
            file->root->append(std::move(blob));
        }
        const auto expected = file->encode(false);
        file->save(QStringLiteral("chunk-test"));

        const auto after = snapshot();
        bool othersSame = before.size() == after.size();
        for (const auto& [i, data] : before)
            if (i != index && (!after.count(i) || after.at(i) != data))
                othersSame = false;
        check(othersSame, what + QStringLiteral(": other %1 chunks unchanged").arg(before.size() - 1));
        check(after.count(index) && after.at(index) == expected, what + QStringLiteral(": edited chunk reads back"));
        const QString mcc = QFileInfo(regionPath).absolutePath() + QStringLiteral("/c.%1.%2.mcc").arg(cx).arg(cz);
        const RegionFile r(regionPath);
        const bool isExternal = r.compressionType(index % 32, index / 32) & 0x80;
        check(isExternal == QFile::exists(mcc), what + (isExternal ? QStringLiteral(": stored in external .mcc")
                                                                   : QStringLiteral(": no stray .mcc file")));
        return isExternal;
    };

    step(present[0], 16, QStringLiteral("small edit"));
    step(present[1], 200 * 1024, QStringLiteral("grow to 200 KB (moves to the end)"));
    check(step(present[2], 1200 * 1024, QStringLiteral("grow to 1.2 MB")), QStringLiteral("1.2 MB chunk is external"));
    step(present[2], 0, QStringLiteral("shrink back"));
    step(present[1], 0, QStringLiteral("shrink back"));
    step(present[0], 0, QStringLiteral("remove test tag"));
    std::printf("%d problems\n", bad);
    return bad == 0 ? 0 : 1;
}

static int bedrockChunkTest(const QString& worldDir)
{
    const QString dbDir = worldDir + QStringLiteral("/db");
    int bad = 0;
    auto check = [&](bool ok, const QString& what) {
        std::printf("%s %s\n", ok ? "OK  " : "FAIL", qPrintable(what));
        bad += ok ? 0 : 1;
    };
    try {
        int bestX = 0, bestZ = 0;
        size_t best = 0;
        BedrockDb::shared(dbDir)->forEachWithPrefix("digp", [&](std::string_view k, std::string_view v) {
            if (k.size() == 12 && v.size() / 8 > best) {
                best = v.size() / 8;
                std::memcpy(&bestX, k.data() + 4, 4);
                std::memcpy(&bestZ, k.data() + 8, 4);
            }
            return true;
        });
        std::printf("chunk %d, %d with %zu entities\n", bestX, bestZ, best);
        if (best < 2) {
            std::printf("need a chunk with at least 2 entities\n");
            return 1;
        }

        auto file = NbtFile::loadBedrockChunk(dbDir, 0, bestX, bestZ);
        nbt::Tag* entities = file->root->child("Entities");
        nbt::Tag* blockEntities = file->root->child("BlockEntities");
        check(entities && entities->children.size() == best, QStringLiteral("all entities loaded"));
        const size_t blockEntityCount = blockEntities->children.size();

        std::string prefix(8, '\0');
        std::memcpy(prefix.data(), &bestX, 4);
        std::memcpy(prefix.data() + 4, &bestZ, 4);
        const auto beBefore = BedrockDb::shared(dbDir)->get(prefix + char(49));
        file->save(QStringLiteral("chunk-test"));
        check(BedrockDb::shared(dbDir)->get(prefix + char(49)) == beBefore, QStringLiteral("block entities unchanged by a plain save"));

        auto mark = std::make_unique<nbt::Tag>(nbt::TagType::String, "JustNBTTest");
        mark->string = "проверка";
        entities->children[0]->append(std::move(mark));
        auto removed = entities->take(1);
        file->save(QStringLiteral("chunk-test"));

        auto again = NbtFile::loadBedrockChunk(dbDir, 0, bestX, bestZ);
        nbt::Tag* e2 = again->root->child("Entities");
        check(e2->children.size() == best - 1, QStringLiteral("deleted entity is gone"));
        bool marked = false;
        for (const auto& c : e2->children)
            if (const nbt::Tag* t = c->child("JustNBTTest"); t && t->string == "проверка")
                marked = true;
        check(marked, QStringLiteral("edited entity kept the change"));
        check(again->root->child("BlockEntities")->children.size() == blockEntityCount,
              QStringLiteral("block entities still there"));

        e2->append(removed->clone());
        bool refused = false;
        try {
            again->save(QStringLiteral("chunk-test"));
        } catch (const std::exception&) {
            refused = true;
        }
        check(refused, QStringLiteral("adding an entity is refused"));
    } catch (const std::exception& e) {
        std::printf("FAIL exception: %s\n", e.what());
        ++bad;
    }
    std::printf("%d problems\n", bad);
    return bad == 0 ? 0 : 1;
}

static int bedrockChests(const QString& worldDir)
{
    const QString dbDir = worldDir + QStringLiteral("/db");
    std::vector<std::pair<int, int>> found;
    size_t records = 0;
    BedrockDb::shared(dbDir)->forEachWithPrefix("", [&](std::string_view k, std::string_view v) {
        if (k.size() != 9 || uint8_t(k[8]) != 49)
            return true;
        ++records;
        if (std::string_view(v).find("Chest") != std::string_view::npos) {
            int x, z;
            std::memcpy(&x, k.data(), 4);
            std::memcpy(&z, k.data() + 4, 4);
            found.emplace_back(x, z);
        }
        return true;
    });
    std::printf("overworld block entity records: %zu, with a chest: %zu\n", records, found.size());
    if (found.empty())
        return 0;
    const auto [x, z] = found.front();
    std::printf("chunk %d, %d:\n", x, z);
    const auto file = NbtFile::loadBedrockChunk(dbDir, 0, x, z);
    dumpTag(*file->root, 0, 4);
    return 0;
}

static int bedrockSectionsTest(const QString& worldDir)
{
    const QString dbDir = worldDir + QStringLiteral("/db");
    int bad = 0;
    auto check = [&](bool ok, const QString& what) {
        std::printf("%s %s\n", ok ? "OK  " : "FAIL", qPrintable(what));
        bad += ok ? 0 : 1;
    };
    try {
        const auto db = BedrockDb::shared(dbDir);
        std::vector<std::pair<int, int>> chunks;
        std::map<std::string, std::string> before;
        db->forEachWithPrefix("", [&](std::string_view k, std::string_view v) {
            if (k.size() == 9 && uint8_t(k[8]) == 44) {
                int x, z;
                std::memcpy(&x, k.data(), 4);
                std::memcpy(&z, k.data() + 4, 4);
                chunks.emplace_back(x, z);
            }
            if (k.size() == 10 && uint8_t(k[8]) == 47)
                before.emplace(std::string(k), std::string(v));
            return true;
        });
        std::printf("%zu chunks, %zu sub-chunk records\n", chunks.size(), before.size());

        size_t sections = 0, failed = 0;
        for (const auto& [x, z] : chunks) {
            try {
                auto f = NbtFile::loadBedrockChunk(dbDir, 0, x, z);
                sections += f->root->child("Sections")->children.size();
                f->save(QStringLiteral("sections-test"));
            } catch (const std::exception& e) {
                if (failed++ < 3)
                    std::printf("  chunk %d,%d: %s\n", x, z, e.what());
            }
        }
        check(failed == 0, QStringLiteral("all %1 chunks load and save (%2 sections)").arg(chunks.size()).arg(sections));
        size_t changed = 0;
        db->forEachWithPrefix("", [&](std::string_view k, std::string_view v) {
            if (k.size() == 10 && uint8_t(k[8]) == 47) {
                auto it = before.find(std::string(k));
                if (it == before.end() || it->second != v)
                    ++changed;
            }
            return true;
        });
        check(changed == 0, QStringLiteral("unchanged save rewrote no sub-chunk (%1 differ)").arg(changed));

        const auto [x, z] = chunks.front();
        auto f = NbtFile::loadBedrockChunk(dbDir, 0, x, z);
        nbt::Tag* secs = f->root->child("Sections");
        nbt::Tag* top = secs->children.back().get();
        const int y = int(top->child("Y")->integer);
        nbt::Tag* entry = top->child("layers")->children[0]->child("palette")->children[0].get();
        const std::string oldName = entry->child("name")->string;
        entry->child("name")->string = "minecraft:gold_block";
        f->save(QStringLiteral("sections-test"));

        auto g = NbtFile::loadBedrockChunk(dbDir, 0, x, z);
        const nbt::Tag* top2 = g->root->child("Sections")->children.back().get();
        check(top2->child("layers")->children[0]->child("palette")->children[0]->child("name")->string
                  == "minecraft:gold_block",
              QStringLiteral("chunk %1,%2 section Y=%3: %4 -> gold_block").arg(x).arg(z).arg(y).arg(QString::fromStdString(oldName)));
        size_t others = 0;
        std::string editedKey;
        db->forEachWithPrefix("", [&](std::string_view k, std::string_view v) {
            if (k.size() == 10 && uint8_t(k[8]) == 47) {
                auto it = before.find(std::string(k));
                if (it == before.end() || it->second != v) {
                    int kx, kz;
                    std::memcpy(&kx, k.data(), 4);
                    std::memcpy(&kz, k.data() + 4, 4);
                    if (kx == x && kz == z && int8_t(k[9]) == y)
                        editedKey = std::string(k);
                    else
                        ++others;
                }
            }
            return true;
        });
        check(!editedKey.empty(), QStringLiteral("the edited sub-chunk record changed"));
        check(others == 0, QStringLiteral("no other sub-chunk record changed (%1)").arg(others));
        auto source = makeBedrockTileSource(worldDir, 0);
        RenderOptions opts;
        opts.yLimit = RenderOptions{}.yLimit;
        const auto tile = source->tile({x >> 5, z >> 5}, opts);
        check(tile->failedChunks == 0, QStringLiteral("map renders the edited tile without errors"));
    } catch (const std::exception& e) {
        std::printf("FAIL exception: %s\n", e.what());
        ++bad;
    }
    std::printf("%d problems\n", bad);
    return bad == 0 ? 0 : 1;
}

static int bedrockEncodeCheck(const QString& worldDir)
{
    const auto db = BedrockDb::shared(worldDir + QStringLiteral("/db"));
    size_t total = 0, identical = 0, legacy = 0, roundTripFailed = 0;
    QMap<QString, int> differences;
    db->forEachWithPrefix("", [&](std::string_view k, std::string_view v) {
        const bool sub = (k.size() == 10 && uint8_t(k[8]) == 47) || (k.size() == 14 && uint8_t(k[12]) == 47);
        if (!sub)
            return true;
        ++total;
        const std::string value(v);
        auto t = decodeBedrockSubChunk(value, int8_t(k.back()));
        if (!t) {
            ++legacy;
            return true;
        }
        const std::string again = encodeBedrockSubChunk(*t);
        if (again == value) {
            ++identical;
        } else {
            auto t2 = decodeBedrockSubChunk(again, int8_t(k.back()));
            std::vector<uint8_t> a, b;
            nbt::write(*t, nbt::Endian::Little, a);
            nbt::write(*t2, nbt::Endian::Little, b);
            if (a != b)
                ++roundTripFailed;
            differences[QStringLiteral("v%1 hdr%2 -> hdr%3")
                            .arg(uint8_t(value[0]))
                            .arg(uint8_t(value.size() > 3 ? value[3] : 0))
                            .arg(uint8_t(again.size() > 3 ? again[3] : 0))]++;
        }
        return true;
    });
    std::printf("sub-chunks %zu: byte-identical %zu, legacy %zu, same content but other bytes %zu, "
                "content lost %zu\n",
                total, identical, legacy, total - identical - legacy - roundTripFailed, roundTripFailed);
    for (auto it = differences.cbegin(); it != differences.cend(); ++it)
        std::printf("  %s: %d\n", qPrintable(it.key()), it.value());
    return roundTripFailed == 0 ? 0 : 1;
}

static int cleanupTest(const QString& scratch)
{
    int bad = 0;
    auto check = [&](bool ok, const QString& what) {
        std::printf("%s %s\n", ok ? "OK  " : "FAIL", qPrintable(what));
        bad += ok ? 0 : 1;
    };
    const QString root = scratch + QStringLiteral("/backups");
    const QDateTime now = QDateTime::currentDateTime();
    auto makeCopy = [&](const QString& sub, double daysAgo, const QString& name, bool folder) {
        const QString stamp = now.addSecs(qint64(-daysAgo * 86400)).toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss-zzz"));
        const QString dir = root + '/' + sub + '/' + stamp;
        QDir().mkpath(folder ? dir + '/' + name : dir);
        QFile f(folder ? dir + '/' + name + QStringLiteral("/CURRENT") : dir + '/' + name);
        f.open(QIODevice::WriteOnly);
        f.write(QByteArray(1000, 'x'));
    };
    auto count = [&](const QString& sub) {
        return int(QDir(root + '/' + sub).entryList(QDir::Dirs | QDir::NoDotAndDotDot).size());
    };
    for (int d = 0; d < 15; ++d)
        makeCopy(QStringLiteral("worldA"), d + 0.5, QStringLiteral("level.dat"), false);
    for (int i = 0; i < 3; ++i)
        makeCopy(QStringLiteral("worldB/playerdata"), 100 + i, QStringLiteral("player.dat"), false);
    for (int i = 0; i < 12; ++i)
        makeCopy(QStringLiteral("bedrock"), i * 0.1, QStringLiteral("db"), true);

    const auto r = cleanupBackups(root, BackupPolicy{true, 10, 7}, now);
    check(r.removed == 5, QStringLiteral("removed %1 copies (expected 5)").arg(r.removed));
    check(count(QStringLiteral("worldA")) == 10, QStringLiteral("worldA keeps its 10 newest"));
    check(count(QStringLiteral("worldB/playerdata")) == 3, QStringLiteral("worldB keeps all 3 old copies"));
    check(count(QStringLiteral("bedrock")) == 12, QStringLiteral("12 recent db copies kept"));
    const auto again = cleanupBackups(root, BackupPolicy{true, 10, 7}, now);
    check(again.removed == 0, QStringLiteral("second run removes nothing"));
    const auto strict = cleanupBackups(root, BackupPolicy{true, 1, 0}, now);
    check(count(QStringLiteral("worldA")) == 1 && count(QStringLiteral("worldB/playerdata")) == 1
              && count(QStringLiteral("bedrock")) == 1,
          QStringLiteral("keep 1 / 0 days leaves exactly the newest of each (%1 removed)").arg(strict.removed));

    const QString cache = scratch + QStringLiteral("/tiles");
    QDir().mkpath(cache + QStringLiteral("/ab"));
    for (int i = 0; i < 10; ++i) {
        QFile f(cache + QStringLiteral("/ab/%1.jnt").arg(i));
        f.open(QIODevice::WriteOnly);
        f.write(QByteArray(1000, 'x'));
        f.setFileTime(now.addSecs(-3600 + i * 60), QFileDevice::FileModificationTime);
    }
    const auto t = trimTileCache(cache, 5000);
    check(t.removed == 5 && !QFile::exists(cache + QStringLiteral("/ab/0.jnt")) && QFile::exists(cache + QStringLiteral("/ab/9.jnt")),
          QStringLiteral("cache trimmed to the limit, oldest removed first"));
    const auto all = removeAllBackups(root);
    check(QDir(root).isEmpty() && QDir(scratch).exists(QStringLiteral("tiles")), QStringLiteral("remove all backups leaves the cache alone (%1 files)").arg(all.removed));
    std::printf("%d problems\n", bad);
    return bad == 0 ? 0 : 1;
}

static int deleteTest(const QString& worldDir)
{
    int bad = 0;
    auto check = [&](bool ok, const QString& what) {
        std::printf("%s %s\n", ok ? "OK  " : "FAIL", qPrintable(what));
        bad += ok ? 0 : 1;
    };
    const auto world = probeWorld(worldDir);
    if (!world || world->dimensions.isEmpty()) {
        std::printf("not a world: %s\n", qPrintable(worldDir));
        return 1;
    }
    const Dimension& dim = world->dimensions.first();

    if (world->edition == Edition::Java) {
        const QDir regionDir(dim.path + QStringLiteral("/region"));
        const auto files = regionDir.entryInfoList({QStringLiteral("r.*.mca")}, QDir::Files);
        if (files.isEmpty()) {
            std::printf("no region files\n");
            return 1;
        }
        const QString path = files.first().absoluteFilePath();
        const auto pos = parseRegionFileName(files.first().fileName());
        std::map<int, std::vector<uint8_t>> before;
        {
            const RegionFile r(path);
            for (int i = 0; i < 1024; ++i)
                if (r.hasChunk(i % 32, i / 32))
                    before[i] = r.chunkData(i % 32, i / 32);
        }
        int baseX = -1, baseZ = -1;
        for (int lz = 0; lz < 31 && baseX < 0; ++lz)
            for (int lx = 0; lx < 31; ++lx)
                if (before.count(lz * 32 + lx) && before.count(lz * 32 + lx + 1) && before.count((lz + 1) * 32 + lx)
                    && before.count((lz + 1) * 32 + lx + 1)) {
                    baseX = lx;
                    baseZ = lz;
                    break;
                }
        if (baseX < 0) {
            std::printf("no 2x2 block of stored chunks\n");
            return 1;
        }
        const ChunkRange range{pos->x * 32 + baseX, pos->z * 32 + baseZ, pos->x * 32 + baseX + 1,
                               pos->z * 32 + baseZ + 1};
        const auto result = deleteChunks(*world, dim, {range}, QStringLiteral("delete-test"));
        check(result.error.isEmpty(), QStringLiteral("deleted without errors (%1)").arg(result.error));
        check(result.chunks == 4, QStringLiteral("4 chunks reported deleted (%1)").arg(result.chunks));

        const RegionFile after(path);
        int gone = 0, kept = 0, changed = 0;
        for (const auto& [i, data] : before) {
            const int lx = i % 32, lz = i / 32;
            const bool inRange = (lx == baseX || lx == baseX + 1) && (lz == baseZ || lz == baseZ + 1);
            if (inRange) {
                gone += after.hasChunk(lx, lz) ? 0 : 1;
            } else if (!after.hasChunk(lx, lz)) {
                ++changed;
            } else {
                ++kept;
                if (after.chunkData(lx, lz) != data)
                    ++changed;
            }
        }
        check(gone == 4, QStringLiteral("the 4 chunks are gone from the file (%1)").arg(gone));
        check(changed == 0, QStringLiteral("all %1 other chunks are byte-identical (%2 differ)").arg(kept).arg(changed));
        const QString entities = dim.path + QStringLiteral("/entities/") + files.first().fileName();
        if (QFileInfo::exists(entities)) {
            const RegionFile e(entities);
            bool any = false;
            for (int dx = 0; dx < 2; ++dx)
                for (int dz = 0; dz < 2; ++dz)
                    any = any || e.hasChunk(baseX + dx, baseZ + dz);
            check(!any, QStringLiteral("entities of those chunks are gone too"));
        }
    } else {
        const QString dbDir = worldDir + QStringLiteral("/db");
        const auto db = BedrockDb::shared(dbDir);
        std::map<std::string, std::string> before;
        std::vector<std::pair<int, int>> chunks;
        db->forEachWithPrefix("", [&](std::string_view k, std::string_view v) {
            before.emplace(std::string(k), std::string(v));
            if (k.size() == 9 && uint8_t(k[8]) == 44) {
                int x, z;
                std::memcpy(&x, k.data(), 4);
                std::memcpy(&z, k.data() + 4, 4);
                chunks.emplace_back(x, z);
            }
            return true;
        });
        if (chunks.size() < 4) {
            std::printf("not enough chunks\n");
            return 1;
        }
        std::sort(chunks.begin(), chunks.end());
        const auto [cx, cz] = chunks.front();
        const ChunkRange range{cx, cz, cx + 1, cz + 1};
        const auto result = deleteChunks(*world, dim, {range}, QStringLiteral("delete-test"));
        check(result.error.isEmpty(), QStringLiteral("deleted without errors (%1)").arg(result.error));
        check(result.chunks >= 1, QStringLiteral("%1 chunks reported deleted").arg(result.chunks));

        size_t leftInRange = 0, changedOthers = 0, missingOthers = 0;
        std::map<std::string, std::string> after;
        db->forEachWithPrefix("", [&](std::string_view k, std::string_view v) {
            after.emplace(std::string(k), std::string(v));
            return true;
        });
        for (const auto& [k, v] : after) {
            if (k.size() == 9 || k.size() == 10) {
                int x, z;
                std::memcpy(&x, k.data(), 4);
                std::memcpy(&z, k.data() + 4, 4);
                if (range.contains(x, z))
                    ++leftInRange;
            }
            auto it = before.find(k);
            if (it == before.end() || it->second != v)
                ++changedOthers;
        }
        for (const auto& [k, v] : before) {
            const bool deletedOnPurpose = (k.size() == 9 || k.size() == 10 || k.rfind("digp", 0) == 0
                                           || k.rfind("actorprefix", 0) == 0);
            if (!after.count(k) && !deletedOnPurpose)
                ++missingOthers;
        }
        check(leftInRange == 0, QStringLiteral("no records left for the deleted chunks (%1)").arg(leftInRange));
        check(changedOthers == 0, QStringLiteral("no other record changed (%1)").arg(changedOthers));
        check(missingOthers == 0, QStringLiteral("no unrelated record disappeared (%1)").arg(missingOthers));
    }
    std::printf("%d problems\n", bad);
    return bad == 0 ? 0 : 1;
}

int searchCommand(const QStringList& args)
{
    if (args.size() < 3) {
        std::printf("usage: nbtcheck --search <world> <block|item|entity|poi> <names> [minX minZ maxX maxZ]\n");
        return 2;
    }
    const auto world = probeWorld(args[0]);
    if (!world || world->dimensions.isEmpty()) {
        std::printf("not a world: %s\n", qPrintable(args[0]));
        return 1;
    }
    SearchQuery q;
    const QString kind = args[1];
    q.kind = kind == QLatin1String("item")     ? SearchKind::Item
             : kind == QLatin1String("entity") ? SearchKind::Entity
             : kind == QLatin1String("poi")    ? SearchKind::Poi
                                               : SearchKind::Block;
    q.patterns = args[2].split(QLatin1Char(','), Qt::SkipEmptyParts);
    q.maxHits = 100000;
    if (args.size() >= 7)
        q.areas.push_back({args[3].toInt(), args[4].toInt(), args[5].toInt(), args[6].toInt()});

    QElapsedTimer timer;
    timer.start();
    int lastShown = -1;
    const SearchResult r = searchWorld(*world, world->dimensions.first(), q, [&](int done, int total) {
        const int percent = total > 0 ? done * 100 / total : 0;
        if (percent / 10 != lastShown / 10) {
            std::printf("  %3d%% (%d/%d)\n", percent, done, total);
            lastShown = percent;
        }
        return true;
    });
    const qint64 ms = timer.elapsed();
    std::printf("%zu finds%s in %d chunks (%d failed, %d legacy) in %lld ms, %.2f ms per chunk%s%s\n",
                r.hits.size(), r.truncated ? " (limit reached)" : "", r.chunks, r.failedChunks, r.legacyChunks,
                qlonglong(ms), r.chunks > 0 ? double(ms) / r.chunks : 0.0, r.error.isEmpty() ? "" : " - ",
                qPrintable(r.error));
    if (!r.hits.empty()) {
        int minY = r.hits[0].y, maxY = r.hits[0].y;
        std::map<std::string, int> perId;
        for (const SearchHit& h : r.hits) {
            minY = std::min(minY, h.y);
            maxY = std::max(maxY, h.y);
            ++perId[h.id];
        }
        std::printf("Y from %d to %d;", minY, maxY);
        for (const auto& [id, n] : perId)
            std::printf(" %s: %d", id.c_str(), n);
        std::printf("\n");
    }
    for (size_t i = 0; i < std::min<size_t>(r.hits.size(), 12); ++i) {
        const SearchHit& h = r.hits[i];
        std::printf("  %7d %4d %7d  %-32s %-28s %d %s\n", h.x, h.y, h.z, h.id.c_str(), h.subject.c_str(), h.count,
                    qPrintable(h.name));
    }
    return 0;
}

int showItems(const QStringList& files)
{
    const GameAssets& assets = GameAssets::instance();
    std::printf("%s\n", qPrintable(assets.describeSource()));
    if (assets.available())
        std::printf("jar: %s\n", qPrintable(assets.jarPath()));
    for (const char* id : {"minecraft:diamond_sword", "minecraft:oak_planks", "minecraft:enchanted_golden_apple",
                           "minecraft:shulker_box", "minecraft:crossbow", "minecraft:cooked_beef"}) {
        ItemStack probe;
        probe.id = QString::fromLatin1(id);
        probe.maxDamage = defaultMaxDamage(probe.id);
        std::printf("  %-38s %-28s texture %lld bytes\n", id, qPrintable(itemDisplayName(probe)),
                    qlonglong(assets.texturePng(probe.id).size()));
    }

    int stacks = 0, named = 0, withTexture = 0, enchanted = 0;
    QSet<QString> missingTexture;
    for (const QString& file : files) {
        std::printf("\n%s\n", qPrintable(file));
        std::unique_ptr<NbtFile> nbt;
        try {
            nbt = NbtFile::load(file);
        } catch (const std::exception& e) {
            std::printf("  cannot read: %s\n", e.what());
            continue;
        }
        auto walk = [&](auto&& self, const nbt::Tag& tag) -> void {
            if (const auto item = readItemStack(tag)) {
                ++stacks;
                if (itemDisplayName(*item) != prettifyId(item->id))
                    ++named;
                if (!assets.texturePng(item->id).isEmpty())
                    ++withTexture;
                else
                    missingTexture.insert(item->id);
                if (!item->enchantments.empty())
                    ++enchanted;
                if (stacks <= 40 || (!item->enchantments.empty() && enchanted <= 20))
                    std::printf("  %-28s %s\n", qPrintable(item->id), qPrintable(describeItem(*item)));
            }
            for (const auto& child : tag.children)
                self(self, *child);
        };
        walk(walk, *nbt->root);
    }
    std::printf("\n%d item stacks: %d named, %d with a texture, %d enchanted\n", stacks, named, withTexture,
                enchanted);
    for (const QString& id : missingTexture)
        std::printf("  no texture: %s\n", qPrintable(id));
    return 0;
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc >= 3 && QLatin1String(argv[1]) == QLatin1String("--delete-test"))
        return deleteTest(app.arguments().at(2));
    if (argc >= 3 && QLatin1String(argv[1]) == QLatin1String("--cleanup-test"))
        return cleanupTest(app.arguments().at(2));
    if (argc >= 3 && QLatin1String(argv[1]) == QLatin1String("--bedrock-encode-check"))
        return bedrockEncodeCheck(app.arguments().at(2));
    if (argc >= 3 && QLatin1String(argv[1]) == QLatin1String("--bedrock-sections-test"))
        return bedrockSectionsTest(app.arguments().at(2));
    if (argc >= 3 && QLatin1String(argv[1]) == QLatin1String("--bedrock-chests"))
        return bedrockChests(app.arguments().at(2));
    if (argc >= 3 && QLatin1String(argv[1]) == QLatin1String("--chunk-test"))
        return chunkTest(app.arguments().at(2));
    if (argc >= 3 && QLatin1String(argv[1]) == QLatin1String("--bedrock-chunk-test"))
        return bedrockChunkTest(app.arguments().at(2));
    if (argc >= 3 && QLatin1String(argv[1]) == QLatin1String("--shared-test"))
        return sharedTest(app.arguments().at(2));
    if (argc >= 3 && QLatin1String(argv[1]) == QLatin1String("--bedrock-stats"))
        return bedrockStats(app.arguments().at(2));
    if (argc >= 3 && QLatin1String(argv[1]) == QLatin1String("--dump")) {
        try {
            const auto file = NbtFile::load(app.arguments().at(2));
            dumpTag(*file->root, 0, argc >= 4 ? app.arguments().at(3).toInt() : 3);
            return 0;
        } catch (const std::exception& e) {
            std::printf("FAIL %s\n", e.what());
            return 1;
        }
    }
    if (argc >= 4 && QLatin1String(argv[1]) == QLatin1String("--render"))
        return renderTest(app.arguments().at(2), app.arguments().mid(3));
    QStringList targets = app.arguments().mid(1);
    if (!targets.isEmpty() && targets.first() == QLatin1String("--search"))
        return searchCommand(targets.mid(1));
    if (!targets.isEmpty() && targets.first() == QLatin1String("--items"))
        return showItems(targets.mid(1));
    if (!targets.isEmpty() && targets.first() == QLatin1String("--worlds"))
        return listWorlds(targets.mid(1));
    if (targets.size() == 2 && targets.first() == QLatin1String("--db"))
        return checkDb(targets[1]);
    if (targets.isEmpty()) {
        targets << defaultJavaRoots() << defaultBedrockRoots();
        std::printf("No paths given, checking default world folders.\n");
    }

    int total = 0, bad = 0;
    for (const QString& target : targets) {
        QStringList files;
        if (QFileInfo(target).isDir()) {
            QDirIterator it(target, {QStringLiteral("*.dat"), QStringLiteral("*.nbt")}, QDir::Files,
                            QDirIterator::Subdirectories);
            while (it.hasNext())
                files << it.next();
        } else if (QFileInfo::exists(target)) {
            files << target;
        }
        for (const QString& file : files) {
            ++total;
            if (!checkFile(file))
                ++bad;
        }
    }
    std::printf("\n%d files, %d problems\n", total, bad);
    return bad == 0 ? 0 : 1;
}
