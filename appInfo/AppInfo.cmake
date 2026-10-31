set(APP_NAME_CODE "${PROJECT_NAME}")
set(APP_ORG_CODE "organization")

# APP版本信息
set(APP_VERSION ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH})
set(APP_VERSION_CODE ${PROJECT_VERSION_MAJOR}${PROJECT_VERSION_MINOR}${PROJECT_VERSION_PATCH})

if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(APP_BUILD_RELEASE 1)
else()
    set(APP_BUILD_RELEASE 0)
endif()

configure_file(
    ${CMAKE_CURRENT_LIST_DIR}/config/AppInfo.h.in
    ${CMAKE_CURRENT_LIST_DIR}/output/AppInfo.h
)

include_directories(${CMAKE_CURRENT_LIST_DIR}/output)

if(MSVC)
    add_compile_options(/utf-8)
    add_link_options(/MANIFEST:NO)
else()
    add_compile_options(-finput-charset=UTF-8 -fexec-charset=UTF-8)
endif()

if(WIN32)
    set(WIN_NAME "com.${APP_ORG_CODE}.${APP_NAME_CODE}")
    set(WIN_DESCRIPTION "${PROJECT_DESCRIPTION}")
    set(WIN_VERSION "${APP_VERSION}.0")
    file(COPY ${CMAKE_CURRENT_LIST_DIR}/config/winLogo.ico DESTINATION ${CMAKE_CURRENT_LIST_DIR}/output)
    configure_file(
        ${CMAKE_CURRENT_LIST_DIR}/config/winManifest.manifest.in
        ${CMAKE_CURRENT_LIST_DIR}/output/winManifest.manifest
    )
    configure_file(
        ${CMAKE_CURRENT_LIST_DIR}/config/winRC.rc.in
        ${CMAKE_CURRENT_LIST_DIR}/output/winRC.rc
    )
    set(APPINFO_SRC ${CMAKE_CURRENT_LIST_DIR}/output/winRC.rc)
endif()
