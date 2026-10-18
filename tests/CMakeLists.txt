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

# The whole window, offscreen: open a project, quit (which saves), check the file.
add_executable(test_project_roundtrip test_project_roundtrip.cpp)
target_link_libraries(test_project_roundtrip PRIVATE feedra_app)
add_test(NAME project_roundtrip COMMAND test_project_roundtrip)
set_tests_properties(project_roundtrip PROPERTIES
    ENVIRONMENT "ALSOFT_DRIVERS=null;QT_QPA_PLATFORM=offscreen"
    TIMEOUT 120)
