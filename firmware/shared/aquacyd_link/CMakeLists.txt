idf_component_register(
    SRCS
        "src/aquacyd_link_protocol.cpp"
    INCLUDE_DIRS
        "include"
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_11)
