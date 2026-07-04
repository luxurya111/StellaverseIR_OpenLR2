cmake_minimum_required(VERSION 3.23)

project(StellaverseIR_OpenLR2)

find_package(nlohmann_json CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)

set(HTTP_AUTH_PRIVATE_SRC "${CMAKE_SOURCE_DIR}/http_auth/http_auth.cpp")
if(EXISTS "${HTTP_AUTH_PRIVATE_SRC}")
    set(HTTP_AUTH_SRC "${HTTP_AUTH_PRIVATE_SRC}")
    message(STATUS "HTTP auth: http_auth/")
else()
    set(HTTP_AUTH_SRC "${CMAKE_SOURCE_DIR}/src/http_auth_stub.cpp")
    message(STATUS "HTTP auth: public stub (http_auth/ not found)")
endif()

add_library(StellaverseIR SHARED)

set_target_properties(
    StellaverseIR
    PROPERTIES
        CXX_STANDARD 23
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)

target_sources(
    StellaverseIR
    PRIVATE
        ./src/StellaverseIR.cpp
        ./src/http.cpp
        ./src/log.cpp
        ./src/rank.cpp
        ./src/score_db.cpp
        ./src/score_payload.cpp
        "${HTTP_AUTH_SRC}"
)

target_include_directories(
    StellaverseIR
    PUBLIC ./include/
    PRIVATE ./src/
)

target_link_libraries(StellaverseIR PRIVATE nlohmann_json::nlohmann_json)
target_link_libraries(StellaverseIR PRIVATE unofficial::sqlite3::sqlite3)

if(WIN32)
    target_link_libraries(StellaverseIR PRIVATE winhttp bcrypt)
    target_compile_definitions(StellaverseIR PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(ARCH_SUFFIX ".x64")
elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(ARCH_SUFFIX ".x86")
else()
    message(FATAL_ERROR "Unsupported CMAKE_SIZEOF_VOID_P")
endif()

set(CONF_SUFFIX $<$<CONFIG:Debug>:_D>)

set_target_properties(
    StellaverseIR
    PROPERTIES OUTPUT_NAME "StellaverseIR${CONF_SUFFIX}${ARCH_SUFFIX}"
)

install(TARGETS StellaverseIR)
if(MSVC)
    install(FILES $<TARGET_PDB_FILE:StellaverseIR> DESTINATION bin)
endif()
include(CPack)
