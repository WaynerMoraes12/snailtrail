find_package(GTest QUIET)

if(NOT GTest_FOUND)
    include(FetchContent)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
        URL_HASH SHA256=7b42b4d6ed48810c5362c265a17faebe90dc2373c885e5216439d37927f02926
        DOWNLOAD_EXTRACT_TIMESTAMP ON)
    FetchContent_MakeAvailable(googletest)
endif()

set(SNAILTRAIL_TEST_SOURCES
    test_strings.cpp
    test_json_writer.cpp
    test_lexer.cpp
    test_fingerprint.cpp
    test_parser.cpp
    test_sql_writer.cpp
    test_schema.cpp
    test_slow_log_parser.cpp
    test_chunker.cpp
)

add_executable(snailtrail_tests ${SNAILTRAIL_TEST_SOURCES})
target_link_libraries(snailtrail_tests PRIVATE snailtrail::core GTest::gtest_main)
target_compile_definitions(snailtrail_tests PRIVATE
    SNAILTRAIL_SAMPLES_DIR="${PROJECT_SOURCE_DIR}/samples")
snailtrail_set_warnings(snailtrail_tests)

include(GoogleTest)
gtest_discover_tests(snailtrail_tests DISCOVERY_MODE PRE_TEST)
