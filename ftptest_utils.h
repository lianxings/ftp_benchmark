#ifndef FTPTESTUTILS_H
#define FTPTESTUTILS_H

#include <string>
using std::string;

#define DIM(x) (sizeof(x)/sizeof(*(x)))
static const char     *sizes[]   = { "EB", "PB", "TB", "GB", "MB", "KB", "B" };
static const uint64_t  exbibytes = 1024ULL * 1024ULL * 1024ULL *
                                   1024ULL * 1024ULL * 1024ULL;

int is_dir(string path);
void size_to_string(uint64_t size, char * buf);
int64_t string_to_size(const char * str);
void normalize_dir_format(string& localPath, string& ftpPath);
void resolve_list_entry (const char * dataBuf, string& name, string& sizeStr);
string resolve_base_name (const string& srcPath);

#endif
