add_executable(vespera_rmlui_spike
    main.cpp
)

target_link_libraries(vespera_rmlui_spike PRIVATE Vespera::Engine)

set_target_properties(vespera_rmlui_spike PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:vespera_rmlui_spike>"
)

add_custom_command(TARGET vespera_rmlui_spike POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/ui"
        "$<TARGET_FILE_DIR:vespera_rmlui_spike>/ui"
    COMMENT "Copying Vespera RmlUi showcase document"
)

add_custom_command(TARGET vespera_rmlui_spike POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/branding"
        "$<TARGET_FILE_DIR:vespera_rmlui_spike>/branding"
    COMMENT "Copying Vespera branding"
)

if(WIN32)
    target_sources(vespera_rmlui_spike PRIVATE "${CMAKE_BINARY_DIR}/generated/vespera.rc")
endif()
