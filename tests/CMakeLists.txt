# Find GTest
find_package(GTest REQUIRED)

# Add test executables
add_executable(rocksdb_kvcache_test rocksdb_kvcache_test.cpp)
target_link_libraries(rocksdb_kvcache_test PRIVATE sldmkvcachestore GTest::GTest GTest::Main)

add_executable(vllm_connector_test vllm_connector_test.cpp)
target_link_libraries(vllm_connector_test PRIVATE sldmkvcachestore GTest::GTest GTest::Main)

# Add tests
add_test(NAME rocksdb_kvcache_test COMMAND rocksdb_kvcache_test)
add_test(NAME vllm_connector_test COMMAND vllm_connector_test)

# Add a target to run all tests
add_custom_target(check
    COMMAND ${CMAKE_CTEST_COMMAND} --verbose
    DEPENDS rocksdb_kvcache_test vllm_connector_test
) 