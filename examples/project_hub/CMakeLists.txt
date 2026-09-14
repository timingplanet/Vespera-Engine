add_executable(vespera_project_hub
    main.cpp
)

target_link_libraries(vespera_project_hub PRIVATE Vespera::Engine)
if(WIN32)
    target_link_libraries(vespera_project_hub PRIVATE shell32 ole32)
endif()

set_target_properties(vespera_project_hub PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:vespera_project_hub>"
)

add_custom_command(TARGET vespera_project_hub POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/ui"
        "$<TARGET_FILE_DIR:vespera_project_hub>/ui"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/templates"
        "$<TARGET_FILE_DIR:vespera_project_hub>/templates"
    COMMENT "Copying Vespera Project Hub UI and project templates"
)

add_custom_command(TARGET vespera_project_hub POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/branding"
        "$<TARGET_FILE_DIR:vespera_project_hub>/branding"
    COMMENT "Copying Vespera branding"
)

if(WIN32)
    target_sources(vespera_project_hub PRIVATE "${CMAKE_BINARY_DIR}/generated/vespera.rc")
endif()
