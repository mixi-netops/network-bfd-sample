CMAKE_MINIMUM_REQUIRED(VERSION 3.0)

SUBDIRS(deps/smmr/libsmmr)

INCLUDE_DIRECTORIES("./deps/smmr/")
INCLUDE_DIRECTORIES("./deps/netmap/")
INCLUDE_DIRECTORIES("/usr/local/include/")
SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O2 ")

SUBDIRS(bfdd)
SUBDIRS(bfdd_cpp)
SUBDIRS(bfdd_netmap)

