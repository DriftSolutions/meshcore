AUX_SOURCE_DIRECTORY(. SRCFILES)

add_executable(meshcore-companion-mqtt ${SRCFILES})
TARGET_LINK_LIBRARIES(meshcore-companion-mqtt ${LIBS})
