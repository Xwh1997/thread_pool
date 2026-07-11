include(GenerateExportHeader)

add_library(projecta_thread_pool thread_pool.cpp)
add_library(ProjectA::thread_pool ALIAS projecta_thread_pool)

target_compile_features(projecta_thread_pool PUBLIC cxx_std_20)
set_target_properties(projecta_thread_pool PROPERTIES
  CXX_EXTENSIONS OFF
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR}
  EXPORT_NAME thread_pool
)

generate_export_header(projecta_thread_pool
  EXPORT_FILE_NAME ${PROJECT_BINARY_DIR}/generated/projecta/concurrency/export.hpp
  EXPORT_MACRO_NAME PROJECTA_THREAD_POOL_EXPORT
)

target_include_directories(projecta_thread_pool
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/generated>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(projecta_thread_pool PUBLIC Threads::Threads)
projecta_thread_pool_apply_build_options(projecta_thread_pool)
projecta_thread_pool_apply_sanitizers(projecta_thread_pool)

if(NOT BUILD_SHARED_LIBS)
  target_compile_definitions(projecta_thread_pool PUBLIC PROJECTA_THREAD_POOL_STATIC_DEFINE)
endif()

install(TARGETS projecta_thread_pool
  EXPORT ProjectAThreadPoolTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES
  ${PROJECT_BINARY_DIR}/generated/projecta/concurrency/export.hpp
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/projecta/concurrency
)
