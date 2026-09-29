add_library(snailtrail_cli_lib STATIC
    src/arguments.cpp
    src/app.cpp
    src/cmd_analyze.cpp
    src/cmd_advise.cpp
    src/cmd_inspect.cpp
    src/cmd_rules.cpp
    src/cmd_generate.cpp
)
target_include_directories(snailtrail_cli_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(snailtrail_cli_lib PUBLIC snailtrail::core)
snailtrail_set_warnings(snailtrail_cli_lib)

add_executable(snailtrail src/main.cpp)
target_link_libraries(snailtrail PRIVATE snailtrail_cli_lib)
snailtrail_set_warnings(snailtrail)

if(SNAILTRAIL_STATIC)
    if(MINGW)
        target_link_options(snailtrail PRIVATE -static)
    elseif(NOT MSVC)
        target_link_options(snailtrail PRIVATE -static-libstdc++ -static-libgcc)
    endif()
endif()

install(TARGETS snailtrail RUNTIME DESTINATION bin)
