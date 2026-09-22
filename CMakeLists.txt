cmake_minimum_required(VERSION 3.21)

project(JustNBT VERSION 0.1.0 LANGUAGES C CXX)

if(WIN32)
    enable_language(RC)
    set(JN_WIN_RESOURCES app.rc)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets Network)
find_package(Qt6 6.4 QUIET COMPONENTS LinguistTools)

include(FetchContent)
find_package(libdeflate CONFIG QUIET)
if(NOT libdeflate_FOUND)
    set(LIBDEFLATE_BUILD_SHARED_LIB OFF CACHE BOOL "" FORCE)
    set(LIBDEFLATE_BUILD_GZIP OFF CACHE BOOL "" FORCE)
    set(LIBDEFLATE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(libdeflate
        GIT_REPOSITORY https://github.com/ebiggers/libdeflate.git
        GIT_TAG v1.24
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(libdeflate)
endif()
if(TARGET libdeflate::libdeflate_static)
    set(JN_DEFLATE libdeflate::libdeflate_static)
else()
    set(JN_DEFLATE libdeflate::libdeflate_shared)
endif()

FetchContent_Declare(lz4
    GIT_REPOSITORY https://github.com/lz4/lz4.git
    GIT_TAG v1.10.0
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR do-not-add)
FetchContent_MakeAvailable(lz4)
add_library(jn_lz4 STATIC ${lz4_SOURCE_DIR}/lib/lz4.c)
target_include_directories(jn_lz4 PUBLIC ${lz4_SOURCE_DIR}/lib)

set(LEVELDB_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LEVELDB_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(LEVELDB_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/leveldb EXCLUDE_FROM_ALL)
target_link_libraries(leveldb ${JN_DEFLATE})

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra)
endif()

qt_standard_project_setup()

add_library(justnbt_core STATIC
    src/core/Nbt.h
    src/core/Nbt.cpp
    src/core/NbtIO.h
    src/core/NbtIO.cpp
    src/core/Compression.h
    src/core/Compression.cpp
    src/core/NbtFile.h
    src/core/NbtFile.cpp
    src/core/Worlds.h
    src/core/Worlds.cpp
    src/core/Backup.h
    src/core/Backup.cpp
    src/core/BedrockDb.h
    src/core/BedrockDb.cpp
    src/core/Region.h
    src/core/Region.cpp
    src/core/BlockColors.h
    src/core/BlockColors.cpp
    src/core/MapRender.h
    src/core/MapRender.cpp
    src/core/MapRenderDetail.h
    src/core/BedrockMap.cpp
    src/core/WorldBorder.h
    src/core/WorldBorder.cpp
    src/core/Maintenance.h
    src/core/Maintenance.cpp
    src/core/WorldEdit.h
    src/core/WorldEdit.cpp
    src/core/Config.h
    src/core/Config.cpp
    src/core/GameAssets.h
    src/core/GameAssets.cpp
    src/core/ItemInfo.h
    src/core/ItemInfo.cpp
    src/core/ChunkFormat.h
    src/core/ChunkFormat.cpp
    src/core/WorldSearch.h
    src/core/WorldSearch.cpp
    src/core/TagInfo.h
    src/core/TagInfo.cpp
)
target_include_directories(justnbt_core PUBLIC src)
target_link_libraries(justnbt_core PUBLIC Qt6::Core PRIVATE ${JN_DEFLATE} leveldb jn_lz4)

set(JN_UI_SOURCES
    src/ui/MainWindow.h
    src/ui/MainWindow.cpp
    src/ui/NbtModel.h
    src/ui/NbtModel.cpp
    src/ui/NbtEditor.h
    src/ui/NbtEditor.cpp
    src/ui/Dialogs.h
    src/ui/Dialogs.cpp
    src/ui/MapView.h
    src/ui/MapView.cpp
    src/ui/MapTab.h
    src/ui/MapTab.cpp
    src/ui/MaintenanceDialog.h
    src/ui/MaintenanceDialog.cpp
    src/ui/ElidingLabel.h
    src/ui/ElidingLabel.cpp
    src/ui/ItemVisuals.h
    src/ui/ItemVisuals.cpp
    src/ui/SingleInstance.h
    src/ui/SingleInstance.cpp
    src/ui/FileAssociation.h
    src/ui/FileAssociation.cpp
    src/ui/SearchPanel.h
    src/ui/SearchPanel.cpp
    src/ui/Inventory.h
    src/ui/Inventory.cpp
    src/ui/InventoryPanel.h
    src/ui/InventoryPanel.cpp
    src/ui/MinecraftSourceDialog.h
    src/ui/MinecraftSourceDialog.cpp
    src/ui/ItemEditorDialog.h
    src/ui/ItemEditorDialog.cpp
)
set(JN_UI_LIBS justnbt_core Qt6::Widgets Qt6::Network)
if(WIN32)
    list(APPEND JN_UI_LIBS shell32)
endif()

qt_add_executable(JustNBT WIN32
    ${JN_WIN_RESOURCES}
    src/resources.qrc
    src/main.cpp
    ${JN_UI_SOURCES}
)
target_link_libraries(JustNBT PRIVATE ${JN_UI_LIBS})
target_compile_definitions(JustNBT PRIVATE JUSTNBT_VERSION="${PROJECT_VERSION}")

if(TARGET Qt6::lrelease)
    qt_add_translations(JustNBT TS_FILES translations/justnbt_ru.ts translations/justnbt_en.ts)
else()
    message(WARNING "Qt LinguistTools not found: JustNBT will only speak English.")
endif()

find_package(Qt6 COMPONENTS Test QUIET)
if(TARGET Qt6::Test)
    add_executable(undo_test tests/undo_test.cpp src/ui/NbtModel.cpp src/ui/NbtModel.h
                         src/ui/ItemVisuals.cpp src/ui/ItemVisuals.h)
    target_link_libraries(undo_test PRIVATE justnbt_core Qt6::Widgets Qt6::Test)
    add_executable(map_selection_test tests/map_selection_test.cpp src/ui/MapView.cpp src/ui/MapView.h)
    target_link_libraries(map_selection_test PRIVATE justnbt_core Qt6::Widgets Qt6::Test)
endif()
add_executable(map_snapshot tests/map_snapshot.cpp src/ui/MapTab.cpp src/ui/MapTab.h src/ui/MapView.cpp
                            src/ui/MapView.h src/ui/NbtModel.cpp src/ui/NbtModel.h
                            src/ui/ElidingLabel.cpp src/ui/ElidingLabel.h
                            src/ui/ItemVisuals.cpp src/ui/ItemVisuals.h
                            src/ui/SearchPanel.cpp src/ui/SearchPanel.h)
target_link_libraries(map_snapshot PRIVATE justnbt_core Qt6::Widgets)
add_executable(dialog_snapshot tests/dialog_snapshot.cpp src/ui/MaintenanceDialog.cpp src/ui/MaintenanceDialog.h)
target_link_libraries(dialog_snapshot PRIVATE justnbt_core Qt6::Widgets)
add_executable(search_test tests/search_test.cpp)
target_link_libraries(search_test PRIVATE justnbt_core)
add_executable(i18n_test tests/i18n_test.cpp)
target_link_libraries(i18n_test PRIVATE Qt6::Core)
add_executable(assets_test tests/assets_test.cpp)
target_link_libraries(assets_test PRIVATE justnbt_core)
add_executable(item_test tests/item_test.cpp)
target_link_libraries(item_test PRIVATE justnbt_core)
add_executable(tree_snapshot tests/tree_snapshot.cpp src/ui/NbtModel.cpp src/ui/NbtModel.h
                            src/ui/ItemVisuals.cpp src/ui/ItemVisuals.h)
target_link_libraries(tree_snapshot PRIVATE justnbt_core Qt6::Widgets)
set(JN_WINDOW_TESTS session_test opening_test inventory_test)
if(TARGET Qt6::Test)
    list(APPEND JN_WINDOW_TESTS search_ui_test)
endif()
foreach(test ${JN_WINDOW_TESTS})
    add_executable(${test} tests/${test}.cpp src/resources.qrc ${JN_UI_SOURCES})
    target_link_libraries(${test} PRIVATE ${JN_UI_LIBS} $<TARGET_NAME_IF_EXISTS:Qt6::Test>)
    set_property(TARGET ${test} PROPERTY AUTORCC ON)
    target_compile_definitions(${test} PRIVATE JUSTNBT_VERSION="${PROJECT_VERSION}")
endforeach()

add_executable(nbtcheck tools/nbtcheck.cpp)
target_link_libraries(nbtcheck PRIVATE justnbt_core Qt6::Gui)

if(UNIX AND NOT APPLE)
    include(GNUInstallDirs)
    install(TARGETS JustNBT RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES packaging/justnbt.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES packaging/justnbt.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps)
    install(FILES LICENSE THIRD_PARTY_NOTICES.md DESTINATION ${CMAKE_INSTALL_DOCDIR})
    install(DIRECTORY LICENSES DESTINATION ${CMAKE_INSTALL_DOCDIR})
endif()

if(WIN32)
    find_program(JN_WINDEPLOYQT windeployqt HINTS "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_BINS}")
    get_filename_component(JN_CC_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
    set(JN_RUNTIME_DLLS
        "${JN_CC_BIN}/libstdc++-6.dll"
        "${JN_CC_BIN}/libgcc_s_seh-1.dll"
        "${JN_CC_BIN}/libwinpthread-1.dll")
    add_custom_command(TARGET JustNBT POST_BUILD
        COMMAND "${JN_WINDEPLOYQT}" --no-translations --no-compiler-runtime
                --no-system-d3d-compiler --no-opengl-sw --verbose 0
                "$<TARGET_FILE:JustNBT>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${JN_RUNTIME_DLLS} "$<TARGET_FILE_DIR:JustNBT>"
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:JustNBT>/translations"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_TRANSLATIONS}/qtbase_ru.qm"
                "$<TARGET_FILE_DIR:JustNBT>/translations"
        VERBATIM)
endif()
