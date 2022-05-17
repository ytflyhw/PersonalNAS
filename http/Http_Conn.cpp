#include "Http_Conn.h"

char CharToInt(char ch)
{
    if(ch >= '0' && ch <= '9')return (char)(ch - '0');
    if(ch >= 'a' && ch <= 'f')return (char)(ch - 'a' + 10);
    if(ch >= 'A' && ch <= 'F')return (char)(ch - 'A' + 10);
    return -1;
}

char StrToBin(char *str)
{
   char tempWord[2];
   char chn;
 
    tempWord[0] = CharToInt(str[0]);    // make the B to 11 -- 00001011
    tempWord[1] = CharToInt(str[1]);    // make the 0 to 0 -- 00000000
    chn = (tempWord[0] << 4) | tempWord[1];    // to change the BO to 10110000 
     
    return chn;
}
string UrlGB2312Decode(string str)
{
     string output = "";
     char tmp[2];
     int i = 0, idx = 0, ndx, len = str.length();
     
    while(i<len)
     {
         if(str[i] == '%')
         {
             tmp[0] = str[i + 1];
             tmp[1] = str[i + 2];
             output += StrToBin(tmp);
             i = i + 3;
         }
         else if(str[i] == '+')
         {
             output += ' ';
             i++;
         }
         else
         {
             output += str[i];
             i++;
         }
     }
     return output;
 }



//定义http响应的一些状态信息
const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to staisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file form this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the request file.\n";

//将内核事件表注册读事件
void addfd(int epollfd, int fd, bool one_shot);
//对文件描述符设置非阻塞
int setnonblocking(int fd);
//将事件重置为EPOLLONESHOT
void modfd(int epollfd, int fd, int ev);
//从内核时间表删除描述符
void removefd(int epollfd, int fd);
// 工具（char* 匹配 string）
long strlocfind(char* str, long startloc,const char* target, long strlength);

int Http_Conn::m_user_count = 0;
int Http_Conn::m_epollfd = -1;

void Http_Conn::init(int sockfd, const sockaddr_in &addr, char *root, int close_log)
{
    m_sockfd = sockfd;
    m_address = addr;
    m_close_log = close_log;

    addfd(m_epollfd, sockfd, true);     // 将当前连接添加到 epoll 内核事件表

    m_user_count++;

    doc_root = root;

    init();
}

void Http_Conn::init()
{
    m_check_state = CHECK_STATE_REQUESTLINE;
    bytes_to_send = 0;
    bytes_have_send = 0;
    m_method = GET;
    m_url = "";
    m_version = "";
    m_content_length = 0;
    m_host = "";
    m_checked_idx = 0;
    m_start_line= 0;
    m_read_idx = 0;
    m_write_idx = 0;
    m_body_idx = 0;

    memset(m_read_buf, '\0', READ_BUFFER_SIZE);
    memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);
    memset(m_real_file, '\0', FILENAME_LEN);
}

bool Http_Conn::read_once()
{
    // 请求行+请求头溢出缓冲区（请求体会循环接收直至没有数据）
    if (m_read_idx >= READ_BUFFER_SIZE)
    {
        return false;
    }

    // 从内核缓冲区接收数据
    int bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
    m_read_idx += bytes_read;

    if (bytes_read <= 0)
    {
        return false;
    }

    return true;
}

void Http_Conn::process()
{
    HTTP_CODE read_ret = process_read();
    if (read_ret == NO_REQUEST)
    {
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        return;
    }
    bool write_ret = process_write(read_ret);
    if (!write_ret)
    {
        close_conn();
    }
    modfd(m_epollfd, m_sockfd, EPOLLOUT);
}

