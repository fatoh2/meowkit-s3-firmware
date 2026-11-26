#pragma once
#include <string>

namespace meow_storage {
template<class FS> bool ensureDirectory(FS& fs,const std::string& path,std::string& error) {
    if (path.empty() || path[0]!='/') { error="Invalid SD folder path"; return false; }
    for (size_t end=1;end<=path.size();++end) {
        if (end<path.size() && path[end]!='/') continue;
        const std::string part=path.substr(0,end);
        if (part=="/") continue;
        if (!fs.exists(part.c_str()) && !fs.mkdir(part.c_str())) {
            error="Cannot create "+part; return false;
        }
        auto folder=fs.open(part.c_str());
        if (!folder || !folder.isDirectory()) { error="Not an SD folder: "+part; return false; }
        folder.close();
    }
    return true;
}
}
