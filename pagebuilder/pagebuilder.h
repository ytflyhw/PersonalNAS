#pragma once
#include "../global.h"
#include <fstream>
#include <algorithm>
#include <string>
#include <vector>
#include <dirent.h>
#include <string.h>
#include "../log/log.h"

using namespace std;

class PageBuilder {
public:

    string m_url;
    string m_doc_root;
    int m_close_log;


    PageBuilder(string url, int close_log, string m_doc_root);

	void buildViewPage();
	void buildVideoPage();
	void buildSingPage();
};