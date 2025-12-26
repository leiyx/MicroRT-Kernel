add_subdirectory(Kernel)

if(CMAKE_CROSSCOMPILING)

else()
    # 本地编译DT，不使用交叉编译工具链
    add_subdirectory(Test)
endif()