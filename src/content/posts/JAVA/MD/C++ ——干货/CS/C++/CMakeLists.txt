cmake_minimum_required(VERSION 3.5.0)
project(main VERSION 0.1.0 LANGUAGES C CXX)

include_directories(include) 

# 将找到的源文件列表存储在一个变量中 src文件夹下的所有cpp文件存储在SRC_SUB下
aux_source_directory(src SRC_SUB)
aux_source_directory(. SRC_CUR) 


# 添加src目录下的所有源文件到可执行文件test1
add_executable(main ${SRC_SUB} ${SRC_CUR})

# include(CTest)
# enable_testing()
# set(CPACK_PROJECT_NAME ${PROJECT_NAME})
# set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
# include(CPack)
 