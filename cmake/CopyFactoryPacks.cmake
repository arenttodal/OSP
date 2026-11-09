# Copies the factory packs (*.osppack) into one plugin bundle's Contents/Resources/Factory,
# where the plugin installs them from at startup (apps/plugin/Source/LibraryPlaces.cpp,
# factoryPackSource). Run as a post-build step: cmake -DSOURCE=<dir> -DDEST=<dir>
# [-DBUNDLE=<.component/.vst3/.app>] -P CopyFactoryPacks.cmake
#
# The folder is replaced whole, so a pack removed from SOURCE leaves the bundle too. On macOS
# the bundle is signed again (ad hoc) because new resources break the build's own seal; the
# release script signs it again with the Developer ID afterwards.
file(GLOB packs "${SOURCE}/*.osppack")
file(REMOVE_RECURSE "${DEST}")
file(MAKE_DIRECTORY "${DEST}")
foreach(pack IN LISTS packs)
    file(COPY "${pack}" DESTINATION "${DEST}")
endforeach()
list(LENGTH packs count)
message(STATUS "Factory packs: ${count} into ${DEST}")
if(APPLE AND BUNDLE)
    execute_process(COMMAND codesign --force --sign - "${BUNDLE}" RESULT_VARIABLE signed)
    if(NOT signed EQUAL 0)
        message(FATAL_ERROR "codesign failed for ${BUNDLE}")
    endif()
endif()
