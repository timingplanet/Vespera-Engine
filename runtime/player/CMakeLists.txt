add_executable(vespera_player
    main.cpp
)

target_link_libraries(vespera_player PRIVATE Vespera::Engine)

set_target_properties(vespera_player PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:vespera_player>"
)

add_custom_command(TARGET vespera_player POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/branding"
        "$<TARGET_FILE_DIR:vespera_player>/branding"
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:vespera_player>/legal"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/LICENSE"
        "$<TARGET_FILE_DIR:vespera_player>/legal/Vespera.LICENSE.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/THIRD_PARTY_NOTICES.md"
        "$<TARGET_FILE_DIR:vespera_player>/legal/Vespera.ThirdPartyNotices.md"
    COMMENT "Copying Vespera branding and legal notices"
)

if(WIN32)
    target_sources(vespera_player PRIVATE "${CMAKE_BINARY_DIR}/generated/vespera.rc")
    target_link_libraries(vespera_player PRIVATE user32)
endif()
