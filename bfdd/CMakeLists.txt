CMAKE_MINIMUM_REQUIRED(VERSION 3.0)

SET(PRJ bfdd)
PROJECT(${PRJ})

INCLUDE_DIRECTORIES("../deps/smmr/")
INCLUDE_DIRECTORIES("/usr/local/include/")
SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O2")


SET(SOURCE_FILES
        ${SOURCE_FILES}
        bfdd.c
        bfdd_socket.c
        bfdd_event.c
        bfdd_session.c)

ADD_EXECUTABLE(${PRJ} ${SOURCE_FILES})
TARGET_LINK_LIBRARIES(${PRJ} pthread)
TARGET_LINK_LIBRARIES(${PRJ} smmr)

IF (UNIX AND NOT APPLE)
    TARGET_LINK_LIBRARIES(${PRJ} rt)
ENDIF()

