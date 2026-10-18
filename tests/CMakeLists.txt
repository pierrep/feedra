# Tests. Run with:  ctest --test-dir build --output-on-failure
#
# test_region_renderer   the loop/crossfade renderer, pure computation
# test_engine            the audio engine on a real OpenAL device. It sets ALSOFT_DRIVERS=null,
#                        so OpenAL Soft renders to its silent "null" device and no sound card is
#                        needed. Skipped when no device opens.

add_executable(test_region_renderer test_region_renderer.cpp ${CMAKE_SOURCE_DIR}/src/RegionRenderer.cpp)
target_include_directories(test_region_renderer PRIVATE ${CMAKE_SOURCE_DIR}/src)
feedra_warnings(test_region_renderer)
add_test(NAME region_renderer COMMAND test_region_renderer)

add_executable(test_engine test_engine.cpp)
target_link_libraries(test_engine PRIVATE feedra_engine)
feedra_warnings(test_engine)
add_test(NAME engine COMMAND test_engine quick)
set_tests_properties(engine PROPERTIES
    ENVIRONMENT "ALSOFT_DRIVERS=null"
    SKIP_RETURN_CODE 77
    TIMEOUT 300)

if(WIN32)
    # The audio DLLs next to the test, as for the app.
    foreach(dll OpenAL32.dll sndfile.dll libmpg123-0.dll)
        foreach(dir "${CMAKE_SOURCE_DIR}/bin" "${FEEDRA_LIBDIR}")
            if(EXISTS "${dir}/${dll}")
                add_custom_command(TARGET test_engine POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${dir}/${dll}" "$<TARGET_FILE_DIR:test_engine>/${dll}"
                    VERBATIM)
                break()
            endif()
        endforeach()
    endforeach()
endif()

# The preview (cue) output: a second device on its own thread.
add_executable(test_cue test_cue.cpp)
target_link_libraries(test_cue PRIVATE feedra_engine)
feedra_warnings(test_cue)
add_test(NAME cue COMMAND test_cue)
set_tests_properties(cue PROPERTIES
    ENVIRONMENT "ALSOFT_DRIVERS=null"
    SKIP_RETURN_CODE 77
    TIMEOUT 60)

# The whole window, offscreen: open a project, quit (which saves), check the file.
add_executable(test_project_roundtrip test_project_roundtrip.cpp)
target_link_libraries(test_project_roundtrip PRIVATE feedra_app)
feedra_warnings(test_project_roundtrip)
add_test(NAME project_roundtrip COMMAND test_project_roundtrip)
set_tests_properties(project_roundtrip PROPERTIES
    ENVIRONMENT "ALSOFT_DRIVERS=null;QT_QPA_PLATFORM=offscreen"
    TIMEOUT 120)

# The Editor page and the bottom panel, driven directly with a real pad.
add_executable(test_panels test_panels.cpp)
target_link_libraries(test_panels PRIVATE feedra_app)
feedra_warnings(test_panels)
add_test(NAME panels COMMAND test_panels)
set_tests_properties(panels PROPERTIES
    ENVIRONMENT "ALSOFT_DRIVERS=null;QT_QPA_PLATFORM=offscreen"
    SKIP_RETURN_CODE 77
    TIMEOUT 120)

# Scene fades and the crossfade between scenes, with real pads.
add_executable(test_scene_fades test_scene_fades.cpp)
target_link_libraries(test_scene_fades PRIVATE feedra_app)
feedra_warnings(test_scene_fades)
add_test(NAME scene_fades COMMAND test_scene_fades)
set_tests_properties(scene_fades PROPERTIES
    ENVIRONMENT "ALSOFT_DRIVERS=null;QT_QPA_PLATFORM=offscreen"
    SKIP_RETURN_CODE 77
    TIMEOUT 120)

# The live controls' logic: the talk-over duck, the pad keys and the MIDI map.
add_executable(test_live test_live.cpp ${CMAKE_SOURCE_DIR}/src/Duck.cpp ${CMAKE_SOURCE_DIR}/src/KeyLayout.cpp
    ${CMAKE_SOURCE_DIR}/src/ControlMap.cpp)
target_include_directories(test_live PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_live PRIVATE Qt6::Core)
feedra_warnings(test_live)
add_test(NAME live COMMAND test_live)

# The live controls in the window: pad keys, Space, Esc, Talk.
add_executable(test_live_window test_live_window.cpp)
target_link_libraries(test_live_window PRIVATE feedra_app)
feedra_warnings(test_live_window)
add_test(NAME live_window COMMAND test_live_window)
set_tests_properties(live_window PROPERTIES
    ENVIRONMENT "ALSOFT_DRIVERS=null;QT_QPA_PLATFORM=offscreen"
    TIMEOUT 120)
