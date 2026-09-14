add_executable(vespera_packager
    main.cpp
)

target_link_libraries(vespera_packager PRIVATE Vespera::Engine)
target_compile_features(vespera_packager PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(vespera_packager PRIVATE /W4 /permissive- /Zc:__cplusplus)
else()
    target_compile_options(vespera_packager PRIVATE -Wall -Wextra -Wpedantic)
endif()
