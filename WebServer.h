#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <unistd.h>
#include <cstdlib>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include "http/Http_Conn.h"
#include "threadpool/threadpool.h"
#include "timer/lst_timer.h"

const int MAX_FD = 65536;           // 最大文件描述符
const int MAX_EVENT_NUMBER = 10000; // 最大事件数
const int TIMESLOT = 5;             // 最小超时单位

class WebServer
{
public:
    int m_port;         // 端口
    char* m_root;       // 根目录路径
    int m_log_write;    // 日志模式（同步/异步）
    int m_close_log;    // 是否开启日志

    int m_pipefd[2];    // 本地套接字（用于传输信号）

    Http_Conn *users;   // http 链接池

    // 线程池
    threadpool<Http_Conn> *m_pool;
    int m_thread_num;   // 线程数

    // 定时器
    client_data *users_timer;   
    Utils utils;

    // epoll_event
    int m_epollfd;      // 内核时间表
    int m_listenfd;
    epoll_event m_events[MAX_EVENT_NUMBER];
    int m_LISTENTrigmode;   // 监听触发模式（LT/ET）
    int m_CONNTrigmode;     // 连接触发模式（LT/ET）

public:
    WebServer();
    ~WebServer();

    // 初始化（端口， 日志模式， 开启日志， 线程数量）
    void init(int port, int log_write, int close_log, int thread_num);
    // 线程池
    void thread_pool();
    // 日志
    void log_write();
    // 触发模式
    void trig_mode();
    // 事件监听
    void eventListen();
    // 事件处理
    void eventLoop();

private:
    // 新链接，初始化 client_data,并添加到定时器链表（新链接的文件描述符， 地址）
    void timer(int connfd, struct sockaddr_in client_address);
    // 调整定时器链表
    void adjust_timer(util_timer *timer);

    // 处理新链接
    bool deal_clinetdata();
    // 移除定时器（定时器， 文件描述符）
    void deal_timer(util_timer *timer, int sockfd);
    // 处理信号
    bool deal_signal(bool& timeout, bool& stop_server);
    // 读数据
    void deal_read(int sockfd);
    // 写数据
    void deal_write(int sockfd);

};

#endif