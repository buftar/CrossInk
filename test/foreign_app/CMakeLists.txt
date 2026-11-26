add_executable(ForeignAppTest ForeignAppTest.cpp ${REPO_ROOT}/src/network/ForeignApp.cpp)
target_include_directories(ForeignAppTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/stubs ${REPO_ROOT}/src/network)
target_link_libraries(ForeignAppTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(ForeignAppTest)
