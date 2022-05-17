#include "pagebuilder.h"

PageBuilder::PageBuilder(string url, int close_log, string doc_root)
{
	m_url = url;
	m_close_log = close_log;
	m_doc_root = doc_root;
}

void PageBuilder::buildViewPage()
{
	// 读取网页框架
	ifstream ifs;
	ifs.open("./root/view.html", ios::in);
	string html = "";
	string buf = "";
	while (getline(ifs, buf)) {
		html += buf;
	}
	ifs.close();

    // 遍历文件夹
	string path = m_doc_root + m_url;
	vector<string> files;
	//文件信息
	struct dirent *filename;
    DIR *dir;
    const char *p = path.c_str();
    dir = opendir(p);
    if (dir == NULL)
    {
        LOG_ERROR("%s","can not open dir !");
    }

    while ((filename = readdir(dir)) != NULL)
    {
		string name = filename->d_name;
        if (strcmp(filename->d_name, ".") == 0|| strcmp(filename->d_name, "..") == 0 || 
			name.find("_mp4tom3u8") != string::npos || name.find(".html") != string::npos)
        {
            continue;
        }
        files.push_back(filename->d_name);
    }
    closedir(dir);
	
	// 建立文件table
	int insertIndex = html.find("<table class=\"filetable\">") + 25;
	int fileNum = files.size();
	html.insert(insertIndex, "</tr>");
	for(int i = 0; i < fileNum; i++)
	{
		if((fileNum - i) % 5 == 0)
		{
			html.insert(insertIndex, "</tr><tr>");
		}
		string ph = files.back();
		string temp_url = m_url;
		files.pop_back();
		html.insert(insertIndex, "<p>" + ph + "</p></div></td>");
		//mp3,mp4页面跳转
		int MP4Loc = ph.find(".mp4");
		int MP3Loc = ph.find(".mp3");
		int DirLoc = ph.find(".");
		string fn = ph;
		if(DirLoc == -1){
			html.insert(insertIndex, "<a href=\""+ temp_url + ph +
				"/\"><img src=\"/htmlSrc/folder.png\" width=\"100\" height=\"100\" border=\"0\"></a>");
		}
		else if (MP4Loc != -1)
		{
			string tph = ph.replace(MP4Loc, 4, VEDIO_SUFFIX);
			// 资源链接
			html.insert(insertIndex, "<a href=\"" + temp_url + ph +
				"\"><img src=\"/htmlSrc/vedio.png\" width=\"100\" height=\"100\" border=\"0\"></a>");
		}	
		else if (MP3Loc != -1) 
		{
			string tph = ph.replace(MP3Loc, 4, MUSIC_SUFFIX);
			// 资源链接
			html.insert(insertIndex, "<a href=\""+ temp_url + ph +
				"\"><img src=\"/htmlSrc/music.png\" width=\"100\" height=\"100\" border=\"0\"></a>");
		}
		else
		{
			html.insert(insertIndex, "<a href=\""+ temp_url + ph +
				"\"><img src=\"/htmlSrc/other.png\" width=\"100\" height=\"100\" border=\"0\"></a>");
		}
		// 删除按钮
		html.insert(insertIndex, "<td align=center><div><UL id=fm><LI><A href=\"#\">...</A><UL><LI><A onclick=\"filedelete('"+ 
			fn +"')\" href=\"#\">删除</button></LI></UL></LI></UL>");

	}
	html.insert(insertIndex, "<tr>");


	//html.insert(html.find("function () {undefined") +22, "\n");
	int position = 500;
	while ((position = html.find("undefined", position)) != string::npos)
	{
		html.insert(position + 9, "\n");
		position++;
	}

	// 保存页面
	ofstream ofs;
	ofs.open(m_doc_root + m_url + "view.html", ios::out);
	ofs << html;
	ofs.close();
}

