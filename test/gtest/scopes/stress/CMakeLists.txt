include_directories(${CMAKE_CURRENT_SOURCE_DIR})

configure_file(Registry.ini.in ${CMAKE_CURRENT_BINARY_DIR}/Registry.ini)
configure_file(Runtime.ini.in ${CMAKE_CURRENT_BINARY_DIR}/Runtime.ini)
configure_file(Zmq.ini.in ${CMAKE_CURRENT_BINARY_DIR}/Zmq.ini)

add_definitions(-DTEST_RUNTIME_PATH="${CMAKE_CURRENT_BINARY_DIR}")
add_definitions(-DTEST_RUNTIME_FILE="${CMAKE_CURRENT_BINARY_DIR}/Runtime.ini")
add_definitions(-DTEST_REGISTRY_PATH="${PROJECT_BINARY_DIR}/scoperegistry")

add_executable(scopes-stress scopes-stress.cpp)
target_link_libraries(scopes-stress ${TESTLIBS})

add_dependencies(scopes-stress scoperegistry scoperunner)

add_test(stress scopes-stress)
add_subdirectory(scopes)
