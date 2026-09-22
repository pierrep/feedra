cmake_minimum_required(VERSION 3.21)
project(feedra VERSION 0.12.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC OFF)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

set(FEEDRA_SOURCES
    src/main.cpp
    src/AppConfig.cpp
    src/Theme.cpp
    src/AudioSample.cpp
    src/OpenALSoundPlayer.cpp
    src/SoundPlayer.cpp
    src/Scene.cpp
    src/MainWindow.cpp
    src/widgets/SoundPadWidget.cpp
    src/widgets/SceneRowWidget.cpp
    src/widgets/SampleRowWidget.cpp
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

target_include_directories(feedra PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/widgets
    ${CMAKE_CURRENT_SOURCE_DIR}/src/kissFFT
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(feedra PRIVATE Qt6::Widgets)

if(WIN32)
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
    find_package(OpenAL REQUIRED)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SNDFILE REQUIRED sndfile)
    pkg_check_modules(MPG123 REQUIRED libmpg123)
    target_compile_definitions(feedra PRIVATE FEEDRA_USING_MPG123)
    target_include_directories(feedra PRIVATE ${OPENAL_INCLUDE_DIR} ${SNDFILE_INCLUDE_DIRS} ${MPG123_INCLUDE_DIRS})
    target_link_libraries(feedra PRIVATE ${OPENAL_LIBRARY} ${SNDFILE_LIBRARIES} ${MPG123_LIBRARIES})
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
