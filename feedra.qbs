import qbs
import qbs.Process
import qbs.File
import qbs.FileInfo
import qbs.TextFile
import "../../../libs/openFrameworksCompiled/project/qtcreator/ofApp.qbs" as ofApp

Project{
    property string of_root: "../../.."

    ofApp {
        name: { return FileInfo.baseName(sourceDirectory) }

        files: [
            "src/AudioSample.cpp",
            "src/AudioSample.h",
            "src/AppConfig.cpp",
            "src/AppConfig.h",
            "src/DialogUtils.cpp",
            "src/DialogUtils.h",
            "src/OpenALSoundPlayer.cpp",
            "src/OpenALSoundPlayer.h",
            "src/Scene.cpp",
            "src/Scene.h",
            "src/SoundObject.cpp",
            "src/SoundObject.h",
            "src/SoundPlayer.cpp",
            "src/SoundPlayer.h",
            "src/UI/AddScene.cpp",
            "src/UI/AddScene.h",
            "src/UI/Button.cpp",
            "src/UI/Button.h",
            "src/UI/CheckBox.cpp",
            "src/UI/CheckBox.h",
            "src/UI/DeleteScene.cpp",
            "src/UI/DeleteScene.h",
            "src/UI/Interactive.cpp",
            "src/UI/Interactive.h",
            "src/UI/LoadButton.cpp",
            "src/UI/LoadButton.h",
            "src/UI/Looper.cpp",
            "src/UI/Looper.h",
            "src/UI/NumberBox.cpp",
            "src/UI/NumberBox.h",
            "src/UI/PlayBar.cpp",
            "src/UI/PlayBar.h",
            "src/UI/PlayButton.cpp",
            "src/UI/PlayButton.h",
            "src/UI/PlayScene.cpp",
            "src/UI/PlayScene.h",
            "src/UI/SimpleSlider.cpp",
            "src/UI/SimpleSlider.h",
            "src/UI/StopButton.cpp",
            "src/UI/StopButton.h",
            "src/UI/TextInputField.cpp",
            "src/UI/TextInputField.h",
            "src/UI/TextInputFieldFontRenderer.h",
            "src/main.cpp",
            "src/ofApp.cpp",
            "src/ofApp.h",
        ]

        of.addons: [
        ]

        // additional flags for the project. the of module sets some
        // flags by default to add the core libraries, search paths...
        // this flags can be augmented through the following properties:
        of.pkgConfigs: []       // list of additional system pkgs to include
        of.includePaths: []     // include search paths
        of.cFlags: []           // flags passed to the c compiler
        of.cxxFlags: []         // flags passed to the c++ compiler
        of.linkerFlags: []      // flags passed to the linker
        of.defines: []          // defines are passed as -D to the compiler
                                // and can be checked with #ifdef or #if in the code
        of.frameworks: []       // osx only, additional frameworks to link with the project
        of.staticLibraries: []  // static libraries
        of.dynamicLibraries: [] // dynamic libraries

        // other flags can be set through the cpp module: http://doc.qt.io/qbs/cpp-module.html
        // eg: this will enable ccache when compiling
        //
        // cpp.compilerWrapper: 'ccache'

        Depends{
            name: "cpp"
        }

        // common rules that parse the include search paths, core libraries...
        Depends{
            name: "of"
        }

        // dependency with the OF library
        Depends{
            name: "openFrameworks"
        }
    }

    property bool makeOF: true  // use makfiles to compile the OF library
                                // will compile OF only once for all your projects
                                // otherwise compiled per project with qbs
    

    property bool precompileOfMain: false  // precompile ofMain.h
                                           // faster to recompile when including ofMain.h 
                                           // but might use a lot of space per project

    references: [FileInfo.joinPaths(of_root, "/libs/openFrameworksCompiled/project/qtcreator/openFrameworks.qbs")]
}
