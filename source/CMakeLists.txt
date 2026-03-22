# 搜集所有源文件
set(SOURCES
    ThreadPool.cpp
)

# 创建静态库
add_library(my_thread STATIC ${SOURCES})

# 指定头文件路径
# PUBLIC 表示不仅自己用，链接这个库的项目（如测试）也能自动获取该路径
target_include_directories(my_thread PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../include)