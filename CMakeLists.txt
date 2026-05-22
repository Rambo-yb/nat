cmake_minimum_required(VERSION 2.8)
project(nat)

add_definitions(-DUSE_EASYLOGGER)
add_definitions(-D_GNU_SOURCE)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17")
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/example MAIN_SRC)
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/src SRC)

aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/general_src/common SRC)
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/general_src/http SRC)
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/general_src/list SRC)

aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/third_src/cjson SRC)
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/third_src/easylogger SRC)
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/third_src/mongoose SRC)


include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/general_src/common)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/general_src/http)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/general_src/list)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/third_src/cjson)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/third_src/easylogger)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/third_src/mongoose)

set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/_install/curl/include)
# link_directories(${CMAKE_CURRENT_SOURCE_DIR}/_install/curl/lib)
find_library(CURL_LIB curl PATHS ${CMAKE_CURRENT_SOURCE_DIR}/_install/curl/lib)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/_install/zlib/include)
# link_directories(${CMAKE_CURRENT_SOURCE_DIR}/_install/zlib/lib)
find_library(Z_LIB z PATHS ${CMAKE_CURRENT_SOURCE_DIR}/_install/zlib/lib)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/_install/libdatachannel/include)
# link_directories(${CMAKE_CURRENT_SOURCE_DIR}/_install/libdatachannel/lib)
find_library(DATACHANNEL_LIB datachannel PATHS ${CMAKE_CURRENT_SOURCE_DIR}/_install/libdatachannel/lib)
find_library(JUICE_LIB juice PATHS ${CMAKE_CURRENT_SOURCE_DIR}/_install/libdatachannel/lib)
find_library(SRTP2_LIB srtp2 PATHS ${CMAKE_CURRENT_SOURCE_DIR}/_install/libdatachannel/lib)
find_library(USRSCTP_LIB usrsctp PATHS ${CMAKE_CURRENT_SOURCE_DIR}/_install/libdatachannel/lib)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/_install/openssl/include)
# link_directories(${CMAKE_CURRENT_SOURCE_DIR}/_install/openssl/lib)
find_library(SSL_LIB ssl PATHS ${CMAKE_CURRENT_SOURCE_DIR}/_install/openssl/lib)
find_library(CRYPTO_LIB crypto PATHS ${CMAKE_CURRENT_SOURCE_DIR}/_install/openssl/lib)

if (MEDIA_TYPE STREQUAL "ffmpeg")
add_definitions(-DFFMPEG_ENABLE)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/_install/x264/include)
link_directories(${CMAKE_CURRENT_SOURCE_DIR}/_install/x264/lib)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/_install/ffmpeg/include)
link_directories(${CMAKE_CURRENT_SOURCE_DIR}/_install/ffmpeg/lib)
endif()

add_library(nat_static STATIC ${SRC})
set_target_properties(nat_static PROPERTIES OUTPUT_NAME "nat")
add_library(nat SHARED ${SRC})

target_link_libraries(nat PRIVATE 
		${CURL_LIB} ${Z_LIB} 
		${DATACHANNEL_LIB} ${JUICE_LIB} ${SRTP2_LIB} ${USRSCTP_LIB} 
		${SSL_LIB} ${CRYPTO_LIB})
target_link_libraries(nat_static PRIVATE 
		${CURL_LIB} ${Z_LIB} 
		${DATACHANNEL_LIB} ${JUICE_LIB} ${SRTP2_LIB} ${USRSCTP_LIB} 
		${SSL_LIB} ${CRYPTO_LIB})

add_executable(nat_test ${MAIN_SRC})
target_link_libraries(nat_test nat pthread m dl stdc++)

if (MEDIA_TYPE STREQUAL "ffmpeg")
target_link_libraries(nat_test avdevice avfilter avcodec avformat avutil postproc swresample swscale x264)
endif()

install(TARGETS nat_test DESTINATION bin)
install(TARGETS nat nat_static DESTINATION lib)
install(DIRECTORY include/ DESTINATION include)
# install(FILES example/main.c DESTINATION example)
