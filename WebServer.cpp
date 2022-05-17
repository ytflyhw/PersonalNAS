#include "WebServer.h"

// 构造方法 初始化 root 文件夹路径，定时器链表，用户连接列表
WebServer::WebServer()
{
    // root 文件夹路径
    char server_path[200];
    getcwd(server_path, 200);
    char root[6] = "/root";
    m_root = (char *)malloc(strlen(server_path) + strlen(root) + 1);
    strcpy(m_root, server_path);
    strcat(m_root, root);

    // 定时器
    users_timer = new client_data[MAX_FD];

    // http连接对象
    users = new Http_Conn[MAX_FD];
}

// 析构
WebServer::~WebServer()
{
    close(m_epollfd);
    close(m_listenfd);
    close(m_pipefd[1]);
    close(m_pipefd[0]);

    delete[] users;
    delete[] users_timer;
    delete m_pool;
}

void WebServer::init(int port, int log_write, int close_log, int thread_num)
{
    m_port = port;
    m_log_write = log_write;
    m_close_log = close_log;
    m_thread_num = thread_num;
}

void WebServer::trig_mode()
{
    m_LISTENTrigmode = 0;
    m_CONNTrigmode = 0;
}

void WebServer::log_write()
{
    if (0 == m_close_log)
    {
        //初始化日志
        if (1 == m_log_write)
            Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 800);
        else
            Log::get_instance()->init("./ServerLog", m_close_log, 2000, 800000, 0);
    }
}

void WebServer::thread_pool()
{
    //线程池
    m_pool = new threadpool<Http_Conn>(m_thread_num);
}

void WebServer::eventListen()
{
    //网络编程基础步骤
    m_listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(m_listenfd >= 0);

    struct linger tmp = {1, 1};
    setsockopt(m_listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof(tmp));

    int ret = 0;
    struct sockaddr_in address;
    bzero(&address, sizeof(address));   // address 内容置零
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(m_port);

    int flag = 1;
    setsockopt(m_listenfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));  // 端口复用
    ret = bind(m_listenfd, (struct sockaddr *)&address, sizeof(address));   // 绑定地址以及端口
    assert(ret >= 0);
    ret = listen(m_listenfd, 5);    // 监听
    assert(ret >= 0);

    utils.init(TIMESLOT);

    //epoll创建内核事件表
    // epoll_event events[MAX_EVENT_NUMBER];
    m_epollfd = epoll_create(5);    // 创建 epoll 实例（事件表），其中参数在 Linux2.6.8 后将被忽略，但必须为正
    assert(m_epollfd != -1);

    utils.addfd(m_epollfd, m_listenfd, false, m_LISTENTrigmode);
    Http_Conn::m_epollfd = m_epollfd;   // 事件表

    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_pipefd);    // 创建UNIX域套接字
    assert(ret != -1);
    utils.setnonblocking(m_pipefd[1]);  
    utils.addfd(m_epollfd, m_pipefd[0], false, 0);

    utils.addsig(SIGPIPE, SIG_IGN);
    utils.addsig(SIGALRM, utils.sig_handler, false);
    utils.addsig(SIGTERM, utils.sig_handler, false);

    alarm(TIMESLOT);

    //工具类,信号和描述符基础操作
    Utils::u_pipefd = m_pipefd;
    Utils::u_epollfd = m_epollfd;
}

void WebServer::eventLoop()
{
    bool timeout = false;
    bool stop_server = false;

    while (!stop_server)
    {
        int number = epoll_wait(m_epollfd, m_events, MAX_EVENT_NUMBER, -1);   // events记录epoll检测到的就绪事件
        if (number < 0 && errno != EINTR)
        {
            LOG_ERROR("%s", "epoll failure");
            break;
        }
        
        // 遍历就绪事件
        for (int i = 0; i < number; i++)
        {
            int sockfd = m_events[i].data.fd; 

            //处理新到的客户连接
            if (sockfd == m_listenfd)
            {
                bool flag = deal_clinetdata();
                if (false == flag)
                    continue;
            }
            else if (m_events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                
                //服务器端关闭连接，移除对应的定时器
                util_timer *timer = users_timer[sockfd].timer;
                deal_timer(timer, sockfd);
            }
            //处理信号
            else if ((sockfd == m_pipefd[0]) && (m_events[i].events & EPOLLIN))
            {
                bool flag = deal_signal(timeout, stop_server);
                if (false == flag)
                    LOG_ERROR("%s", "dealclientdata failure");
            }
            //处理客户连接上接收到的数据
            else if (m_events[i].events & EPOLLIN)
            {
                deal_read(sockfd);
            }
            else if (m_events[i].events & EPOLLOUT)
            {
                deal_write(sockfd);
            }
        }
        if (timeout)
        {
            utils.timer_handler();

            LOG_INFO("%s", "timer tick");

            timeout = false;
        }
    }
}

