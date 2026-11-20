# ANDOR Learn Capture Studio: the scenario runner (development only; see LearnCapture.cpp).
# Links the plugin's shared code as osp_plugin_tests does; adds nothing to the plugin targets.
add_executable(osp_learn_capture LearnCapture.cpp)
target_include_directories(osp_learn_capture PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/apps/plugin/Source
    $<TARGET_PROPERTY:OSP_Plugin,INCLUDE_DIRECTORIES>)
target_compile_definitions(osp_learn_capture PRIVATE
    $<TARGET_PROPERTY:OSP_Plugin,COMPILE_DEFINITIONS>)
target_link_libraries(osp_learn_capture PRIVATE OSP_Plugin osp_dsp juce::juce_cryptography)
