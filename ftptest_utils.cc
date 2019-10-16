#include <sys/stat.h>
#include <string.h>

#include "ftptest_utils.h"

int is_dir(string path) 
{
    struct stat st;
    stat(path.c_str(), &st);
    if (S_ISDIR(st.st_mode)) {
        return 1;
    } else {
        return 0;
    }
}

void size_to_string(uint64_t size, char * buf)
{   
    uint64_t  multiplier = exbibytes;
    int i;

    for (i = 0; i < DIM(sizes); i++, multiplier /= 1024) {   
        if (size < multiplier)
            continue;
        if (size % multiplier == 0)
            sprintf(buf, "%llu %s", (unsigned long long)size / multiplier, sizes[i]);
        else
            sprintf(buf, "%.1f %s", (float) size / multiplier, sizes[i]);
        return ;
    }
    strcpy(buf, "0");
    return ;
}

int64_t string_to_size(const char * str)
{
    char c;
    int64_t num;
    int64_t ret;
    int64_t mult;
    char buf[32];

    strcpy(buf, str);
    
    c = buf[strlen(buf) - 1];
    if (c > '9') {
        buf[strlen(buf) - 1] = '\0';
    }
    double f = atof(buf);
    num = atoll(buf);

    if(isdigit(c))
        return num;
    
    switch(c) {
    case 't':
    case 'T':
        mult = 0x1ULL << 40; //1024 * 1024 * 1024*1024;
	break;
    case 'g':
    case 'G':
        mult = 0x1ULL << 30; //1024 * 1024 * 1024;
	break;
    case 'm':
    case 'M':
        mult = 0x1ULL << 20;//1024 * 1024;
	break;
    case 'k':
    case 'K':
        mult = 0x1ULL << 10;//1024;
	break;
    case 'b':
    case 'B':
        mult = 1;
	break;
    default:
        return -1;
    }

    ret = mult * f;
    return ret;
}

void normalize_dir_format(string& localPath, string& ftpPath) 
{
    if (localPath.length() > 0 && localPath.at(localPath.size() - 1) != '/') {
        localPath.push_back('/');
    }

    if (ftpPath.length() > 0) {
        if (ftpPath.at(0) != '/') {
            ftpPath = "/" + ftpPath;
        }
        if (ftpPath.at(ftpPath.size() - 1) != '/') {
            ftpPath.push_back('/');
        }
    }
    return;
}

void resolve_list_entry (const char * dataBuf, string& name, string& sizeStr) 
{
    if (dataBuf == NULL) return;
    int i = strlen(dataBuf) - 2;
    while (dataBuf[i] != ' ') {
        name = dataBuf[i] + name;
        --i;
    }
    int spaceNum = 0;
    while (spaceNum < 4) {
        if (dataBuf[i] == ' ')
            spaceNum++;
        --i;
    }
    while (dataBuf[i] != ' ') {
        sizeStr = dataBuf[i] + sizeStr;
        --i;
    }
}

string resolve_base_name (const string& srcPath) 
{
    int lastSlashLoc = 0;
    int slashLoc = 0;
    string baseName("");
    while ((slashLoc = srcPath.find('/', lastSlashLoc)) != std::string::npos) {
        if (slashLoc == srcPath.size() - 1) {
            baseName = srcPath.substr(lastSlashLoc, srcPath.size() - lastSlashLoc);
            break;
        }
        lastSlashLoc = slashLoc + 1;
    }
    
    if (baseName.empty()) {
        baseName = srcPath.substr(lastSlashLoc, srcPath.size() - lastSlashLoc);
    }
    return baseName;
}


