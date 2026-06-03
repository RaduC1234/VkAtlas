#pragma once

#include <string>

class FileDialogs {
public:

    static std::string openFile(const char *filter);

    static std::string saveFile(const char *filter);
};
