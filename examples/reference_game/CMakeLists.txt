add_executable(vespera_reference_game
    main.cpp
    ../../editor/automation_protocol.cpp
    ../../editor/automation_server.cpp
)

target_link_libraries(vespera_reference_game PRIVATE Vespera::Engine)
target_include_directories(vespera_reference_game PRIVATE "${CMAKE_SOURCE_DIR}/editor")
add_executable(sectorline_reference_game ALIAS vespera_reference_game)
if(WIN32)
    target_link_libraries(vespera_reference_game PRIVATE ws2_32)
endif()

set_target_properties(vespera_reference_game PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:vespera_reference_game>"
)

add_custom_command(TARGET vespera_reference_game POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets"
        "$<TARGET_FILE_DIR:vespera_reference_game>/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/VesperaReference.vesperaproject"
        "$<TARGET_FILE_DIR:vespera_reference_game>/VesperaReference.vesperaproject"
    COMMENT "Copying Vespera reference-game assets"
)