Http_Conn::HTTP_CODE Http_Conn::process_read()
{
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char* text = 0;

    // 状态机
    while((m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK) || ((line_status = parse_line()) == LINE_OK))
    {
        text = get_line();
        m_start_line = m_checked_idx;
        // LOG_INFO("%s", text);

        switch (m_check_state)
        {
        case CHECK_STATE_REQUESTLINE:
        {
            ret = parse_request_line(text);
            if (ret == BAD_REQUEST)
            {
                return BAD_REQUEST;
            }
            break;
        }
        case CHECK_STATE_HEADER:
        {
            ret = parse_headers(text);
            if (ret == BAD_REQUEST)
                return BAD_REQUEST;
            else if (ret == GET_REQUEST)
            {
                return do_request();
            }
            break;
        }
        case CHECK_STATE_CONTENT:
        {
            ret = parse_content(text);
            if (ret == GET_REQUEST)
                return do_request();
            line_status = LINE_OPEN;
            break;
        }
        default:
            return INTERNAL_ERROR;
        }
    }
    return NO_REQUEST;
}

Http_Conn::HTTP_CODE Http_Conn::parse_content(char* text)
{
    int body_buf_size = min(m_content_length - m_body_idx, m_read_idx - m_checked_idx);
    if(body_buf_size <= 0)
    {
        return NO_REQUEST;
    }
    if(m_body_idx == 0)
    {
        m_body = (char*)malloc(m_content_length);
    }
    
    for(int i = 0; i < body_buf_size; i ++)
    {
        m_body[m_body_idx + i] = m_read_buf[m_checked_idx + i];
    }

    m_body_idx += body_buf_size;
    // 缓冲区清空
    m_read_idx = 0;
    m_checked_idx = 0;
    m_start_line = 0;
    memset(m_read_buf, '\0', READ_BUFFER_SIZE);
    // 请求体已完整接收
    if(m_body_idx >= m_content_length)
    {
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

//从状态机，用于分析出一行内容
//返回值为行的读取状态，有LINE_OK,LINE_BAD,LINE_OPEN
Http_Conn::LINE_STATUS Http_Conn::parse_line()
{
    char temp;
    for (; m_checked_idx < m_read_idx; ++m_checked_idx)
    {
        temp = m_read_buf[m_checked_idx];
        if (temp == '\r')
        {
            if ((m_checked_idx + 1) == m_read_idx)
                return LINE_OPEN;
            else if (m_read_buf[m_checked_idx + 1] == '\n')
            {
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
        else if (temp == '\n')
        {
            if (m_checked_idx > 1 && m_read_buf[m_checked_idx - 1] == '\r')
            {
                m_read_buf[m_checked_idx - 1] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}

//解析http请求行，获得请求方法，目标url及http版本号
Http_Conn::HTTP_CODE Http_Conn::parse_request_line(char *text)
{
    char* url = strpbrk(text, " \t");
    if (!url)
    {
        return BAD_REQUEST;
    }
    *url++ = '\0';
    char *method = text;
    if (strcasecmp(method, "GET") == 0)
    {
        m_method = GET;
    }
    else if (strcasecmp(method, "POST") == 0)
    {
        m_method = POST;
    }
    else
    {
        return BAD_REQUEST;
    }
    url += strspn(url, " \t");
    char* version = strpbrk(url, " \t");
    if (!version)
    {
        return BAD_REQUEST;
    }
    *version++ = '\0';
    version += strspn(version, " \t");
    if (strcasecmp(version, "HTTP/1.1") != 0)
    {
        return BAD_REQUEST;
    }
    if (strncasecmp(url, "http://", 7) == 0)
    {
        url += 7;
        url = strchr(url, '/');
    }

    if (strncasecmp(url, "https://", 8) == 0)
    {
        url += 8;
        url = strchr(url, '/');
    }

    if (!url || url[0] != '/')
    {
        return BAD_REQUEST;
    }
    //当url为/时，显示判断界面
    if (strlen(url) == 1)
        strcat(url, "main.html");
    m_check_state = CHECK_STATE_HEADER;
     // 静态化
    m_url = UrlGB2312Decode(url);
    m_version = version;
    return NO_REQUEST;
}

//解析http请求的一个头部信息
Http_Conn::HTTP_CODE Http_Conn::parse_headers(char *text)
{
    if (text[0] == '\0')
    {
        if (m_content_length != 0)
        {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        return GET_REQUEST;
    }
    else if (strncasecmp(text, "Connection:", 11) == 0)
    {
        text += 11;
        text += strspn(text, " \t");
        if (strcasecmp(text, "keep-alive") == 0)
        {
            m_linger = true;
        }
    }
    else if (strncasecmp(text, "Content-length:", 15) == 0)
    {
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atol(text);
    }
    else if (strncasecmp(text, "Host:", 5) == 0)
    {
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    }
    else if (strncasecmp(text, "Content-type:", 13) == 0)
    {
        text += 13;
        text += strspn(text, " \t");
        m_content_type = text;
    }
    else
    {
        LOG_INFO("oop!unknow header: %s", text);
    }
    
    return NO_REQUEST;
}


bool Http_Conn::process_write(Http_Conn::HTTP_CODE ret)
{
    switch (ret)
    {
    case OPTION_REQUEST:
    {
        add_status_line(200, ok_200_title);
        add_headers(strlen(ok_200_title));
        if (!add_content(ok_200_title))
            return false;
        break;
    }
    case POST_REQUEST:
    {
        add_status_line(200, ok_200_title);
        add_headers(strlen(ok_200_title));
        if (!add_content(ok_200_title))
            return false;
        break;
    }
    case INTERNAL_ERROR:
    {
        add_status_line(500, error_500_title);
        add_headers(strlen(error_500_form));
        if (!add_content(error_500_form))
            return false;
        break;
    }
    case BAD_REQUEST:
    {
        add_status_line(404, error_404_title);
        add_headers(strlen(error_404_form));
        if (!add_content(error_404_form))
            return false;
        break;
    }
    case FORBIDDEN_REQUEST:
    {
        add_status_line(403, error_403_title);
        add_headers(strlen(error_403_form));
        if (!add_content(error_403_form))
            return false;
        break;
    }
    case FILE_REQUEST:
    {
        add_status_line(200, ok_200_title);
        if (m_file_stat.st_size != 0)
        {
            add_headers(m_file_stat.st_size);
            m_iv[0].iov_base = m_write_buf;
            m_iv[0].iov_len = m_write_idx;
            m_iv[1].iov_base = m_file_address;
            m_iv[1].iov_len = m_file_stat.st_size;
            m_iv_count = 2;
            bytes_to_send = m_write_idx + m_file_stat.st_size;
            return true;
        }
        else
        {
            const char *ok_string = "<html><body></body></html>";
            add_headers(strlen(ok_string));
            if (!add_content(ok_string))
                return false;
        }
    }
    default:
    {
        return false;
    }
    }
    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    bytes_to_send = m_write_idx;
    return true;
}

void Http_Conn::close_conn(bool real_close)
{
    if (real_close && (m_sockfd != -1))
    {
        // printf("close %d\n", m_sockfd);
        removefd(m_epollfd, m_sockfd);
        m_sockfd = -1;
        m_user_count--;
    }
}

//从内核时间表删除描述符
void removefd(int epollfd, int fd)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

//将事件重置为EPOLLONESHOT
void modfd(int epollfd, int fd, int ev)
{
    epoll_event event;
    event.data.fd = fd;

    event.events = ev | EPOLLONESHOT | EPOLLRDHUP;

    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

//将内核事件表注册读事件
void addfd(int epollfd, int fd, bool one_shot)
{
    epoll_event event;
    event.data.fd = fd;

    event.events = EPOLLIN | EPOLLRDHUP;

    if (one_shot)
        event.events |= EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

//对文件描述符设置非阻塞
int setnonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

bool Http_Conn::write()
{
    int temp = 0;

    if (bytes_to_send == 0)
    {
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        init();
        return true;
    }

    while (1)
    {
        temp = writev(m_sockfd, m_iv, m_iv_count);

        if (temp < 0)
        {
            if (errno == EAGAIN)
            {
                modfd(m_epollfd, m_sockfd, EPOLLOUT);
                return true;
            }
            unmap();
            return false;
        }

        bytes_have_send += temp;
        bytes_to_send -= temp;
        if (bytes_have_send >= m_iv[0].iov_len)
        {
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
            m_iv[1].iov_len = bytes_to_send;
        }
        else
        {
            m_iv[0].iov_base = m_write_buf + bytes_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - bytes_have_send;
        }

        if (bytes_to_send <= 0)
        {
            unmap();
            modfd(m_epollfd, m_sockfd, EPOLLIN);

            if (m_linger)
            {
                init();
                return true;
            }
            else
            {
                return false;
            }
        }
    }
}

void Http_Conn::unmap()
{
    if (m_file_address)
    {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}

Http_Conn::HTTP_CODE Http_Conn::do_request()
{
    strcpy(m_real_file, doc_root);
    int len = strlen(doc_root);
    // post请求
    if(m_method == POST)
    {
        // 文件操作
        if(strncasecmp(m_content_type.c_str(), "text/plain", 10) == 0)
        {
            if(get_option())
            {
                return OPTION_REQUEST;
            }
            else
            {
                return BAD_REQUEST;
            }
        }
        // 上传文件
        else
        {
            if(post_file())
            {
                return POST_REQUEST;
            }
            else
            {
                return BAD_REQUEST;
            }
        }
    }
    // get请求
    else
    {
        // 请求页面
        if(m_url.find(".") == string::npos || m_url.find(VEDIO_SUFFIX) != string::npos || m_url.find(MUSIC_SUFFIX) != string::npos)
        {
            get_page();
        }

        strncpy(m_real_file + len, m_url.c_str(), m_url.length());
        if (stat(m_real_file, &m_file_stat) < 0)
        {
            return NO_RESOURCE;
        }
        if (!(m_file_stat.st_mode & S_IROTH))
        {
            return FORBIDDEN_REQUEST;
        }
        if (S_ISDIR(m_file_stat.st_mode))
        {
            return BAD_REQUEST;
        }
            
        int fd = open(m_real_file, O_RDONLY);
        m_file_address = (char *)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
        close(fd);
        return FILE_REQUEST;
    }


}

void Http_Conn::get_page()
{
    string html = "";
    // 非主页
    if(m_url != "main.html")
    {
        PageBuilder pg(m_url, m_close_log, doc_root);
		// 播放页
		if (m_url.find(VEDIO_SUFFIX) != string::npos) {
			pg.buildVideoPage();
			m_url = m_url.substr(0, m_url.find(VEDIO_SUFFIX)) + VEDIO_HTML;
		}
		else if (m_url.find(MUSIC_SUFFIX) != string::npos) {
			pg.buildSingPage();
			m_url = m_url.substr(0, m_url.find(MUSIC_SUFFIX)) + MUSIC_HTML;
		}
		// 浏览页
		else{
			pg.buildViewPage();
			m_url = m_url + "view.html";
		}
    }
}

bool Http_Conn::get_option()
{
    string requset_body = m_body;
    LOG_INFO("file option: %s", requset_body);
    free(m_body);
    int cut = requset_body.find(":");
    if(cut != string::npos)
    {
        string option = requset_body.substr(0, cut);
        string file = doc_root + m_url + "'" + requset_body.substr(cut + 1, requset_body.find(";") - cut - 1) + "'";
        if (option == DELETE) {
            // 删除文件
            replace(file.begin(), file.end(), '\\', '/');
            string controller = "rm -rf " + file;
            system(controller.c_str());
            // 视频文件删除需要同时删除播放页以及m3u8文件
            int mp4index = file.find(".mp4");
            if(mp4index != string::npos)
            {
                file = file.replace(mp4index, 4, "play.html", 9);
                string controller1 = "rm -rf " + file;
                system(controller1.c_str());
                file = file.replace(mp4index, 9, "_mp4tom3u8", 10);
                string controller2 = "rm -rf " + file;
                system(controller2.c_str());
            }
        }
        else if(option == MKDIR){
            replace(file.begin(), file.end(), '\\', '/');
            string controller = "mkdir " + file;
            system(controller.c_str());
        }
        else
        {
            return false;
        }
        return true;
    }
    else
    {
        return false;
    }
}

bool Http_Conn::post_file()
{
    File* f = this->analyseRequestBody();
    if(f == nullptr)
    {
        return false;
    }
    string tempPath = doc_root + m_url + f->fileName;
    const char* filePath = tempPath.c_str();

    // 读出原文件
    ofstream ofs;
    ofs.open(filePath, ios::app | ios::binary);
    ofs.write(f->fileData, f->fileSize);
    ofs.close();
    
    free(m_body);

    return true;
}

File* Http_Conn::analyseRequestBody()
{
    File* file = new File();
	// 获取分割符
	int boundaryloc = m_content_type.find("boundary=");
    if(boundaryloc == string::npos)
    {
        return nullptr;
    }
	string boundary = m_content_type.substr(boundaryloc + 9, m_content_type.length() - boundaryloc - 9);
	// 获取文件名
	long fileNameleft = strlocfind(m_body, 0, "filename=", m_content_length) + 10;
	long fileNameright = strlocfind(m_body, fileNameleft, "\"", m_content_length);

	for (long i = fileNameleft; i < fileNameright; i++)
	{
		file->fileName += m_body[i];
	}

	// 获取文件大小
	long dataleft = strlocfind(m_body, fileNameright, "\r\n\r\n", m_content_length) + 4;
	//long dataright = req.bodySize - 42 - 4;
	long dataright = strlocfind(m_body, dataleft, boundary.c_str(), m_content_length) - 4;
	file->fileSize = dataright - dataleft;

	if (dataright + 4 >= m_content_length) {
		return nullptr;
	}
	// 获取文件数据
	file->fileData = &m_body[dataleft];

	return file;
}

long strlocfind(char* str, long startloc,const char* target, long strlength)
{
    int length = strlen(target);
	long i = startloc;
	while (i < strlength)
	{
		bool find = true;
		for (int j = 0; j < length; j++)
		{
			if (str[i + j] != target[j])
			{
				find = false;
				break;
			}
		}
		if (find)
		{
			break;
		}
		i++;
	}
	return i;
}

bool Http_Conn::add_response(const char *format, ...)
{
    if (m_write_idx >= WRITE_BUFFER_SIZE)
        return false;
    va_list arg_list;
    va_start(arg_list, format);
    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list);
    if (len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx))
    {
        va_end(arg_list);
        return false;
    }
    m_write_idx += len;
    va_end(arg_list);

    LOG_INFO("request:%s", m_write_buf);

    return true;
}
bool Http_Conn::add_status_line(int status, const char *title)
{
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}
bool Http_Conn::add_headers(int content_len)
{
    return add_content_length(content_len) && add_linger() &&
           add_blank_line();
}
bool Http_Conn::add_content_length(int content_len)
{
    return add_response("Content-Length:%d\r\n", content_len);
}
bool Http_Conn::add_content_type()
{
    return add_response("Content-Type:%s\r\n", "text/html");
}
bool Http_Conn::add_linger()
{
    return add_response("Connection:%s\r\n", (m_linger == true) ? "keep-alive" : "close");
}
bool Http_Conn::add_blank_line()
{
    return add_response("%s", "\r\n");
}
bool Http_Conn::add_content(const char *content)
{
    return add_response("%s", content);
}