void PageBuilder::buildVideoPage()
{
	// 读取网页框架
	ifstream ifs;
	ifs.open("./root/video.html", ios::in);
	string html = "";
	string buf = "";
	while (getline(ifs, buf)) {
		html += buf;
	}
	ifs.close();

	// 插入资源
	int urlLoc = m_url.find(VEDIO_SUFFIX);
	int nameLoc = m_url.find_last_of("/");
	string srcName = m_url.substr(nameLoc + 1, urlLoc - nameLoc - 1);
	string src = m_url.substr(0, urlLoc);
	int srcInsertLoc = html.find("source id=\"source\" src=\"\"");
	int titleInsertLoc = html.find("<title>");

	html.insert(srcInsertLoc + 24, m_url.substr(0,nameLoc) + "/" + srcName + "_mp4tom3u8/" + srcName +".m3u8");
	html.insert(titleInsertLoc + 7, srcName);
	// 切片资源
	string mkdirControl = "mkdir " + m_doc_root + m_url.substr(0,nameLoc) + "/" + srcName + "_mp4tom3u8";
	cout << mkdirControl << endl;
	system(mkdirControl.c_str());
	string ffmpegControl1 = "ffmpeg -y -i " + m_doc_root + m_url.substr(0, urlLoc) +
		".mp4  -vcodec copy -acodec copy -vbsf h264_mp4toannexb " + m_doc_root + m_url.substr(0,nameLoc) + "/" + srcName + "_mp4tom3u8/" + srcName + ".ts";
	system(ffmpegControl1.c_str());
	string ffmpegControl2 = "ffmpeg -i " + m_doc_root + m_url.substr(0,nameLoc) + "/" + srcName + "_mp4tom3u8/" + srcName + ".ts -c copy -map 0 -f segment -segment_list " + m_doc_root +
		m_url.substr(0,nameLoc) + "/" + srcName + "_mp4tom3u8/" + srcName + ".m3u8 -segment_time 20 " + m_doc_root + m_url.substr(0, nameLoc) + "/" + srcName + "_mp4tom3u8/" + srcName + "1s_%5d.ts";
	system(ffmpegControl2.c_str());

	// 保存页面
	ofstream ofs;
	ofs.open(m_doc_root + m_url.substr(0, urlLoc) + VEDIO_HTML, ios::out);
	ofs << html;
	ofs.close();
}

void PageBuilder::buildSingPage()
{
	// 读取网页框架
	ifstream ifs;
	ifs.open("./html/music.html", ios::in);
	string html = "";
	string buf = "";
	while (getline(ifs, buf)) {
		html += buf;
	}
	ifs.close();

	// 插入资源
	int urlLoc = m_url.find(MUSIC_SUFFIX);
	int nameLoc = m_url.find_last_of("/");
	string srcName = m_url.substr(nameLoc + 1, urlLoc - nameLoc - 1);
	string src = m_url.substr(0, urlLoc);
	src = src;
	int srcInsertLoc = html.find("audio src=\"\"");
	int titleInsertLoc = html.find("<title>");
	html.insert(srcInsertLoc + 11, src + ".mp3");
	html.insert(titleInsertLoc + 7, srcName);

	// 收藏列表
	string path = "./userconf/music_collection";
	vector<string> files;
	//文件信息
	struct dirent *filename;
    DIR *dir;
    const char *p = path.c_str();
    dir = opendir(p);
    if (dir == NULL)
    {
        LOG_ERROR("%s","can not open dir !");
    }

    while ((filename = readdir(dir)) != NULL)
    {
        if (strcmp(filename->d_name, ".") == 0|| strcmp(filename->d_name, "..") == 0)
        {
            continue;
        }
        files.push_back(filename->d_name);
    }
    closedir(dir);

	int collectInsertLoc = html.find("form action=\"\"");
	html.insert(collectInsertLoc + 13, src + ".mp3" + OPTION + COLLECT + URL_SEG);
	int optionLoc = html.find("<select name=\"collection\">");
	for (string ph : files) {
		string text = ph;
		html.insert(optionLoc + 25, "<option value=\"" + ph + "\">" + text + "</option>");
	}
	// 保存页面
	ofstream ofs;
	ofs.open("." + m_url.substr(0, urlLoc) + MUSIC_HTML, ios::out);
	ofs << html;
	ofs.close();
}