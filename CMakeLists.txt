cmake_minimum_required(VERSION 3.21)
project(feedra VERSION 0.12.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC OFF)

# openal-soft is keg-only on Homebrew, so its .pc file is not on the default search path.
if(APPLE)
    foreach(_pkgdir
        /opt/homebrew/lib/pkgconfig
        /opt/homebrew/opt/openal-soft/lib/pkgconfig
        /usr/local/lib/pkgconfig
        /usr/local/opt/openal-soft/lib/pkgconfig
        /opt/local/lib/pkgconfig)
        if(EXISTS "${_pkgdir}")
            if("$ENV{PKG_CONFIG_PATH}" STREQUAL "")
                set(ENV{PKG_CONFIG_PATH} "${_pkgdir}")
            else()
                set(ENV{PKG_CONFIG_PATH} "$ENV{PKG_CONFIG_PATH}:${_pkgdir}")
            endif()
        endif()
    endforeach()
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets)

set(FEEDRA_SOURCES
    src/main.cpp
    src/AppConfig.cpp
    src/Theme.cpp
    src/AudioSample.cpp
    src/OpenALSoundPlayer.cpp
    src/SoundPlayer.cpp
    src/SampleLoadQueue.cpp
    src/Scene.cpp
    src/MainWindow.cpp
    src/widgets/SoundPadWidget.cpp
    src/widgets/SceneRowWidget.cpp
    src/widgets/SampleRowWidget.cpp
    src/widgets/ReorderListHost.cpp
    src/widgets/WaveformWidget.cpp
    resources/resources.qrc
)

set(KISSFFT_SOURCES
    src/kissFFT/kiss_fft.c
    src/kissFFT/kiss_fftr.c
    src/kissFFT/kiss_fftnd.c
    src/kissFFT/kiss_fftndr.c
    src/kissFFT/kfc.c
)

add_executable(feedra WIN32 ${FEEDRA_SOURCES} ${KISSFFT_SOURCES})

if(APPLE)
    set_target_properties(feedra PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "Feedra"
        MACOSX_BUNDLE_GUI_IDENTIFIER "org.feedra.feedra"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_RPATH TRUE
    )
endif()

target_include_directories(feedra PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/widgets
    ${CMAKE_CURRENT_SOURCE_DIR}/src/kissFFT
)

target_link_libraries(feedra PRIVATE Qt6::Widgets)

if(WIN32)
    # Bundled import libraries and headers. Linux and macOS use the system copies below.
    target_include_directories(feedra PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_compile_definitions(feedra PRIVATE FEEDRA_USING_MPG123 NOMINMAX WIN32_LEAN_AND_MEAN)
    set(FEEDRA_LIBDIR ${CMAKE_CURRENT_SOURCE_DIR}/libs/Win64)
    target_link_directories(feedra PRIVATE ${FEEDRA_LIBDIR})
    if(MINGW AND EXISTS "${FEEDRA_LIBDIR}/libOpenAL32.dll.a")
        target_link_libraries(feedra PRIVATE "${FEEDRA_LIBDIR}/libOpenAL32.dll.a" sndfile libmpg123-0)
    else()
        target_link_libraries(feedra PRIVATE OpenAL32 sndfile libmpg123-0)
    endif()
    if(MSVC)
        target_link_options(feedra PRIVATE "/IGNORE:4099")
    endif()
else()
    # Linux and macOS: Qt 6, OpenAL Soft, libsndfile, mpg123, and pkg-config.
    #   Debian/Ubuntu: qt6-base-dev libopenal-dev libsndfile1-dev libmpg123-dev pkg-config
    #   Fedora: qt6-qtbase-devel openal-soft-devel libsndfile-devel mpg123-devel pkgconf
    #   macOS: brew install qt openal-soft libsndfile mpg123 pkg-config
    find_package(PkgConfig REQUIRED)
    find_package(Threads REQUIRED)
    pkg_check_modules(OPENAL REQUIRED IMPORTED_TARGET openal)
    pkg_check_modules(SNDFILE REQUIRED IMPORTED_TARGET sndfile)
    pkg_check_modules(MPG123 REQUIRED IMPORTED_TARGET libmpg123)

    target_compile_definitions(feedra PRIVATE FEEDRA_USING_MPG123)
    if(APPLE)
        # <OpenAL/al.h> is Apple's deprecated framework. This build links OpenAL Soft.
        target_compile_definitions(feedra PRIVATE FEEDRA_OPENAL_SOFT)
    endif()
    target_link_libraries(feedra PRIVATE
        PkgConfig::OPENAL
        PkgConfig::SNDFILE
        PkgConfig::MPG123
        Threads::Threads
    )

    foreach(_rpath_dir ${OPENAL_LIBRARY_DIRS} ${SNDFILE_LIBRARY_DIRS} ${MPG123_LIBRARY_DIRS})
        if(_rpath_dir)
            set_property(TARGET feedra APPEND PROPERTY BUILD_RPATH "${_rpath_dir}")
        endif()
    endforeach()

    if(NOT APPLE)
        target_link_libraries(feedra PRIVATE m)
        if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9.0)
            target_link_libraries(feedra PRIVATE stdc++fs)
        endif()
    endif()

    message(STATUS "Feedra OpenAL: ${OPENAL_LIBRARIES} (${OPENAL_LIBRARY_DIRS})")
    message(STATUS "Feedra sndfile: ${SNDFILE_LIBRARIES} (${SNDFILE_LIBRARY_DIRS})")
    message(STATUS "Feedra mpg123: ${MPG123_LIBRARIES} (${MPG123_LIBRARY_DIRS})")
endif()

if(MSVC)
    target_compile_options(feedra PRIVATE /Zc:__cplusplus /wd4996)
endif()

function(feedra_copy_runtime src dest_name)
    add_custom_command(TARGET feedra POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${src}" "$<TARGET_FILE_DIR:feedra>/${dest_name}"
        VERBATIM)
endfunction()

add_custom_command(TARGET feedra POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/bin/data"
        "$<TARGET_FILE_DIR:feedra>/data"
    VERBATIM)

if(WIN32)
    foreach(dll OpenAL32.dll sndfile.dll libmpg123-0.dll)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/bin/${dll}")
            feedra_copy_runtime("${CMAKE_CURRENT_SOURCE_DIR}/bin/${dll}" "${dll}")
        elseif(EXISTS "${FEEDRA_LIBDIR}/${dll}")
            feedra_copy_runtime("${FEEDRA_LIBDIR}/${dll}" "${dll}")
        endif()
    endforeach()

    get_target_property(_qt_qmake Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_qt_bin "${_qt_qmake}" DIRECTORY)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${_qt_bin}")
    if(WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET feedra POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}" --no-translations "$<TARGET_FILE:feedra>"
            VERBATIM)
    endif()
endif()

if(APPLE)
    get_target_property(_qt_qmake Qt6::qmake IMPORTED_LOCATION)
    if(NOT _qt_qmake)
        get_target_property(_qt_qmake Qt6::qmake IMPORTED_LOCATION_RELEASE)
    endif()
    get_filename_component(_qt_bin "${_qt_qmake}" DIRECTORY)
    find_program(MACDEPLOYQT_EXECUTABLE macdeployqt HINTS "${_qt_bin}")
    if(MACDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET feedra POST_BUILD
            COMMAND "${MACDEPLOYQT_EXECUTABLE}" "$<TARGET_BUNDLE_DIR:feedra>" -always-overwrite
            VERBATIM)
    endif()
endif()
