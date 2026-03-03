cmake_minimum_required(VERSION 3.0)
project(TinyWebServer)
set(SRC_LIST
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/config.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/webServer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/CGImysql/sql_connection_pool.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/http/http_conn.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/log/log.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/timer/lst_timer.cpp
)
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/CGImysql
    ${CMAKE_CURRENT_SOURCE_DIR}/http
    ${CMAKE_CURRENT_SOURCE_DIR}/lock
    ${CMAKE_CURRENT_SOURCE_DIR}/log 
    ${CMAKE_CURRENT_SOURCE_DIR}/timer
    ${CMAKE_CURRENT_SOURCE_DIR}/threadPool
)
set(CMAKE_CXX_STANDARD 11)

add_executable(server ${SRC_LIST})

target_link_libraries(server pthread mysqlclient)
