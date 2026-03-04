
1. 引入MRTK

```shell
git submodule add https://github.com/leiyx/MicroRT-Kernel.git mrtk
git submodule update --init --remote
```

2. 配置``mrtk_config.h``

3. 配置本项目CMakeLists.txt，链接mrtk_kernel