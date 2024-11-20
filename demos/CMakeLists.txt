# -----------------------------------------------------------------------------
# Options
# -----------------------------------------------------------------------------
option(BUILD_USEFUL_DEMOS "Build useful demos (hashsum)" FALSE)
option(BUILD_USABLE_DEMOS "Build usable demos (crypt sizes constants pem-info)" FALSE)
option(BUILD_TEST_DEMOS "Build test demos (small tv_gen)" FALSE)

# -----------------------------------------------------------------------------
# Useful demos
# -----------------------------------------------------------------------------

if(BUILD_USEFUL_DEMOS)

    list(APPEND ALL_DEMOS_TARGETS hashsum)

    # hashsum
    add_executable(hashsum ${CMAKE_CURRENT_SOURCE_DIR}/hashsum.c)

    target_link_libraries(hashsum PRIVATE ${PROJECT_NAME})

endif()

# -----------------------------------------------------------------------------
# Usable demos
# -----------------------------------------------------------------------------

if(BUILD_USABLE_DEMOS)

    list(APPEND ALL_DEMOS_TARGETS crypt sizes constants pem-info)

    # ltcrypt
    add_executable(crypt ${CMAKE_CURRENT_SOURCE_DIR}/crypt.c)

    target_link_libraries(crypt PRIVATE ${PROJECT_NAME})

    # sizes
    add_executable(sizes ${CMAKE_CURRENT_SOURCE_DIR}/sizes.c)

    target_link_libraries(sizes PRIVATE ${PROJECT_NAME})

    # constants
    add_executable(constants ${CMAKE_CURRENT_SOURCE_DIR}/constants.c)

    target_link_libraries(constants PRIVATE ${PROJECT_NAME})

    # pem-info
    add_executable(pem-info ${CMAKE_CURRENT_SOURCE_DIR}/pem-info.c)

    target_link_libraries(pem-info PRIVATE ${PROJECT_NAME})

endif()

# -----------------------------------------------------------------------------
# Test demos
# -----------------------------------------------------------------------------

if(BUILD_TEST_DEMOS)

    list(APPEND ALL_DEMOS_TARGETS tv_gen)

    # small
    add_executable(small ${CMAKE_CURRENT_SOURCE_DIR}/small.c)

    target_link_libraries(small PRIVATE ${PROJECT_NAME})

    # tv_gen
    add_executable(tv_gen ${CMAKE_CURRENT_SOURCE_DIR}/tv_gen.c)

    target_link_libraries(tv_gen PRIVATE ${PROJECT_NAME})

endif()

# -----------------------------------------------------------------------------
# Install targets
# -----------------------------------------------------------------------------
install(
    TARGETS ${ALL_DEMOS_TARGETS}
    COMPONENT "runtime"
    EXPORT ${TARGETS_EXPORT_NAME}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
