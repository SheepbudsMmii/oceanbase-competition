#ob_unittest(test_manage_tenant omt/test_manage_tenant.cpp)
storage_unittest(test_worker_pool omt/test_worker_pool.cpp)
storage_unittest(test_hfilter_parser)
storage_unittest(test_query_response_time mysql/test_query_response_time.cpp)

add_subdirectory(rpc EXCLUDE_FROM_ALL)
