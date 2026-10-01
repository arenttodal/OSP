add_executable(research-renderer main.cpp)
target_link_libraries(research-renderer PRIVATE osp_research)
osp_set_warnings(research-renderer)
