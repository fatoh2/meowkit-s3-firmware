#pragma once
#include <cstdio>
#include <cstdint>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cerrno>

namespace meowgotchi {
// Arduino File::flush() discards sync errors. Use the VFS directly so a full
// or removed card cannot silently count buffered records as saved.
class CaptureFile {
    FILE* _file=nullptr;
public:
    explicit operator bool() const { return _file!=nullptr; }
    bool open(const char* directory,const char* name) {
        if (::mkdir(directory,0777)!=0 && errno!=EEXIST) return false;
        char path[192];
        const int n=snprintf(path,sizeof(path),"%s/%s",directory,name);
        if (n<0 || size_t(n)>=sizeof(path)) return false;
        const int fd=::open(path,O_WRONLY|O_CREAT|O_EXCL,0666);
        if (fd<0) return false;
        _file=fdopen(fd,"wb");
        if (!_file) ::close(fd);
        return _file!=nullptr;
    }
    size_t write(const uint8_t* data,size_t len) { return _file?fwrite(data,1,len,_file):0; }
    bool sync() { return _file && !ferror(_file) && fflush(_file)==0 && fsync(fileno(_file))==0; }
    bool close() {
        if (!_file) return true;
        FILE* file=_file; _file=nullptr; return fclose(file)==0;
    }
};
} // namespace meowgotchi
