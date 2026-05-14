AUX_SOURCE_DIRECTORY(. SRCFILES)

add_executable(meshcore-mqtt-irc ${SRCFILES})
TARGET_LINK_LIBRARIES(meshcore-mqtt-irc ${LIBS})
