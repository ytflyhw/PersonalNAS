#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <map>

#include "../lock/locker.h"
#include "../timer/lst_timer.h"
#include "../pagebuilder/pagebuilder.h"
#include "../log/log.h"
#include "../global.h"
#include "file.h"

class Http_Conn
{
public:     
    static const int FILENAME_LEN = 200;        // 文件名缓冲区大小
    static const int READ_BUFFER_SIZE = 2048;   // 读缓冲区大小
    static const int WRITE_BUFFER_SIZE = 1024;  // 写缓冲区大小

    // 请求方法
    enum METHOD{ GET = 0, POST, HEAD, PUT, TRACE, OPTIONS, CONNECT, PATH };
    // 报文状态机
    enum CHECK_STATE{ CHECK_STATE_REQUESTLINE = 0, CHECK_STATE_HEADER, CHECK_STATE_CONTENT};
    // 请求码
    enum HTTP_CODE{ NO_REQUEST, GET_REQUEST, POST_REQUEST, OPTION_REQUEST, BAD_REQUEST, NO_RESOURCE, FORBIDDEN_REQUEST, FILE_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTION};
    // 请求行状态
    enum LINE_STATUS{ LINE_OK, LINE_BAD, LINE_OPEN };

public:
    static int m_epollfd;       // epoll 内核事件表
    static int m_user_count;    // 连接数量
    
private:
    int m_close_log;    // 开启日志

    char *doc_root;     // 根目录路径

    CHECK_STATE m_check_state;  // 状态机

    int m_sockfd;                       // socket 文件描述符
    sockaddr_in m_address;              // 请求地址

    char m_read_buf[READ_BUFFER_SIZE];  // 请求缓冲区
    int m_read_idx;                     // 读缓冲区指针
    int m_checked_idx;                  // 解析指针位置
    int m_start_line;                   // 每行报文的起点
    char m_write_buf[WRITE_BUFFER_SIZE];// 响应缓冲区
    int m_write_idx;

    char m_real_file[FILENAME_LEN];     // 文件名缓冲区

    METHOD m_method;                // 请求方法
    std::string m_url;              // 请求url
    std::string m_version;          // 请求版本
    std::string m_host;
    std::string m_content_type;     // 请求type
    int m_content_length;   // 请求体大小
    bool m_linger;          // 长短连接

    int m_body_idx;         // 请求体指针，表示已经接收的请求体大小
    char* m_body;           // 请求体

    // 发送响应
    struct stat m_file_stat;
    struct iovec m_iv[2];
    int m_iv_count;
    int bytes_to_send;
    int bytes_have_send;
    char *m_file_address;

public:
    Http_Conn() {}
    ~Http_Conn() {  }

    // 连接初始化
    void init(int sockfd, const sockaddr_in &addr, char *root, int close_log);
    // 从内核缓冲区接收一次数据
    bool read_once(); 
    // 向内核缓冲区写数据（发送）
    bool write();
    // 线程池调用入口
    void process();
    // 关闭连接
    void close_conn(bool real_close = true);

    // 获取客户端地址
    sockaddr_in *get_address()
    {
        return &m_address;
    }

private:
    // 初始化
    void init();
    // 处理读
    HTTP_CODE process_read();
    // 处理写
    bool process_write(HTTP_CODE);
    // 获取行起点
    char *get_line() { return m_read_buf + m_start_line; };
    // 判断行状态
    LINE_STATUS parse_line();
    // 解析请求行
    HTTP_CODE parse_request_line(char *text);
    // 解析请求头
    HTTP_CODE parse_headers(char *text);
    // 解析请求体
    HTTP_CODE parse_content(char *text);
    // 处理请求
    HTTP_CODE do_request();

    void unmap();

    // 添加响应
    bool add_response(const char *format, ...);
    bool add_content(const char *content);
    bool add_status_line(int status, const char *title);
    bool add_headers(int content_length);
    bool add_content_type();
    bool add_content_length(int content_length);
    bool add_linger();
    bool add_blank_line();

    // 业务功能
    bool get_option();  // 文件操作
    bool get_file();    // 请求文件（网页）
    bool post_file();   // 上传文件
    void get_page();    // 请求页面
    File* analyseRequestBody();    // 请求体解析
};



#endif