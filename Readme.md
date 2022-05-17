# PersonnalNAS

Linux下C++个人云存储服务器。（服务框架参考：[qinguoyi/TinyWebServer: Linux下C++轻量级Web服务器 (github.com)](https://github.com/qinguoyi/TinyWebServer)）

- 使用**线程池**+**非阻塞Socket**+**epoll（LT实现）**+**事件处理（Proactor）**并发模型
- 使用**状态机**解析HTTP报文
- 使用**同步/异步日志系统**记录服务器运行状态
- 前端使用CSS对页面进行美化，使用JavaScript实现功能，上传报文采用**formdata**格式。
- 前端通过递归将文件**切片上传**，后端根据content-length字段动态分配内存，可通过响应状态码要求前端发送下一文件切片或重发当前文件切片。

## 快速开始

- 服务器环境

  - Ubuntu18.04 LTS（windows wsl2 子系统）
  - C++11

- 编译（装上make，直接编译即可）

  ```bash
  make
  ```

- 运行

  ```bash
  ./server
  ```

- 浏览器端（默认端口9006）
  - 如果部署与wsl，注意设置端口转发以及防火墙的入站规则
  ```bash
  ip:9006
  ```

## 个性化运行

```bash
./server [-p port] [-l LOGWrite] [-m TRIGMode] [-o OPT_LINGER] [-s sql_num] [-t thread_num] [-c close_log]
```

- -p，自定义端口号
  - 默认9006
- -l，选择日志写入方式，默认同步写入
  - 0，同步写入
  - 1，异步写入
- -m，listenfd和connfd的模式组合，默认使用LT + LT
  - 0，表示使用LT + LT
  - 1，表示使用LT + ET
  - 2，表示使用ET + LT
  - 3，表示使用ET + ET
- -o，优雅关闭连接，默认不使用
  - 0，不使用
  - 1，使用
- -s，数据库连接数量
  - 默认为8
- -t，线程数量
  - 默认为8
- -c，关闭日志，默认打开
  - 0，打开日志
  - 1，关闭日志

## >>TODO

- 基于m3u8实现在线视频点播功能
- 完善新建文件夹，文件重命名功能
- 提供中文支持（目前上传功能支持中文文件名）
- 提供音乐收藏以及列表播放功能