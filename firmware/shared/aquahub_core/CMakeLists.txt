idf_component_register(
    SRCS
        "src/aquahub_registry.cpp"
        "src/aquahub_automation.cpp"
    INCLUDE_DIRS
        "include"
)

target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall
    -Wextra
    -Werror
)
