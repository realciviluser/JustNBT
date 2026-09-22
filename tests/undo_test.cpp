#include "core/NbtIO.h"
#include "ui/NbtModel.h"

#include <QAbstractItemModelTester>
#include <QApplication>
#include <QMimeData>
#include <QUndoStack>

#include <cstdio>

using nbt::Tag;
using nbt::TagType;

static std::vector<uint8_t> bytes(const Tag& root)
{
    std::vector<uint8_t> out;
    nbt::write(root, nbt::Endian::Big, out);
    return out;
}

static std::unique_ptr<Tag> makeDocument()
{
    auto root = std::make_unique<Tag>(TagType::Compound, "");
    auto data = std::make_unique<Tag>(TagType::Compound, "Data");
    auto health = std::make_unique<Tag>(TagType::Float, "Health");
    health->floating = 20.0;
    data->append(std::move(health));
    auto name = std::make_unique<Tag>(TagType::String, "LevelName");
    name->string = "Мой мир";
    data->append(std::move(name));
    auto list = std::make_unique<Tag>(TagType::List, "Pos");
    list->listType = TagType::Double;
    for (double v : {1.5, 64.0, -3.25}) {
        auto e = std::make_unique<Tag>(TagType::Double);
        e->floating = v;
        list->append(std::move(e));
    }
    data->append(std::move(list));
    auto arr = std::make_unique<Tag>(TagType::IntArray, "UUID");
    arr->ints = {1, 2, 3, 4};
    data->append(std::move(arr));
    data->append(std::make_unique<Tag>(TagType::List, "Empty"));
    root->append(std::move(data));
    return root;
}

