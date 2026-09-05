# Game-free browser bootstrap.
#
# This is intentionally separate from PublicProducts.cmake. The latter combines
# the runtime with player-generated translated shards and initialization data;
# public web artifacts must never take those files as inputs.
if(NOT MKW_PLATFORM_WEB)
    message(FATAL_ERROR "Web.cmake may only be included for the Emscripten target")
endif()

if(MKW_BUILD_PRODUCTS)
    message(FATAL_ERROR
        "Translated products are not available for the web bootstrap. "
        "Configure with -DMKW_BUILD_PRODUCTS=OFF.")
endif()

function(mkw_validate_web_public_inputs target)
    cmake_parse_arguments(ARG "" "" "SOURCES" ${ARGN})
    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "${target} must declare its public source inputs")
    endif()

    foreach(source IN LISTS ARG_SOURCES)
        get_filename_component(source_absolute "${source}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_LIST_DIR}/..")
        file(RELATIVE_PATH source_relative "${CMAKE_CURRENT_LIST_DIR}/../.." "${source_absolute}")
        string(REPLACE "\\" "/" source_relative "${source_relative}")
        if(source_relative MATCHES "^\\.\\./" OR
           source_relative MATCHES "^(Assets|generated|PulsarPacks)(/|$)")
            message(FATAL_ERROR
                "${target} is a public web target and cannot consume this input: ${source_relative}")
        endif()
    endforeach()
endfunction()

function(mkw_add_web_public_runtime target)
    cmake_parse_arguments(ARG "" "" "SOURCES" ${ARGN})
    mkw_validate_web_public_inputs(${target} SOURCES ${ARG_SOURCES})
    add_library(${target} STATIC ${ARG_SOURCES})
    target_include_directories(${target} PUBLIC "${CMAKE_CURRENT_LIST_DIR}/../include")
    target_compile_features(${target} PUBLIC cxx_std_17)
    target_compile_definitions(${target} PUBLIC MKW_PLATFORM_WEB=1)
    set_target_properties(${target} PROPERTIES
        UNITY_BUILD OFF
        EXPORT_NAME web_public_runtime)
endfunction()

function(mkw_add_web_public_test target)
    cmake_parse_arguments(ARG "" "" "SOURCES" ${ARGN})
    mkw_validate_web_public_inputs(${target} SOURCES ${ARG_SOURCES})
    add_executable(${target} ${ARG_SOURCES})
    target_compile_features(${target} PRIVATE cxx_std_17)
    target_compile_definitions(${target} PRIVATE MKW_PLATFORM_WEB=1)
endfunction()

mkw_add_web_public_runtime(mkw_web_public_runtime
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/../src/platform/web/web_bootstrap.cpp")

# This is a browser-loadable module, rather than a native CTest executable. It
# exports the ABI function for a JavaScript harness and also verifies the same
# value from its C++ entry point.
mkw_add_web_public_test(mkw_web_public_runtime_tests
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/../tests/web_public_runtime_tests.cpp")
target_link_libraries(mkw_web_public_runtime_tests PRIVATE mkw_web_public_runtime)
set_target_properties(mkw_web_public_runtime_tests PROPERTIES
    OUTPUT_NAME mkw_web_public_runtime_tests
    SUFFIX ".mjs")
target_link_options(mkw_web_public_runtime_tests PRIVATE
    "-sMODULARIZE=1"
    "-sEXPORT_ES6=1"
    "-sEXPORT_NAME=createMkwWebPublicRuntimeTests"
    "-sENVIRONMENT=web,worker,node"
    "-sEXPORTED_FUNCTIONS=['_main','_MkwWebPublicRuntimeAbiVersion']")

mkw_validate_web_public_inputs(mkw_web_public_runtime_tests_browser_harness
    SOURCES "${CMAKE_CURRENT_LIST_DIR}/../tests/web_public_runtime_tests.html.in")
configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/../tests/web_public_runtime_tests.html.in"
    "${CMAKE_CURRENT_BINARY_DIR}/mkw_web_public_runtime_tests.html"
    @ONLY)

mkw_validate_web_public_inputs(mkw_web_public_runtime_tests_node_harness
    SOURCES "${CMAKE_CURRENT_LIST_DIR}/../tests/web_public_runtime_tests.mjs")
enable_testing()
add_test(
    NAME mkw_web_public_runtime_node_tests
    COMMAND "${CMAKE_CROSSCOMPILING_EMULATOR}"
        "${CMAKE_CURRENT_LIST_DIR}/../tests/web_public_runtime_tests.mjs"
        "$<TARGET_FILE:mkw_web_public_runtime_tests>")

add_custom_target(mkw_web_public_runtime_check DEPENDS mkw_web_public_runtime_tests)
message(STATUS "Configured game-free Emscripten web runtime tests")
