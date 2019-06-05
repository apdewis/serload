project(serload)
include_directories(../argparse)
link_directories(../argparse)

add_executable(${PROJECT_NAME} serload.c)
target_link_libraries(serload argparse m c)
install(TARGETS ${PROJECT_NAME} DESTINATION host/bin)