#include <QCoreApplication>
#include <QFileInfo>
#include <QTranslator>

#include <cstdio>

namespace {
int failures = 0;
QTranslator translator;

void check(bool ok, const QString& what)
{
    std::printf("%-62s %s\n", qPrintable(what), ok ? "ok" : "FAILED");
    if (!ok)
        ++failures;
}

void checkText(const char* context, const char* source, const QString& expected)
{
    const QString got = QCoreApplication::translate(context, source);
    check(got == expected, QStringLiteral("%1: %2").arg(QLatin1String(context), QLatin1String(source)));
    if (got != expected)
        std::printf("      got      %s\n      expected %s\n", qPrintable(got), qPrintable(expected));
}

void checkPlural(const char* context, const char* source, int n, const QString& expected)
{
    const QString got = QCoreApplication::translate(context, source, nullptr, n);
    check(got == expected, QStringLiteral("%1: %2 with n=%3").arg(QLatin1String(context), QLatin1String(source)).arg(n));
    if (got != expected)
        std::printf("      got      %s\n      expected %s\n", qPrintable(got), qPrintable(expected));
}
}

int main(int argc, char** argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    if (args.size() < 2) {
        std::printf("usage: i18n_test <justnbt_ru.qm>\n");
        return 2;
    }
    check(QFileInfo::exists(args[1]), QStringLiteral("the compiled translation is there"));
    check(translator.load(args[1]), QStringLiteral("it loads"));
    QCoreApplication::installTranslator(&translator);
    check(translator.language().startsWith(QLatin1String("ru")), QStringLiteral("it says it is Russian"));

    checkText("MainWindow", "Open NBT file…", QStringLiteral("Открыть NBT-файл…"));
    checkText("NbtEditor", "Save", QStringLiteral("Сохранить"));
    checkText("NbtModel", "Value", QStringLiteral("Значение"));
    checkText("MapTab", "To spawn", QStringLiteral("К спавну"));
    checkText("MapView", "   ·   no data", QStringLiteral("   ·   нет данных"));
    checkText("AddTagDialog", "New tag", QStringLiteral("Новый тег"));
    checkText("ArrayEditDialog", "Array", QStringLiteral("Массив"));
    checkText("MaintenanceDialog", "Backups", QStringLiteral("Резервные копии"));
    checkText("justnbt", "Cannot open the file: ", QStringLiteral("Не удалось открыть файл: "));
    checkText("justnbt", "Nether", QStringLiteral("Незер"));
    checkText("justnbt", "Local player (~local_player)", QStringLiteral("Локальный игрок (~local_player)"));

    checkText("MainWindow", "Worlds found: %1", QStringLiteral("Найдено миров: %1"));
    checkText("MainWindow", "Open with", QStringLiteral("Открыть с помощью"));
    checkText("SearchPanel", "Only in the selected area", QStringLiteral("Только в выделенной области"));
    checkText("MapTab", "Search", QStringLiteral("Поиск"));
    checkText("SearchPanel", "Search", QStringLiteral("Найти"));
    checkText("FileAssociation", "NBT file", QStringLiteral("NBT файл"));
    checkText("InventoryPanel", "Ender chest", QStringLiteral("Эндер-сундук"));
    checkText("NbtEditor", "Inventory", QStringLiteral("Инвентарь"));
    checkText("ItemEditorDialog", "Food and potions", QStringLiteral("Еда и зелья"));
    checkText("ItemEditorDialog", "Saves from death like a totem", QStringLiteral("Спасает от смерти, как тотем"));
    checkText("MinecraftSourceDialog", "The newest", QStringLiteral("Самая новая"));
    checkText("NbtEditor", "Read %1 again?\n\nUnsaved changes will be lost.",
              QStringLiteral("Перечитать «%1» заново?\n\nНесохранённые изменения пропадут."));

    checkPlural("NbtEditor", "%n tag(s)", 1, QStringLiteral("1 тег"));
    checkPlural("NbtEditor", "%n tag(s)", 3, QStringLiteral("3 тега"));
    checkPlural("NbtEditor", "%n tag(s)", 7, QStringLiteral("7 тегов"));
    checkPlural("NbtEditor", "%n tag(s)", 11, QStringLiteral("11 тегов"));
    checkPlural("NbtEditor", "%n tag(s)", 21, QStringLiteral("21 тег"));
    checkPlural("MapTab", "%n chunk(s)", 2, QStringLiteral("2 чанка"));
    checkPlural("NbtModel", "%n entry(ies)", 5, QStringLiteral("5 записей"));
    checkPlural("NbtModel", "%n element(s)", 1, QStringLiteral("1 элемент"));

    check(QCoreApplication::translate("MainWindow", "this text does not exist")
              == QLatin1String("this text does not exist"),
          QStringLiteral("an unknown text stays English"));

    std::printf("\n%d failures\n", failures);
    return failures == 0 ? 0 : 1;
}
