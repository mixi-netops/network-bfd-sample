CMAKE_MINIMUM_REQUIRED(VERSION 3.0)
SET(PRJ smmr)
PROJECT(${PRJ})

INCLUDE_DIRECTORIES("/usr/local/include/")
INCLUDE_DIRECTORIES(("../"))
SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D__TEST__")

## library
SET(SOURCE_FILES
        ${SOURCE_FILES}
        ../smmr.c
        ../smmr_memory.c
        ../smmr_pagedbuffer.c
        ../smmr_categories.c
        ../smmr_database.c
        ../smmr_tree.c
        )
ADD_LIBRARY(${PRJ} SHARED ${SOURCE_FILES})
TARGET_LINK_LIBRARIES(${PRJ} pthread)
IF (UNIX AND NOT APPLE)
    TARGET_LINK_LIBRARIES(${PRJ} rt)
ENDIF()

INSTALL(TARGETS ${PRJ} LIBRARY DESTINATION lib)

