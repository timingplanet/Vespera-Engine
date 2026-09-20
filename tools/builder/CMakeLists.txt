add_executable(vespera_builder
    main.cpp
)

target_link_libraries(vespera_builder PRIVATE Vespera::Engine)
target_compile_features(vespera_builder PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(vespera_builder PRIVATE /W4 /permissive- /Zc:__cplusplus)
else()
    target_compile_options(vespera_builder PRIVATE -Wall -Wextra -Wpedantic)
endif()
