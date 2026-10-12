cmake_minimum_required(VERSION 3.21)
project(ObjectBuilderQt VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets)

# After each build, copy the Qt and compiler runtime DLLs next to the
# executable so it starts from an IDE, Explorer or a shell without Qt on PATH.
get_target_property(_ob_qmake Qt6::qmake IMPORTED_LOCATION)
get_filename_component(_ob_qt_bin "${_ob_qmake}" DIRECTORY)
find_program(OB_WINDEPLOYQT windeployqt HINTS "${_ob_qt_bin}")
function(ob_deploy_qt target)
    if(WIN32 AND OB_WINDEPLOYQT)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${OB_WINDEPLOYQT}" --no-translations --compiler-runtime --no-system-dxc-compiler --verbose 0 "$<TARGET_FILE:${target}>"
            COMMENT "Copying Qt DLLs next to ${target}"
            VERBATIM)
    endif()
endfunction()

# --- core: file formats, data model, geometry (no widgets) ---------------
add_library(obcore STATIC
    src/core/Flags.h
    src/core/ObjectData.h src/core/ObjectData.cpp
    src/core/LodObject.h src/core/LodObject.cpp
    src/core/P3dIO.h src/core/P3dIO.cpp
    src/core/ObjIO.h src/core/ObjIO.cpp
    src/core/Primitives.h src/core/Primitives.cpp
    src/core/MeshOps.h src/core/MeshOps.cpp
    src/core/Compression.h src/core/Compression.cpp
    src/core/Pbo.h src/core/Pbo.cpp
    src/core/Paa.h src/core/Paa.cpp
    src/core/FbxIO.h src/core/FbxIO.cpp
    src/core/Odol.h src/core/Odol.cpp
    src/core/TextureLibrary.h src/core/TextureLibrary.cpp
)
target_include_directories(obcore PUBLIC src)
target_link_libraries(obcore PUBLIC Qt6::Core Qt6::Gui)

# --- application ----------------------------------------------------------
option(OB_BUILD_APP "Build the GUI application" ON)
if(OB_BUILD_APP)
add_executable(ObjectBuilderQt WIN32
    src/main.cpp
    src/app/Document.h src/app/Document.cpp
    src/app/CommandRegistry.h src/app/CommandRegistry.cpp
    src/app/CommandIds.h
    src/app/Commands.h
    src/app/CommandsFile.cpp
    src/app/CommandsEdit.cpp
    src/app/CommandsGeometry.cpp
    src/app/CommandsView.cpp
    src/app/SelfTest.cpp
    src/app/CommandsImport.cpp
    src/app/GameData.h src/app/GameData.cpp
    src/ui/MainWindow.h src/ui/MainWindow.cpp
    src/ui/UiSpec.h src/ui/UiSpec.cpp
    src/ui/Viewport.h src/ui/Viewport.cpp
    src/ui/Panels.h src/ui/Panels.cpp
    src/ui/Dialogs.h src/ui/Dialogs.cpp
    resources/resources.qrc
)
target_link_libraries(ObjectBuilderQt PRIVATE obcore Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets)
target_compile_definitions(ObjectBuilderQt PRIVATE PROJECT_VERSION="${PROJECT_VERSION}")
ob_deploy_qt(ObjectBuilderQt)
endif()

# --- tests ----------------------------------------------------------------
enable_testing()
add_executable(obcore_tests tests/core_tests.cpp)
target_link_libraries(obcore_tests PRIVATE obcore)
target_compile_definitions(obcore_tests PRIVATE OB_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
add_test(NAME obcore_tests COMMAND obcore_tests)
ob_deploy_qt(obcore_tests)
add_executable(obtool tests/obtool.cpp)
target_link_libraries(obtool PRIVATE obcore)
ob_deploy_qt(obtool)
