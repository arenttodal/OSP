add_executable(research-renderer main.cpp AndorCommands.cpp)
target_link_libraries(research-renderer PRIVATE osp_research andor_io)
osp_set_warnings(research-renderer)
