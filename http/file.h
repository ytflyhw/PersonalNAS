#pragma once
#include <map>
#include <string>

class File {
public:
	std::string fileName;
	std::string fileType;
	long fileSize;

	char* fileData;

	File() {
		this->fileName = "";
		this->fileType = "";
		this->fileSize = 0;
	}
	
};