bool WebServer::deal_clinetdata()
{
    struct sockaddr_in client_address;
    socklen_t client_addrlength = sizeof(client_address);

    // LT
    int connfd = accept(m_listenfd, (struct sockaddr *)&client_address, &client_addrlength);
    if (connfd < 0)
    {
        LOG_ERROR("%s:errno is:%d", "accept error", errno);
        return false;
    }
    if (Http_Conn::m_user_count >= MAX_FD)
    {
        utils.show_error(connfd, "Internal server busy");
        LOG_ERROR("%s", "Internal server busy");
        return false;
    }
    timer(connfd, client_address);  // 绑定用户地址，添加定时器链表

    return true;
}

bool WebServer::deal_signal(bool &timeout, bool &stop_server)
{
    int ret = 0;
    int sig;
    char signals[1024];
    ret = recv(m_pipefd[0], signals, sizeof(signals), 0);   // 通过套接字接收信号
    if (ret == -1)
    {
        return false;
    }
    else if (ret == 0)
    {
        return false;
    }
    else
    {
        for (int i = 0; i < ret; ++i)
        {
            switch (signals[i])
            {
            case SIGALRM:
            {
                timeout = true;
                break;
            }
            case SIGTERM:
            {
                stop_server = true;
                break;
            }
            }
        }
    }
    return true;
}

void WebServer::deal_read(int sockfd)
{
    util_timer *timer = users_timer[sockfd].timer;

    //proactor
    if (users[sockfd].read_once())
    {
        LOG_INFO("deal with the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));

        //若监测到读事件，将该事件放入请求队列
        m_pool->append_p(users + sockfd);

        if (timer)
        {
            adjust_timer(timer);
        }
    }
    else
    {
        deal_timer(timer, sockfd);
    }
}

void WebServer::deal_write(int sockfd)
{
    util_timer *timer = users_timer[sockfd].timer;

    //proactor
    if (users[sockfd].write())
    {
        LOG_INFO("send data to the client(%s)", inet_ntoa(users[sockfd].get_address()->sin_addr));
        if (timer)
        {
            adjust_timer(timer);
        }
    }
    else
    {
        deal_timer(timer, sockfd);
    }
}

void WebServer::timer(int connfd, struct sockaddr_in client_address)
{
    // 创建 http 连接
    users[connfd].init(connfd, client_address, m_root, m_close_log);

    //初始化client_data数据
    //创建定时器，设置回调函数和超时时间，绑定用户数据，将定时器添加到链表中
    users_timer[connfd].address = client_address;
    users_timer[connfd].sockfd = connfd;
    util_timer *timer = new util_timer;
    timer->user_data = &users_timer[connfd];
    timer->cb_func = cb_func;
    time_t cur = time(NULL);
    timer->expire = cur + 3 * TIMESLOT;
    users_timer[connfd].timer = timer;
    utils.m_timer_lst.add_timer(timer);
}

//若有数据传输，则将定时器往后延迟3个单位
//并对新的定时器在链表上的位置进行调整
void WebServer::adjust_timer(util_timer *timer)
{
    time_t cur = time(NULL);
    timer->expire = cur + 3 * TIMESLOT;
    utils.m_timer_lst.adjust_timer(timer);

    LOG_INFO("%s", "adjust timer once");
}

void WebServer::deal_timer(util_timer *timer, int sockfd)
{
    timer->cb_func(&users_timer[sockfd]);
    if (timer)
    {
        utils.m_timer_lst.del_timer(timer);
    }

    LOG_INFO("close fd %d", users_timer[sockfd].sockfd);
}