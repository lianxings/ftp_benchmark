#ifndef FTPBENCHMARK_H
#define FTPBENCHMARK_H

#include <string>
#include <atomic>
#include <queue>
#include <mutex>
#include <condition_variable>

using std::string;

struct fileInfo {
    string name; int64_t size;
    fileInfo () :name(""), size(0) {}
};

template <typename T> class task
{
public:
    T src; T dst;
    task(){};
    ~task(){};
};

std::queue<task<fileInfo> > tasksQueue;
std::mutex tasksMutex;
std::condition_variable tasksProductCV, tasksExecuteCV;
static const int maxQueueSize = 8192 * 2;
static int64_t totalFileSize = 0;

bool gFtpDebug = false;


FILE * normalFd = NULL;
FILE * errorFd = NULL;
const char * normalLogFile = "./FtpBenchMarkNormal.log";
const char * errorLogFile = "./FtpBenchMarkError.log";

std::atomic<int> exitFlag (0);
std::atomic<int> endFlag (0);
std::atomic<int> liveWorkThread (0);
std::atomic_long fileNum (0);
std::atomic_long finished (0);
std::atomic_long failed (0);
std::atomic_long md5Failed (0);
std::atomic_long verifyFailed (0);
std::atomic_llong finishSize (0);
std::atomic_long errorSize (0);


struct ftpInfo {
    char * ftpServer;
    char * user;
    char * pass;
    ftpInfo() : 
        ftpServer(NULL), 
        user(NULL), 
        pass(NULL)
    {}
};

struct recvParam {
    string testName;
	int width;
	int layer;
	int fileNum;
	int64_t fileSize;
	int64_t maxSize;
	int64_t minSize;
    int  finished;
	bool verify;
    recvParam() : 
        testName(""),
       	width(-1),
    	layer(-1),
    	fileNum(-1),
    	fileSize(0),
    	maxSize(0),
    	minSize(0),
        verify(false)
    {}
};

#endif