int main(int argc, char** argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    static int modelFailures = 0;
    qInstallMessageHandler([](QtMsgType, const QMessageLogContext&, const QString& msg) {
        if (!msg.startsWith(QLatin1String("FAIL!")))
            return;
        if (msg.contains(QLatin1String("(c.next)")) || msg.contains(QLatin1String("(c.last)")))
            return;
        ++modelFailures;
        std::printf("MODEL %s\n", qPrintable(msg));
    });
    QApplication app(argc, argv);
    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        std::printf("%s %s\n", ok ? "OK  " : "FAIL", what);
        failures += ok ? 0 : 1;
    };

    auto root = makeDocument();
    QUndoStack undo;
    NbtModel model(root.get(), QStringLiteral("test.dat"), &undo);
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::Warning);

    const auto original = bytes(*root);
    Tag* data = root->child("Data");
    auto idx = [&](Tag* t, int col = 0) { return model.indexFromTag(t, col); };

    check(model.setData(idx(data->child("Health"), NbtModel::ValueColumn), QStringLiteral("5,5"), Qt::EditRole),
          "set Health = 5,5");
    check(model.setData(idx(data->child("LevelName")), QStringLiteral("Name"), Qt::EditRole), "rename LevelName");
    check(!model.setData(idx(data->child("Name")), QStringLiteral("Health"), Qt::EditRole), "duplicate name rejected");
    const int before = undo.count();
    check(!model.setData(idx(data->child("Health"), NbtModel::ValueColumn), QStringLiteral("abc"), Qt::EditRole),
          "bad float rejected");
    check(undo.count() == before, "rejected edit leaves no undo step");
    Tag* pos = data->child("Pos");
    auto e = std::make_unique<Tag>(TagType::Double);
    e->floating = 99.0;
    check(model.insertTag(idx(pos), 1, std::move(e)).isValid(), "insert into list");
    check(!model.insertTag(idx(pos), 0, std::make_unique<Tag>(TagType::Int)).isValid(), "wrong list type rejected");
    model.removeTag(idx(pos->children[0].get()));
    check(pos->children.size() == 3 && pos->children[0]->floating == 99.0, "remove list element");
    Tag* empty = data->child("Empty");
    check(model.insertTag(idx(empty), 0, std::make_unique<Tag>(TagType::String)).isValid(), "insert into empty list");
    check(empty->listType == TagType::String, "empty list got element type");
    Tag newArr(TagType::IntArray);
    newArr.ints = {9, 8, 7};
    model.replaceValue(data->child("UUID"), newArr);
    check(model.modifiedCount() == 4, QStringLiteral("4 modified (%1)").arg(model.modifiedCount()).toLatin1());
    check(model.addedCount() == 1, QStringLiteral("1 added (%1)").arg(model.addedCount()).toLatin1());
    check(model.removedCount() == 1, QStringLiteral("1 removed (the list element) (%1)").arg(model.removedCount()).toLatin1());
    check(model.changeOf(root.get()) == NbtModel::Change::Inside, "root marked as containing changes");
    check(model.changeOf(data->child("Health")) == NbtModel::Change::Modified, "edited tag marked as modified");
    check(model.changeOf(data->child("Empty")->children[0].get()) == NbtModel::Change::Added, "new tag marked as added");
    check(!model.changedTags().empty(), "changed tags listed for stepping through");

    model.removeTag(idx(data));
    check(root->children.empty(), "remove compound");

    const auto edited = bytes(*root);
    check(edited != original, "document changed");
    check(model.removedCount() == 1 && model.addedCount() == 0,
          "after removing Data only that removal is marked");

    while (undo.canUndo())
        undo.undo();
    check(bytes(*root) == original, "undo all -> original bytes");
    check(model.addedCount() == 0 && model.modifiedCount() == 0 && model.removedCount() == 0,
          QStringLiteral("undo all -> no change marks left (+%1 ✎%2 −%3)")
              .arg(model.addedCount())
              .arg(model.modifiedCount())
              .arg(model.removedCount())
              .toUtf8());
    check(root->child("Data") && root->child("Data")->child("Empty")->listType == TagType::End,
          "empty list type restored");

    while (undo.canRedo())
        undo.redo();
    check(bytes(*root) == edited, "redo all -> edited bytes");
    check(model.removedCount() == 1, "redo all -> marks are back");
    model.resetBaseline();
    check(model.addedCount() == 0 && model.modifiedCount() == 0 && model.removedCount() == 0,
          "saving clears the marks");

    for (int i = 0; i < 4; ++i)
        undo.undo();
    Tag* d = root->child("Data");
    check(d != nullptr, "Data back after partial undo");
    if (d)
        model.setData(idx(d->child("Health"), NbtModel::ValueColumn), QStringLiteral("1"), Qt::EditRole);
    check(!undo.canRedo(), "new edit clears redo");
    while (undo.canUndo())
        undo.undo();
    check(bytes(*root) == original, "undo after branch -> original bytes");

    {
        auto doc = makeDocument();
        QUndoStack stack;
        NbtModel m(doc.get(), QStringLiteral("test.dat"), &stack);
        QAbstractItemModelTester t2(&m, QAbstractItemModelTester::FailureReportingMode::Warning);
        Tag* d = doc->child("Data");
        const auto before = bytes(*doc);

        const std::vector<const Tag*> copied{d->child("Health"), d->child("LevelName")};
        const QByteArray clip = NbtModel::serializeTags(copied);
        auto pasted = NbtModel::deserializeTags(clip);
        check(pasted.size() == 2 && pasted[0]->name == "Health" && pasted[1]->name == "LevelName",
              "clipboard: two tags survive the round trip");
        const int steps = stack.index();
        check(m.insertTags(d, -1, std::move(pasted), QStringLiteral("вставка")), "paste accepted");
        check(stack.index() == steps + 1, "paste is one undo step");
        check(d->child("Health 2") && d->child("LevelName 2"), "pasted names made unique");

        m.duplicateTags({d->child("Health"), d->child("UUID")});
        check(d->child("Health 3") && d->child("UUID 2"), "duplicate makes copies next to the originals");

        m.removeTags({d->child("Health 2"), d->child("Health 3"), d->child("LevelName 2"), d->child("UUID 2")});
        check(!d->child("Health 2") && !d->child("UUID 2"), "several tags deleted");
        stack.undo();
        check(d->child("Health 2") && d->child("UUID 2"), "one undo brings them all back");

        std::vector<std::unique_ptr<Tag>> wrong;
        wrong.push_back(std::make_unique<Tag>(TagType::Int));
        check(!m.insertTags(d->child("Pos"), -1, std::move(wrong), QStringLiteral("вставка")),
              "list refuses a foreign type");

        Tag* health = d->child("Health");
        QModelIndexList dragged{m.indexFromTag(health)};
        std::unique_ptr<QMimeData> mime(m.mimeData(dragged));
        check(mime != nullptr, "drag data created");
        const int before2 = stack.index();
        m.dropMimeData(mime.get(), Qt::MoveAction, 0, 0, m.indexFromTag(d->child("Empty")));
        check(!d->child("Health") && d->child("Empty")->children.size() == 1, "tag moved by drag and drop");
        check(stack.index() == before2 + 1, QStringLiteral("the move is one undo step (%1 -> %2)").arg(before2).arg(stack.index()).toUtf8());
        stack.undo();
        check(d->child("Health") && d->child("Empty")->children.empty(), "undo puts it back");

        while (stack.canUndo())
            stack.undo();
        check(bytes(*doc) == before, "undo all -> the document is as it was");
    }

    check(modelFailures == 0, "model notifications consistent (QAbstractItemModelTester)");
    std::printf("\n%d failures\n", failures);
    return failures == 0 ? 0 : 1;
}
