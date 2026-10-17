idf_component_register(
    SRCS
        "src/ed25519_verify.c"
        "src/monocypher.c"
        "src/monocypher-ed25519.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
)
