cmake_minimum_required(VERSION 3.0)

set(C74_MIN_API_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../lib/min-api)

include(${C74_MIN_API_DIR}/script/min-pretarget.cmake)

find_package(Protobuf REQUIRED)	

add_library( 												
	${PROJECT_NAME} 
	MODULE
	${PROJECT_NAME}.cpp
)	

version_tag(${PROJECT_NAME})

include_directories(${C74_INCLUDES})

include(${C74_MIN_API_DIR}/script/min-posttarget.cmake)

target_link_libraries(${PROJECT_NAME} PUBLIC 					
						${Protobuf_LIBRARIES})

add_dependencies(${PROJECT_NAME} shared_protos)

target_link_libraries(${PROJECT_NAME} PUBLIC shared_protos)
target_link_libraries(${PROJECT_NAME} PUBLIC checked_ptr)
target_link_libraries(${PROJECT_NAME} PUBLIC o_legacy_include)

define_version_tag(${PROJECT_NAME})
liboh_setup(${PROJECT_NAME})