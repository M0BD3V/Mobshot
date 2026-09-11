find_package(Qt${QT_VERSION_MAJOR} CONFIG REQUIRED Core)

add_executable(mobshot-privacy-tests
  privacydetector_test.cpp
  ${CMAKE_SOURCE_DIR}/src/utils/privacydetector.cpp)
target_include_directories(mobshot-privacy-tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(mobshot-privacy-tests PRIVATE Qt${QT_VERSION_MAJOR}::Core)
add_test(NAME mobshot-privacy-tests COMMAND mobshot-privacy-tests)
