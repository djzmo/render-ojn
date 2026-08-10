set(CPACK_PACKAGE_NAME "RenderOJN")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_FILE_NAME "RenderOJN-${PROJECT_VERSION}-${RENDEROJN_PACKAGE_SUFFIX}")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGE_CHECKSUM "SHA256")
if(WIN32 OR APPLE)
  set(CPACK_GENERATOR "ZIP")
else()
  set(CPACK_GENERATOR "TGZ")
endif()
include(CPack)

set(_renderojn_cpack_config_args)
if(CMAKE_CONFIGURATION_TYPES)
  list(APPEND _renderojn_cpack_config_args -C $<CONFIG>)
endif()

add_custom_target(renderojn_package
  COMMAND "${CMAKE_CPACK_COMMAND}" ${_renderojn_cpack_config_args}
  COMMAND "${CMAKE_COMMAND}" -DARTIFACT_DIR="${CMAKE_BINARY_DIR}" -P "${CMAKE_CURRENT_LIST_DIR}/WriteSha256Sums.cmake"
  COMMENT "Create the portable archive and SHA256SUMS")
