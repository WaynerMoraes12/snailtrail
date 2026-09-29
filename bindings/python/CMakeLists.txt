find_package(Python 3.10 REQUIRED COMPONENTS Interpreter Development.Module)
find_package(pybind11 CONFIG REQUIRED)

pybind11_add_module(_native MODULE module.cpp)
target_link_libraries(_native PRIVATE snailtrail::core)
target_compile_features(_native PRIVATE cxx_std_20)

install(TARGETS _native LIBRARY DESTINATION snailtrail)
