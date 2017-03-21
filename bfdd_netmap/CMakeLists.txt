CMAKE_MINIMUM_REQUIRED(VERSION 3.0)

SET(PRJ bfdd_netmap)
PROJECT(${PRJ})

INCLUDE_DIRECTORIES("../deps/netmap/")
INCLUDE_DIRECTORIES("../deps/netmap/sys/")
INCLUDE_DIRECTORIES("/usr/local/include/")
SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O2 ")


SET(SOURCE_FILES
        ${SOURCE_FILES}
        bfdd_netmap.c
        bfdd_netmap_func.c
        )

ADD_EXECUTABLE(${PRJ} ${SOURCE_FILES})
TARGET_LINK_LIBRARIES(${PRJ} pthread)

IF (UNIX AND NOT APPLE)
    TARGET_LINK_LIBRARIES(${PRJ} rt)
ENDIF()

