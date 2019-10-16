
#include <string.h>
#include <getopt.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <time.h>
#include <math.h>

#include <iostream>
#include <vector>
#include <thread>

#include "ftplib.h"
#include "ftp_benchmark.h"
#include "ftptest_utils.h"
#include "WjCryptLib_Md5.h"

void task_print() {
    long finishedTmp = 0;
    long failedTmp = 0;
    long fileNumTmp = 0;
    long lastFinishNum = 0;
    //long errorSizeTmp = 0;
    long long finishSizeTmp = 0;
    long long lastFinishSize = 0;
    string rateStr;

    char printBuf[1024];
    char sizeStrBuf[128];
    snprintf(printBuf, sizeof(printBuf) - 1,"\n%-19s%-22s%-24s%-24s\n",  "Total File Number", "Finished File Number", 
                                                                  "Transfer files per sec", "Transfer Bytes per sec");
    fwrite(printBuf, 1, strlen(printBuf), normalFd);
    while (!endFlag) {
        memset(sizeStrBuf, 0, sizeof(sizeStrBuf));
        memset(printBuf, 0, sizeof(printBuf));
        finishedTmp = finished;
        failedTmp = failed;
        fileNumTmp = fileNum;
        // errorSizeTmp = errorSize;
        finishSizeTmp = finishSize;
        size_to_string(finishSizeTmp - lastFinishSize, sizeStrBuf);
        rateStr = sizeStrBuf;
        rateStr += "/s";
        snprintf(printBuf, sizeof(printBuf) - 1,"\rtotal/finished/failed files number: %ld/%ld/%ld, throughput: %-16s",
                          fileNumTmp, finishedTmp, failedTmp, rateStr.c_str());
        write(1, printBuf, strlen(printBuf));
        snprintf(printBuf, sizeof(printBuf) - 1,"%-19ld%-22ld%-24ld%-24s\n", fileNumTmp, finishedTmp, finishedTmp - lastFinishNum, rateStr.c_str());
        fwrite(printBuf, 1, strlen(printBuf), normalFd);
        lastFinishSize = finishSizeTmp;
        lastFinishNum = finishedTmp;
        sleep(1);
    }

    memset(sizeStrBuf, 0, sizeof(sizeStrBuf));
    memset(printBuf, 0, sizeof(printBuf));
    finishedTmp = finished;
    failedTmp = failed;
    fileNumTmp = fileNum;
    //errorSizeTmp = errorSize;
    finishSizeTmp = finishSize;
    size_to_string(finishSizeTmp - lastFinishSize, sizeStrBuf);
    rateStr = sizeStrBuf;
    rateStr += "/s";
    snprintf(printBuf, sizeof(printBuf) - 1, "\rtotal/finished/failed files number: %ld/%ld/%ld, throughtout: %-16s",
                       fileNumTmp, finishedTmp, failedTmp, rateStr.c_str());
    write(1, printBuf, strlen(printBuf));    
    snprintf(printBuf, sizeof(printBuf) - 1,"%-19ld%-22ld%-24ld%-24s\n", fileNumTmp, finishedTmp, finishedTmp - lastFinishNum, rateStr.c_str());
    fwrite(printBuf, 1, strlen(printBuf), normalFd);

}

static int connect_and_login_ftp (ftplib* ftpClient, ftpInfo* pFtp, int retryTime) 
{
    int retry = 0;
    static int seeded = 0;
    if (!seeded) {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        srand((unsigned)tv.tv_usec);
        seeded = 1;
    }

    while (true) {
        if (ftpClient->Connect(pFtp->ftpServer)) {
            break;
        }
        
        if (retryTime > 0) {
            retry++;
            if (retry >= retryTime) {
                return -1;
            }
        } else {
            unsigned int sleepTime = rand();
            sleepTime = sleepTime % 31 + 2;
            sleep(sleepTime);
        }
    }
    if (!ftpClient->Login(pFtp->user, pFtp->pass)) {
        return -2;
    }
    return 0;
}

static int build_ftp_data_connection (string accessPath, ftplib::accesstype type, 
                                            ftplib::transfermode mode, ftplib* ftpClient,
                                            ftpInfo* pFtp, ftphandle** nData) 
{
    int retry = 0;
    while (retry < 5) {
        retry++;
        if (!ftpClient->FtpAccess(accessPath.c_str(), type, mode, ftpClient->mp_ftphandle, nData)) {
            if (ftpClient->mp_ftphandle->handle > 0) 
                net_close(ftpClient->mp_ftphandle->handle);
            int ret = connect_and_login_ftp (ftpClient, pFtp, 0);
            if (ret == -2) {  // login failed
                return ret;
            }
        } else {
            return 0;   // get ftp data connection succeed
        }
    }
    return -1;  // retry 5 times but failed
}

static int get_task_from_queue (task<fileInfo>& curTask)
{
    std::unique_lock<std::mutex> lock(tasksMutex);
    while ((tasksQueue.empty()) && (!exitFlag)) {
        tasksExecuteCV.wait(lock);
    }
    if (!tasksQueue.empty()) {
        curTask = tasksQueue.front();
        tasksQueue.pop();
        tasksProductCV.notify_one();
    } else {
        return -1;
    }
    return 0;
}

static int put_task_to_queue (task<fileInfo>& curTask) 
{
    std::unique_lock<std::mutex> lock(tasksMutex);
    while ((tasksQueue.size() >= maxQueueSize) && (liveWorkThread > 0)) {
        tasksProductCV.wait(lock);
    }
    if (liveWorkThread > 0) {
        tasksQueue.push(curTask);
        tasksExecuteCV.notify_one();
    } else {
        return -1;
    }
    return 0;
}

void local2ftp_worker(ftpInfo * pFtp, int id, bool verify) 
{
    string srcPath, dstPath;
    int ioUnit = 256 << 10;
    char *dataBuf = new char[ioUnit];
    char printBuf[1024];
    char sizeStrBuf[128];
    ftplib::accesstype type = ftplib::filewrite;
    ftplib::transfermode mode = ftplib::image;
    FILE *local = NULL;
    ftphandle *nData = NULL;
    char ac[3] = "rb";

    ftplib * ftpClient = new ftplib();
    int ret = connect_and_login_ftp (ftpClient, pFtp, 0);
    if (ret < 0) {
        if (ret == -2)
            fprintf(stderr, "login ftp server %s failed.\n", pFtp->ftpServer);
        --liveWorkThread;
        return;
    }

    while((!tasksQueue.empty()) || (!exitFlag)) {
        task<fileInfo> taskTmp;  
        if (get_task_from_queue(taskTmp) < 0) 
            continue;

        fileInfo srcInfo, dstInfo;
        srcInfo = taskTmp.src;
        dstInfo = taskTmp.dst;
        srcPath = srcInfo.name;
        dstPath = dstInfo.name;
        memset(printBuf, 0, sizeof(printBuf));
        memset(printBuf, 0, sizeof(sizeStrBuf));
        size_to_string(srcInfo.size, sizeStrBuf);
        local = fopen64(srcPath.c_str(), ac);
        if (local == NULL) {
            ++failed;
            errorSize += srcInfo.size;
            sprintf(printBuf, "\nopen source file failed: source: %s, destination: %s, size: %s\n",\
                                                      srcPath.c_str(), dstPath.c_str(), sizeStrBuf);
            fwrite(printBuf, 1, strlen(printBuf), errorFd);
            write(1, printBuf, strlen(printBuf));
            if (failed <= 10) {
                continue;
            } else {
                --liveWorkThread;
                break;
            }
        }
        // get ftp data connection 
        int ret = build_ftp_data_connection(dstPath, type, mode, ftpClient, pFtp, &nData);
        if (ret < 0) {
            ++failed;
            errorSize += srcInfo.size;
            if (ret == -1)
                sprintf(printBuf, "\nget ftp data connection failed: source: %s, destination: %s, size: %s\n",\
                                                                srcPath.c_str(), dstPath.c_str(), sizeStrBuf);
            else
                sprintf(printBuf, "\nre-login ftp server %s failed: source: %s, destination: %s, size: %s\n",\
                                             pFtp->ftpServer , srcPath.c_str(), dstPath.c_str(), sizeStrBuf);
            fwrite(printBuf, 1, strlen(printBuf), errorFd);
            write(2, printBuf, strlen(printBuf));
            if (local != NULL) fclose(local);
            continue;
        }

        Md5Context md5Context;
        if (verify){
            Md5Initialise( &md5Context );
        }


        int readed = 0;
		while ((readed = fread(dataBuf, 1, ioUnit, local)) > 0) {
			if (ftpClient->FtpWrite(dataBuf, readed, nData) < readed) {
                ++failed;
                errorSize += srcInfo.size;
                sprintf(printBuf, "\nftp write failed: source: %s, destination: %s, size: %s\n",\
                                                  srcPath.c_str(), dstPath.c_str(), sizeStrBuf);
                fwrite(printBuf, 1, strlen(printBuf), errorFd);
                write(1, printBuf, strlen(printBuf));
                break;
            }
            if (verify) {
                Md5Update( &md5Context, (const void*)dataBuf, readed);
            }
            finishSize += readed;
        }
        ftpClient->FtpClose(nData);
        char md5str[33] = {'\0'};
        if (readed < 0) {
            ++failed;
            errorSize += srcInfo.size;
            sprintf(printBuf, "\nread source file failed: source: %s, destination: %s, size: %s\n",\
                                                     srcPath.c_str(), dstPath.c_str(), sizeStrBuf);
            fwrite(printBuf, 1, strlen(printBuf), errorFd);
            write(1, printBuf, strlen(printBuf));
        } else {
            ++finished;
            if(verify) {
                MD5_HASH md5;
                char tmp[3] = {'\0'};
                Md5Finalise( &md5Context, &md5);
                memset(md5str, 0, sizeof(md5str));
                for(int j = 0; j < sizeof(md5); j++ ) {
                    sprintf( tmp,"%2.2x", md5.bytes[j]);
                    strcat(md5str, tmp);
                }
            }
        }
        
        fflush(local);
        if (local != NULL) fclose(local);
        
        if (verify) {   // if need to verify, upload a new file just store md5sum
            string newFilePath = dstPath + ".md5";
            ret = build_ftp_data_connection(newFilePath, type, mode, ftpClient, pFtp, &nData);
            if (ret < 0) {
                ++md5Failed;
                if (ret == -1)
                    sprintf(printBuf, "\nget ftp data connection failed: upload md5 file %s\n",\
                                                                          newFilePath.c_str());
                else
                    sprintf(printBuf, "\nre-login ftp server %s failed: upload md5 file %s\n",\
                                                       pFtp->ftpServer , newFilePath.c_str());
                fwrite(printBuf, 1, strlen(printBuf), errorFd);
                write(2, printBuf, strlen(printBuf));
                continue;
            }

            if (ftpClient->FtpWrite(md5str, strlen(md5str), nData) < strlen(md5str)) {
                ++md5Failed;
                sprintf(printBuf, "\nftp upload file %s failed\n", newFilePath.c_str());
                fwrite(printBuf, 1, strlen(printBuf), errorFd);
                write(2, printBuf, strlen(printBuf));
                break;
            }
            ftpClient->FtpClose(nData);
        }

        if (failed > 10) {
            --liveWorkThread;
            break;
        }
    }

    ftpClient->Quit();
    delete []dataBuf;
    delete ftpClient;
}

static void local2ftp_dispatcher_core (ftplib * ftpClient, string localPath, string ftpPath) 
{
    // judge the local path is directory
    if (!is_dir(localPath)) {
        fprintf(stderr, "local path '%s' is not directory.\n", localPath.c_str());
        return;
    }

    // normalize directory path format
    normalize_dir_format(localPath, ftpPath);

    // check if destination dir path exists
    if (!ftpClient->Chdir(ftpPath.c_str())) {
        fprintf(stderr, "change to directory %s failed.\n", ftpPath.c_str());
        return;
    }

    std::queue<task<string> > dirQue;
    task<string> taskTmp;
    taskTmp.src = localPath;
    taskTmp.dst = ftpPath;
    dirQue.push(taskTmp);
    while (!dirQue.empty()) {
        int queSize = dirQue.size();
        for (int i = 0; i < queSize; i++) {
            taskTmp = dirQue.front();
            dirQue.pop();
            string srcPath = taskTmp.src;
            string dstPath = taskTmp.dst;

            // concat and create dst path in ftp
            string baseName = resolve_base_name(srcPath);
            dstPath += baseName;
            if (dstPath.at(dstPath.size() - 1) != '/') {
                dstPath.push_back('/');
            }
            if (!ftpClient->Chdir(dstPath.c_str())) {
                if (!ftpClient->Mkdir(dstPath.c_str())) {
                    fprintf(stderr, "make directory %s failed.\n", dstPath.c_str());
                    return;
                }
            }

            // get files name under the src path
            DIR *dir;
            struct dirent *pDirEnt;
            if ((dir = opendir(srcPath.c_str())) == NULL) {
                fprintf(stderr, "open directory '%s' failed.\n", srcPath.c_str());
                return;
            }

            while ((pDirEnt = readdir(dir)) != NULL) {
                if ((strcmp(pDirEnt->d_name, ".") == 0) || (strcmp(pDirEnt->d_name, "..") == 0))
                    continue;
                else if (pDirEnt->d_type == 4) { // dir
                    string nowSrcPath = srcPath + pDirEnt->d_name + "/";
                    string nowDstPath = dstPath;
                    taskTmp.src = nowSrcPath;
                    taskTmp.dst = nowDstPath;
                    dirQue.push(taskTmp);
                } else if ((pDirEnt->d_type == 8) || (pDirEnt->d_type == 10) ) { 
                    // file or link file , lock and add task
                    fileInfo srcInfo, dstInfo;
                    task<fileInfo> curTask;
                    srcInfo.name = srcPath + pDirEnt->d_name;
                    dstInfo.name = dstPath + pDirEnt->d_name;
                    // statistic the source file information
                    int fdTmp = open(srcInfo.name.c_str(), O_RDONLY);
                    if (fdTmp < 0) {
                        fprintf(stderr, "open file %s failed: %s\n", srcInfo.name.c_str(), strerror(-fdTmp));
                        continue;
                    }
                    struct stat mstat;
                    fstat(fdTmp, &mstat);
                    close(fdTmp);
                    srcInfo.size = dstInfo.size = mstat.st_size;
                    curTask.src = srcInfo;
                    curTask.dst = dstInfo;
                    if (put_task_to_queue(curTask) < 0)
                        break;
                    fileNum++;
                    totalFileSize += mstat.st_size;
                }
            }
            closedir(dir);
        }
    }
}

void local2ftp_dispatcher(ftpInfo * pFtp, string localPath, string ftpPath) 
{
    // normalize directory path format
    normalize_dir_format(localPath, ftpPath);

    // connect to ftp server and login
    ftplib * ftpClient = new ftplib();
    int ret = connect_and_login_ftp (ftpClient, pFtp, 0);
    if (ret < 0) {
        if (ret == -2)
            fprintf(stderr, "login ftp server %s failed.\n", pFtp->ftpServer);
        goto error_out;
    }
    
    // add task to queue
    local2ftp_dispatcher_core(ftpClient, localPath, ftpPath);

error_out:
    ftpClient->Quit();
    exitFlag = 1;
    tasksExecuteCV.notify_all();    
    delete ftpClient;
}

void ftp2local_worker(ftpInfo * pFtp, int id, bool verify) 
{
    string srcPath, dstPath;
    int ioUnit = 256 << 10;
    char * dataBuf = new char[ioUnit];
    char printBuf[1024];
    char sizeStrBuf[128];
    ftplib::accesstype type = ftplib::fileread;
    ftplib::transfermode mode = ftplib::image;
    FILE *local = NULL;
    ftphandle *nData = NULL;
    char ac[3] = "wb";

    ftplib * ftpClient = new ftplib();
    int ret = connect_and_login_ftp (ftpClient, pFtp, 0);
    if (ret < 0) {
        if (ret == -2)
            fprintf(stderr, "login ftp server %s failed.\n", pFtp->ftpServer);
        --liveWorkThread;
        return;
    }

    while((!tasksQueue.empty()) || (!exitFlag)) {
        task<fileInfo> taskTmp;  
        if (get_task_from_queue(taskTmp) < 0) 
            continue;

        fileInfo srcInfo, dstInfo;
        srcInfo = taskTmp.src;
        dstInfo = taskTmp.dst;
        srcPath = srcInfo.name;
        dstPath = dstInfo.name;
        memset(printBuf, 0, sizeof(printBuf));
        memset(printBuf, 0, sizeof(sizeStrBuf));
        size_to_string(srcInfo.size, sizeStrBuf);
		local = fopen64(dstPath.c_str(), ac);
		if (local == NULL) {
            ++failed;
            errorSize += srcInfo.size;
            sprintf(printBuf, "\nopen destination file failed: source: %s, destination: %s, size: %s\n", 
                                                           srcPath.c_str(), dstPath.c_str(), sizeStrBuf);
            fwrite(printBuf, 1, strlen(printBuf), errorFd);
            write(1, printBuf, strlen(printBuf));
            if (failed <= 10) {
                continue;
            } else {
                --liveWorkThread;
                break;
            }
		}
        
        // get ftp data connection 
        int ret = build_ftp_data_connection(srcPath, type, mode, ftpClient, pFtp, &nData);
        if (ret < 0) {
            ++failed;
            errorSize += srcInfo.size;
            if (ret == -1)
                sprintf(printBuf, "\nget ftp data connection failed: source: %s, destination: %s, size: %s\n",\
                                                                srcPath.c_str(), dstPath.c_str(), sizeStrBuf);
            else
                sprintf(printBuf, "\nre-login ftp server %s failed: source: %s, destination: %s, size: %s\n",\
                                             pFtp->ftpServer , srcPath.c_str(), dstPath.c_str(), sizeStrBuf);
            fwrite(printBuf, 1, strlen(printBuf), errorFd);
            write(2, printBuf, strlen(printBuf));
            if (local != NULL) fclose(local);
            continue;
        }

		char checkSumBuf[33] = {'\0'};
		char tmp[3] = {'\0'};
        Md5Context md5Context;
        MD5_HASH md5;
        if (verify){
            memset(checkSumBuf, 0, sizeof(checkSumBuf));
            memset(tmp, 0, sizeof(tmp));
            Md5Initialise( &md5Context );
        }

        int readed = 0;
        while ((readed = ftpClient->FtpRead(dataBuf, ioUnit, nData)) > 0) {
            if (fwrite(dataBuf, 1, readed, local) <= 0) {
                ++failed;
                errorSize += srcInfo.size;
                sprintf(printBuf, "\nwrite destination file failed: source: %s, destination: %s, size: %s\n", 
                                                                srcPath.c_str(), dstPath.c_str(), sizeStrBuf);
                fwrite(printBuf, 1, strlen(printBuf), errorFd);
                write(1, printBuf, strlen(printBuf));
                break;
            }
            if (verify) {
                Md5Update( &md5Context, (const void*)dataBuf, readed);
            }
            finishSize += readed;
        }
        if (readed < 0) {
            ++failed;
            errorSize += srcInfo.size;
            sprintf(printBuf, "\nread source file failed: source: %s, destination: %s, size: %s\n", 
                                                      srcPath.c_str(), dstPath.c_str(), sizeStrBuf);
            fwrite(printBuf, 1, strlen(printBuf), errorFd);
            write(1, printBuf, strlen(printBuf));
        } else {
            ++finished;
            if (verify) {
                Md5Finalise( &md5Context, &md5);
                for(int j = 0; j < sizeof(md5); j++) {
                    sprintf( tmp,"%2.2x", md5.bytes[j]);
                    strcat(checkSumBuf, tmp);
                }
            }
        }
        fflush(local);
        if (local != NULL) fclose(local);
        ftpClient->FtpClose(nData);

        if (readed >= 0 && verify) {
            string newSrcPath = srcPath + ".md5";
            int ret = build_ftp_data_connection(newSrcPath, type, mode, ftpClient, pFtp, &nData);
            if (ret < 0) {
                if (ret == -1)
                    sprintf(printBuf, "\nget ftp data connection failed: read md5 file %s\n", newSrcPath.c_str());
                else
                    sprintf(printBuf, "\nre-login ftp server %s failed: read md5 file %s\n",\
                                                      pFtp->ftpServer , newSrcPath.c_str());
                fwrite(printBuf, 1, strlen(printBuf), errorFd);
                write(2, printBuf, strlen(printBuf));
                continue;
            }

            memset(dataBuf, 0, ioUnit);
            if (ftpClient->FtpRead(dataBuf, ioUnit, nData) <= 0) {
                ++verifyFailed;
                sprintf(printBuf, "\nread md5 file %s failed\n", newSrcPath.c_str());
                fwrite(printBuf, 1, strlen(printBuf), errorFd);
                //write(1, printBuf, strlen(printBuf));
            } else if (strncmp(checkSumBuf, dataBuf, 32) != 0) {
                ++verifyFailed;
				sprintf(printBuf, "\nverify file '%s' failed: upload md5: %s, download md5: %s\n",  
                                                           srcPath.c_str(), dataBuf, checkSumBuf);
                fwrite(printBuf, 1, strlen(printBuf), errorFd);
                write(1, printBuf, strlen(printBuf));
			}
            ftpClient->FtpClose(nData);
        }

        if (failed > 10) {
            --liveWorkThread;
            break;
        }
    }

    ftpClient->Quit();
    delete ftpClient;
    delete [] dataBuf;

}

static void ftp2local_dispatcher_core(ftpInfo * pFtp, ftplib * ftpClient, string ftpPath, string localPath) 
{
    ftplib::accesstype type = ftplib::dirverbose;
    ftplib::transfermode mode = ftplib::ascii;
    int ioUnit = 1 << 10;
    char *dataBuf = static_cast<char*>(malloc(ioUnit));
    char printBuf[1024];
    ftphandle *nData = NULL;

    // check if local path is directory
    if (!is_dir(localPath)) {
        fprintf(stderr, "\nlocal path '%s' is not directory.\n", localPath.c_str());
        return;
    }

    // check if ftp directory exists
    if (!ftpClient->Chdir(ftpPath.c_str())) {
        fprintf(stderr, "change to directory %s failed.\n", ftpPath.c_str());
        return;
    }

    // normalize directory path format
    normalize_dir_format(localPath, ftpPath);
    
    std::queue<task<string> > dirQue;
    task<string> taskTmp;
    taskTmp.src = ftpPath;
    taskTmp.dst = localPath;
    dirQue.push(taskTmp);
    
    while (!dirQue.empty()) {
        int queSize = dirQue.size();
        for (int i = 0; i < queSize; i++) {
            taskTmp = dirQue.front();
            dirQue.pop();
            string srcPath = taskTmp.src;
            string dstPath = taskTmp.dst;
            sprintf(printBuf, "\rlist directory %-45s", srcPath.c_str());
            write(1, printBuf, strlen(printBuf));
            // concat and create dst path at local
            string baseName = resolve_base_name(srcPath);
            dstPath += baseName;
            if (dstPath.at(dstPath.size() - 1) != '/') {
                dstPath.push_back('/');
            }
            if (!opendir(dstPath.c_str())) {
                if (mkdir(dstPath.c_str(), 0777) < 0) {
                    fprintf(stderr, "make directory %s failed.\n", localPath.c_str());
                    return;
                }
            }
            ftpClient->mp_ftphandle->offset = 0;
            int ret = build_ftp_data_connection(srcPath, type, mode, ftpClient, pFtp, &nData);
            if (ret < 0) {
                if (ret == -1)
                    sprintf(printBuf, "List directory %s failed: get ftp data connection failed.\n", srcPath.c_str());
                else
                    sprintf(printBuf, "List directory %s failed: re-login ftp server %s failed.\n",\
                                                                 srcPath.c_str(), pFtp->ftpServer);
                fwrite(printBuf, 1, strlen(printBuf), errorFd);
                write(2, printBuf, strlen(printBuf));
                continue;
            }
    
            while (ftpClient->FtpRead(dataBuf, ioUnit, nData) > 0) {
                string name = "";
                string sizeStr = "";
                resolve_list_entry(dataBuf, name, sizeStr);
                if (name.compare(".") == 0 || name.compare("..") == 0)
                    continue;
                size_t dotLoc = name.find_last_of(".");
                if (dotLoc != std::string::npos && name.substr(dotLoc + 1).compare("md5") == 0)
                    continue;
                if (dataBuf[0] == 'd') {  // the entry is directory
                    string nowSrcPath = srcPath + name + "/";
                    string nowDstPath = dstPath;
                    taskTmp.src = nowSrcPath;
                    taskTmp.dst = nowDstPath;
                    dirQue.push(taskTmp);
                } else if (dataBuf[0] == '-') {  // the entry is file
                    int64_t fileSize = atoi(sizeStr.c_str());
                    fileInfo srcInfo, dstInfo;
                    srcInfo.name = srcPath + name;
                    dstInfo.name = dstPath + name;
                    srcInfo.size = dstInfo.size = fileSize;
                    task<fileInfo> curTask;
                    curTask.src = srcInfo;
                    curTask.dst = dstInfo;
                    if (put_task_to_queue(curTask) < 0)
                        break;
                    ++fileNum;
                    totalFileSize += fileSize;
                }
            }
            ftpClient->FtpClose(nData); // close current data connection for reading next directory
        }
    }
    free(dataBuf);
}

void ftp2local_dispatcher(ftpInfo * pFtp, string ftpPath, string localPath) 
{
    // normalize directory path format
    normalize_dir_format(localPath, ftpPath);

    // connect to ftp server and login
    ftplib * ftpClient = new ftplib();
    int ret = connect_and_login_ftp (ftpClient, pFtp, 0);
    if (ret < 0) {
        if (ret == -2)
            fprintf(stderr, "login ftp server %s failed.\n", pFtp->ftpServer);
        goto error_out;
    }

    ftp2local_dispatcher_core(pFtp, ftpClient, ftpPath, localPath);

error_out:
    exitFlag = 1;
    tasksExecuteCV.notify_all();
    ftpClient->Quit();
    delete ftpClient;
    return;
}

void ftp2null_worker(ftpInfo * pFtp, int id, bool verify) 
{
    string srcPath, dstPath;
    int ioUnit = 256 << 10;
    char * dataBuf = new char[ioUnit];
    char printBuf[1024];
    char sizeStrBuf[128];
    ftplib::accesstype type = ftplib::fileread;
    ftplib::transfermode mode = ftplib::image;
    ftphandle *nData = NULL;

    ftplib * ftpClient = new ftplib();
    int ret = connect_and_login_ftp (ftpClient, pFtp, 0);
    if (ret < 0) {
        if (ret == -2)
            fprintf(stderr, "login ftp server %s failed.\n", pFtp->ftpServer);
        --liveWorkThread;
        return;
    }

    while((!tasksQueue.empty()) || (!exitFlag)) {
        task<fileInfo> taskTmp;
        if (get_task_from_queue(taskTmp) < 0) 
            continue;

        fileInfo srcInfo, dstInfo;
        srcInfo = taskTmp.src;
        dstInfo = taskTmp.dst;
        srcPath = srcInfo.name;
        dstPath = dstInfo.name;
        memset(printBuf, 0, sizeof(printBuf));
        memset(printBuf, 0, sizeof(sizeStrBuf));
        size_to_string(srcInfo.size, sizeStrBuf);
        // get ftp data connection 
        int ret = build_ftp_data_connection(srcPath, type, mode, ftpClient, pFtp, &nData);
        if (ret < 0) {
            ++failed;
            errorSize += srcInfo.size;
            if (ret == -1)
                sprintf(printBuf, "\nget ftp data connection failed: source: %s, destination: null, size: %s\n",\
                                                                                   srcPath.c_str(), sizeStrBuf);
            else
                sprintf(printBuf, "\nre-login ftp server %s failed: source: %s, destination: null, size: %s\n",\
                                                                pFtp->ftpServer , srcPath.c_str(), sizeStrBuf);
            fwrite(printBuf, 1, strlen(printBuf), errorFd);
            write(2, printBuf, strlen(printBuf));
            continue;
        }

		char checkSumBuf[33] = {'\0'};
		char tmp[3] = {'\0'};
        Md5Context md5Context;
        MD5_HASH md5;
        if (verify){
            memset(checkSumBuf, 0, sizeof(checkSumBuf));
            memset(tmp, 0, sizeof(tmp));
            Md5Initialise( &md5Context );
        }

        int readed = 0;
        long long file_size = 0;
        while ((readed = ftpClient->FtpRead(dataBuf, ioUnit, nData)) > 0) {
            finishSize += readed;
            file_size += readed;
            if (verify) {
                Md5Update( &md5Context, (const void*)dataBuf, readed);
            }
        }
        if (readed < 0) {
            ++failed;
            errorSize += srcInfo.size;
            sprintf(printBuf, "\nread source file failed: source: %s, destination: null, size: %s\n",\
                srcPath.c_str(), sizeStrBuf);
            fwrite(printBuf, 1, strlen(printBuf), errorFd);
            write(1, printBuf, strlen(printBuf));
        } else {
            ++finished;
            if (verify) {
                Md5Finalise( &md5Context, &md5);
                for(int j = 0; j < sizeof(md5); j++) {
                    sprintf( tmp,"%2.2x", md5.bytes[j]);
                    strcat(checkSumBuf, tmp);
                }
            }
        }

        ftpClient->FtpClose(nData);

        if(gFtpDebug)
            printf("Thread:%-8ld  sock:%-4d  read file size: %lld\n", syscall(SYS_gettid), nData->handle, file_size);

        if (readed >= 0 && verify) {
            string newSrcPath = srcPath + ".md5";
            int ret = build_ftp_data_connection(newSrcPath, type, mode, ftpClient, pFtp, &nData);
            if (ret < 0) {
                if (ret == -1)
                    sprintf(printBuf, "\nget ftp data connection failed: source: %s, destination: null\n", newSrcPath.c_str());
                else
                    sprintf(printBuf, "\nre-login ftp server %s failed: source: %s, destination: null\n",\
                                                                   pFtp->ftpServer , newSrcPath.c_str());
                fwrite(printBuf, 1, strlen(printBuf), errorFd);
                write(2, printBuf, strlen(printBuf));
                continue;
            }

            memset(dataBuf, 0, ioUnit);
            if (ftpClient->FtpRead(dataBuf, ioUnit, nData) <= 0) {
                ++verifyFailed;
                sprintf(printBuf, "\nread md5 file %s failed\n", newSrcPath.c_str());
                fwrite(printBuf, 1, strlen(printBuf), errorFd);
                //write(1, printBuf, strlen(printBuf));
            } else if (strncmp(checkSumBuf, dataBuf, 32) != 0) {
                ++verifyFailed;
				sprintf(printBuf, "\nverify file '%s' failed: upload md5: %s, download md5: %s\n", \
                                                           srcPath.c_str(), dataBuf, checkSumBuf);
                fwrite(printBuf, 1, strlen(printBuf), errorFd);
                write(1, printBuf, strlen(printBuf));
			}
            ftpClient->FtpClose(nData);
        }

        if (failed > 10) {
            --liveWorkThread;
            break;
        }
    }

    ftpClient->Quit();
    delete ftpClient;
    delete [] dataBuf;

}

static void ftp2null_dispatcher_core(ftpInfo * pFtp, ftplib * ftpClient, string ftpPath, string localPath) 
{
    ftplib::accesstype type = ftplib::dirverbose;
    ftplib::transfermode mode = ftplib::ascii;
    int ioUnit = 1 << 10;
    
    // check if ftp directory exists
    if (!ftpClient->Chdir(ftpPath.c_str())) {
        fprintf(stderr, "change to directory %s failed.\n", ftpPath.c_str());
        return;
    }

    // normalize directory path format
    // localDirPath is "null", do not need to deal with it
    string nullStr("");
    normalize_dir_format(nullStr, ftpPath);
    
    std::queue<task<fileInfo> > tempTaskQue;
    char *dataBuf = static_cast<char*>(malloc(ioUnit));
    char printBuf[1024];
    ftphandle *nData = NULL;
    std::queue<string> dirQue;
    dirQue.push(ftpPath);
    
    while (!dirQue.empty()) {
        int size = dirQue.size();
        for (int i = 0; i < size; i++) {
            string dirPath = dirQue.front();
            dirQue.pop();
            sprintf(printBuf, "\rlist directory %-45s", dirPath.c_str());
            write(1, printBuf, strlen(printBuf));
            ftpClient->mp_ftphandle->offset = 0;
            int ret = build_ftp_data_connection(dirPath, type, mode, ftpClient, pFtp, &nData);
            if (ret < 0) {
                if (ret == -1)
                    sprintf(printBuf, "List directory %s failed: get ftp data connection failed.\n", dirPath.c_str());
                else
                    sprintf(printBuf, "List directory %s failed: re-login ftp server %s failed.\n",\
                                                                 dirPath.c_str(), pFtp->ftpServer);
                fwrite(printBuf, 1, strlen(printBuf), errorFd);
                write(2, printBuf, strlen(printBuf));
                continue;
            }

            // read current direcory
            while (ftpClient->FtpRead(dataBuf, ioUnit, nData) > 0) {
                string name = "";
                string sizeStr = "";
                resolve_list_entry(dataBuf, name, sizeStr);
                if (name.compare(".") == 0 || name.compare("..") == 0)
                    continue;
                size_t dotLoc = name.find_last_of(".");
                if (dotLoc != std::string::npos && name.substr(dotLoc + 1).compare("md5") == 0)
                    continue;
                if (dataBuf[0] == 'd') {  // the entry is directory
                    string nowSrcPath = dirPath + name + "/";
                    dirQue.push(nowSrcPath);
                } else if (dataBuf[0] == '-') {  // the entry is file
                    int64_t fileSize = atoi(sizeStr.c_str());
                    task<fileInfo> taskTmp;
                    fileInfo src, dst;
                    src.name = dirPath + name;
                    dst.name = localPath;
                    src.size = dst.size = size;
                    taskTmp.src = src;
                    taskTmp.dst = dst;  // dst dir is "null"
                    if (put_task_to_queue(taskTmp) < 0)
                        break;
                    ++fileNum;
                    totalFileSize += fileSize;
                }
            }
            ftpClient->FtpClose(nData);  // close this data connection for read next directory
            if (liveWorkThread <= 0)     // if all of workers have exited, dispatcher also exits.
                break;
        }
        if (liveWorkThread <= 0)     // if all of workers have exited, dispatcher also exits.
            break;
    } 
    free(dataBuf);
}


void ftp2null_dispatcher(ftpInfo * pFtp, string ftpPath, string localPath) 
{
    // normalize directory path format
    // localDirPath is null, do not need to deal with it
    string nullStr("");
    normalize_dir_format(nullStr, ftpPath);

    ftplib * ftpClient = new ftplib();
    int ret = connect_and_login_ftp (ftpClient, pFtp, 0);
    if (ret < 0) {
        if (ret == -2)
            fprintf(stderr, "login ftp server %s failed.\n", pFtp->ftpServer);
        goto error_out;
    }

    ftp2null_dispatcher_core(pFtp, ftpClient, ftpPath, localPath);

error_out:
    exitFlag = 1;
    tasksExecuteCV.notify_all();
    ftpClient->Quit();
    delete ftpClient;
}

static void null2ftp_worker_core(recvParam * pRecv, ftplib * ftpClient, string ftpPath, int deep, ftpInfo * pFtp) 
{
    int ret = 0;
    int retry = 0;
    char nameBuf[512];
    int ioUnit = 256 << 10;
    char printBuf[1024];
    char sizeStrBuf[128];
    ftplib::accesstype type = ftplib::filewrite;
    ftplib::transfermode mode = ftplib::image;
    ftphandle *nData = NULL;
    char ac[3] = "rb";

    // normalize directory path format
    string nullStr("");
    normalize_dir_format(nullStr, ftpPath);

    // check if ftp directory exists
    if (!ftpClient->Chdir(ftpPath.c_str())) {
        fprintf(stderr, "change to directory %s failed: %s.\n", ftpPath.c_str(), ftpClient->mp_ftphandle->response);
        return;
    }

    if (deep <= pRecv->layer) {
        for (int i = 0; i < pRecv->width; i++) {
            sprintf(nameBuf, "%s_dir_%d/", pRecv->testName.c_str(), i);
		    string dirPath = ftpPath + nameBuf;
            retry = 0;
            if (!ftpClient->Chdir(dirPath.c_str())) {
                if (!ftpClient->Mkdir(dirPath.c_str())) {
                    fprintf(stderr, "make directory %s failed: %s.\n", dirPath.c_str(), ftpClient->mp_ftphandle->response);
                        continue;
                }
            }
			null2ftp_worker_core(pRecv, ftpClient, dirPath, deep + 1, pFtp);
		}
	} else {        
        char * dataBuf = (char *)malloc(ioUnit);
		if (dataBuf == NULL) {
            fprintf(stderr, "allocate %d memory failed.\n", ioUnit);
			return;
		}
        
        srand(time(NULL));
        for (int i = 0; i < pRecv->fileNum; i++) {
            int64_t osize;
            if (pRecv->maxSize == pRecv->fileSize) {
				osize = rand();
				osize = (osize << 24) + osize;
                osize = (osize%(pRecv->maxSize - pRecv->minSize + 1)) + pRecv->minSize;
            } else {
                osize = pRecv->fileSize;
			}
            //++fileNum;
            totalFileSize += osize;
            size_to_string(osize, sizeStrBuf);
            sprintf(nameBuf, "%s_file_%d", pRecv->testName.c_str(), i);
			string filePath = ftpPath + nameBuf;
            ret = build_ftp_data_connection(filePath, type, mode, ftpClient, pFtp, &nData);
            if (ret < 0) {
                ++failed;
                errorSize += osize;
                if (ret == -1)
                    sprintf(printBuf, "\nget ftp data connection failed: source: null, destination: %s, size: %s\n",\
                                                                                      filePath.c_str(), sizeStrBuf);
                else
                    sprintf(printBuf, "\nre-login ftp server %s failed: source: null, destination: %s, size: %s\n",\
                                                                   pFtp->ftpServer , filePath.c_str(), sizeStrBuf);
                fwrite(printBuf, 1, strlen(printBuf), errorFd);
                write(2, printBuf, strlen(printBuf));
                continue;
            }

            Md5Context md5Context;
            if (pRecv->verify){
                Md5Initialise( &md5Context );
            }

            int upload = 0;
            int ioUnitTmp = ioUnit;
            while (upload < osize) {
                if (ioUnitTmp > osize - upload)
                    ioUnitTmp = osize - upload;
                memset(dataBuf, rand()%94+33, ioUnitTmp);
                if (ftpClient->FtpWrite(dataBuf, ioUnitTmp, nData) < ioUnitTmp) {
                    ++failed;
                    errorSize += osize;
                    sprintf(printBuf, "\nftp write failed: source: null, destination: %s, size: %s\n", filePath.c_str(), sizeStrBuf);
                    fwrite(printBuf, 1, strlen(printBuf), errorFd);
                    write(2, printBuf, strlen(printBuf));
                    break;
                }
                if (pRecv->verify) {
                    Md5Update( &md5Context, (const void*)dataBuf, ioUnitTmp);
                }
                finishSize += ioUnitTmp;
                upload += ioUnitTmp;
            }
            
            char md5str[33] = {'\0'};
            if (upload == osize) {
                ++finished;
                if(pRecv->verify) {
                    MD5_HASH md5;
                    char tmp[3] = {'\0'};
                    Md5Finalise( &md5Context, &md5);
                    memset(md5str, 0, sizeof(md5str));
                    for(int j = 0; j < sizeof(md5); j++ ) {
                        sprintf( tmp,"%2.2x", md5.bytes[j]);
                        strcat(md5str, tmp);
                    }
                }
            }
            
            ftpClient->FtpClose(nData);

            if (pRecv->verify) {   // if need to verify
                string newFilePath = filePath + ".md5";
                ret = build_ftp_data_connection(newFilePath, type, mode, ftpClient, pFtp, &nData);
                if (ret < 0) {
                    ++md5Failed;
                    if (ret == -1)
                        sprintf(printBuf, "\nget ftp data connection failed: source: null, destination: %s\n",\
                                                                                         newFilePath.c_str());
                    else
                        sprintf(printBuf, "\nre-login ftp server %s failed: source: null, destination: %s\n",\
                                                                      pFtp->ftpServer , newFilePath.c_str());
                    fwrite(printBuf, 1, strlen(printBuf), errorFd);
                    write(2, printBuf, strlen(printBuf));
                    continue;
                }
                
                if (ftpClient->FtpWrite(md5str, strlen(md5str), nData) < strlen(md5str)) {
                    ++md5Failed;
                    sprintf(printBuf, "\nftp write failed: source: null, destination: %s\n", newFilePath.c_str());
                    fwrite(printBuf, 1, strlen(printBuf), errorFd);
                    write(2, printBuf, strlen(printBuf));
                    break;
                }
                ftpClient->FtpClose(nData);
            }
            
        }
		free(dataBuf);
    }
	return;
}

void null2ftp_worker(ftpInfo * pFtp, recvParam * pRecv, string ftpPath, int id) 
{
    // normalize directory path format
    string nullStr("");
    normalize_dir_format(nullStr, ftpPath);

    ftplib * ftpClient = new ftplib();
    int ret = connect_and_login_ftp (ftpClient, pFtp, 0);
    if (ret < 0) {
        if (ret == -2)
            fprintf(stderr, "\nthread %d : login ftp server %s failed: %s\n",\
                     id, pFtp->ftpServer, ftpClient->mp_ftphandle->response);
        goto error_out;
    }

    // create work directory
    ftpPath = ftpPath + pRecv->testName +  "_thread_" + to_string(id);
    if (!ftpClient->Chdir(ftpPath.c_str())) {
        if (!ftpClient->Mkdir(ftpPath.c_str())) {
            fprintf(stderr, "make directory %s failed: %s\n",\
                ftpPath.c_str(), ftpClient->mp_ftphandle->response);
            goto error_out;
        }
    }

    null2ftp_worker_core(pRecv, ftpClient, ftpPath, 1, pFtp);	

error_out:
    ftpClient->Quit();
    delete ftpClient;
    --liveWorkThread;
	return;
}


static void usage(char * cmd)
{
    printf("ftp benchmark tool.\n");
    printf("Usage: %s -n <ftp server> -u <user name> -p <password> -s <source path>  -d <destination path> -i\n", cmd);
    printf("options:\n");	
    printf("    -t     # the number of threads\n");
    printf("    -i     # verbose, show ftp messages\n");
    printf("    -v     # verify data correctness\n");
    printf("    -h     # show usage\n");
    printf("Example:\n");
    printf("    %s -n 10.1.1.1:21 -u user -p pass -s local:/home -d ftp:/dir -t 8                 # upload local directory to ftp server\n", cmd);
    printf("    %s -n 10.1.1.1 -u user -p pass -s null -d ftp:/dir -w 3 -l 3 -f 10 -S 64K:1M -t 8 # upload random files to  ftp directory\n", cmd);
    printf("    %s -n 10.1.1.1 -u user -p pass -s ftp:/dir -d local:/home -t 8                    # download files from ftp server to local directory\n", cmd);
    printf("    %s -n ftp.boss.xiaoyun -u user -p pass -s ftp:/dir  -d null -t 16                 # download files from ftp server to /dev/null\n\n", cmd);
    return;
}

int main(int argc, char *argv[])
{
    if (argc == 1) {
        usage(argv[0]);
        return -1;
    }

    string srcDirPath = "";
    string dstDirPath = "";
    string sizeStr = "";
    int threadNum = 8;
    bool recursively = false;
    ftpInfo * pFtp = new ftpInfo();
    recvParam * pRecv = new recvParam();
    char optchar;

    gFtpDebug = false;
    while ((optchar = getopt(argc, argv, "s:d:t:r:n:u:p:w:l:f:S:vih")) != -1) {
        switch(optchar) {
        case 's':
            srcDirPath = optarg;
            break;
        case 'd':
            dstDirPath =  optarg;
            break;
        case 't':
            threadNum = atoi(optarg);
            break;
        case 'i':
            gFtpDebug = true;
            break;
        case 'r':
            recursively = true;
            break;
        case 'n':
            pFtp->ftpServer = optarg;
            break;
        case 'u':
            pFtp->user = optarg;
            pRecv->testName = optarg;
            break;
        case 'p':
            pFtp->pass = optarg;
            break;
        case 'w':
            pRecv->width = atoi(optarg);
            break;
        case 'l':
            pRecv->layer = atoi(optarg);
            break;
        case 'f':
            pRecv->fileNum = atoi(optarg);
            break;
        case 'S':
            sizeStr = optarg;
            break;
        case 'v':
            pRecv->verify = true;
            break;
        case 'h':
            usage(argv[0]);
            exit(0);
        default:
            fprintf(stderr, "unkown option: %c\n", optchar);
            return -1;
        }
    }

    if (pFtp->ftpServer == NULL || strlen(pFtp->ftpServer) == 0) {
        fprintf(stderr, "ftp server not specified.\n");
        return -1;
    }

    if (pFtp->user == NULL || strlen(pFtp->user) == 0) {
        fprintf(stderr, "ftp user not specified.\n");
        return -1;
    }

    if (pFtp->pass == NULL || strlen(pFtp->pass) == 0) {
        fprintf(stderr, "ftp password not specified.\n");
        return -1;
    }

    if (srcDirPath.empty()) {
        fprintf(stderr, "source directory path not specified.\n");
        return -1;
    }

    if (dstDirPath.empty()) {
        fprintf(stderr, " destination directory path not specified.\n");
        return -1;
    }

    if (srcDirPath.compare("null") == 0) {
        if (pRecv->fileNum < 0) {
            fprintf(stderr, " source path is %s, please specify the number of file by option '-f'.\n", srcDirPath.c_str());
            return -1;
        }
        if (sizeStr == "") {
            fprintf(stderr, " source path is %s, please specify file size by option '-S'.\n", srcDirPath.c_str());
            return -1;
        }
        if (pRecv->width < 0) {
            fprintf(stderr, " source path is %s, please specify directry width by option '-w'.\n", srcDirPath.c_str());
            return -1;
        }
        if (pRecv->layer < 0) {
            fprintf(stderr, " source path is %s, please specify directory layer by option '-l'.\n", srcDirPath.c_str());
            return -1;
        }
        size_t colonLoc = sizeStr.find_first_of(":");
		if (colonLoc == std::string::npos) {
            pRecv->fileSize = string_to_size(sizeStr.c_str());
		} else {
			pRecv->minSize = string_to_size(sizeStr.substr(0, colonLoc).c_str());
		    pRecv->maxSize = string_to_size(sizeStr.substr(colonLoc+1).c_str());
	        if ((pRecv->minSize < 0) || (pRecv->maxSize <= 0) || (pRecv->minSize > pRecv->maxSize)) {
	            printf("the value of sizerange is invalid: minsize is %ld, maxsize is %ld.\n", 
					pRecv->minSize, pRecv->maxSize);
				return 0;
	        }
	        pRecv->fileSize = pRecv->maxSize;
		}
    }

    // parse source path and destination path and get the compound mode
    string srcLocation, dstLocation;
    string realSrcPath, realDstPath;
    int compoundMode = 0;  // 1: local to ftp, 2: ftp to local, 3: ftp to null
    int colonLoc = 0;
    colonLoc = srcDirPath.find(":");
    if (colonLoc != std::string::npos) {
        srcLocation = srcDirPath.substr(0, colonLoc);
        realSrcPath = srcDirPath.substr(colonLoc + 1, srcDirPath.size() - colonLoc - 1);
    } else {
        if (srcDirPath.compare("null") != 0) {
            fprintf(stderr, "the format of source path %s is invalid.\n\n", srcDirPath.c_str());
            usage(argv[0]);
            return -1;
        } else {
            realSrcPath = srcDirPath;
        }
    }

    if (dstDirPath.compare("null") != 0) {
        if (dstDirPath.at(dstDirPath.size() - 1) != '/') {
            dstDirPath.push_back('/');
        }
    }

    colonLoc = dstDirPath.find(":");
    if (colonLoc != std::string::npos) {
        dstLocation = dstDirPath.substr(0, colonLoc);
        realDstPath = dstDirPath.substr(colonLoc + 1, dstDirPath.size() - colonLoc - 1);
    } else {
        if (dstDirPath.compare("null") != 0) {
            fprintf(stderr, "the format of destination path %s is invalid.\n\n ", dstDirPath.c_str());
            usage(argv[0]);
            return -1;
        } else {
            realDstPath = dstDirPath;
        }
    }

    if ((srcLocation.compare("local") == 0) && (dstLocation.compare("ftp") == 0)) {
        compoundMode = 1;
    } else if ((srcLocation.compare("ftp") == 0) && (dstLocation.compare("local") == 0)) {
        compoundMode = 2;
    } else if ((srcLocation.compare("ftp") == 0) && (dstDirPath.compare("null") == 0)) {
        compoundMode = 3;
    } else if ((srcDirPath.compare("null") == 0) && (dstLocation.compare("ftp") == 0)) {
        compoundMode = 4;
    } else {
        fprintf(stderr, "the format of source path %s or destination path %s is invalid.\n\n ",\
                                                       srcDirPath.c_str(), dstDirPath.c_str());
        usage(argv[0]);
        return -1;
    }

    // open log file
    normalFd = fopen(normalLogFile, "a");
    if (normalFd == NULL) {
        fprintf(stdout, "open file '%s' failed.\n", normalLogFile);
        return -1;
    }
    errorFd = fopen(errorLogFile, "a");
    if (errorFd == NULL) {
        fprintf(stdout, "open file '%s' failed.\n", errorLogFile);
        return -1;
    }


    char printBuf[1024];
    char * buf = new char[128];
    int len = snprintf(printBuf, sizeof(printBuf) - 1, "\nStarting a new Data Transfer Task.\n");
    snprintf(printBuf + len, sizeof(printBuf) - 1 - len, "source path: %s, destintion path: %s\n", 
                                                           srcDirPath.c_str(), dstDirPath.c_str());
    std::cout << printBuf << std::endl;
    fwrite(printBuf, 1, strlen(printBuf), normalFd);
    
    len = snprintf(printBuf, sizeof(printBuf) - 1, "\nData Transfer Error Information.\n");
    snprintf(printBuf + len, sizeof(printBuf) - 1 - len, "source path: %s, destintion path: %s\n", 
                                                           srcDirPath.c_str(), dstDirPath.c_str());
    fwrite(printBuf, 1, strlen(printBuf), errorFd);
    snprintf(printBuf, sizeof(printBuf), "=========================================================================================\n");
    fwrite(printBuf, 1, strlen(printBuf), normalFd);
    fwrite(printBuf, 1, strlen(printBuf), errorFd);

    std::thread printThread;
    std::thread taskThread;
    std::vector<std::thread> workThreads;

    int deep = 1;
    struct timeval start_time;
    struct timeval end_time;
    gettimeofday(&start_time, NULL);

    // according to compound mode, running corresponding task producer and task executor
    if (compoundMode == 1) {  // source is local, destination is ftp
        taskThread = std::thread(local2ftp_dispatcher, pFtp, realSrcPath, realDstPath);
        for (int i = 0; i < threadNum; i++) {
            workThreads.push_back(std::thread(local2ftp_worker, pFtp, i, pRecv->verify));
            ++liveWorkThread;
        }
    } else if (compoundMode == 2) {  // source is ftp, destination is local
        taskThread = std::thread(ftp2local_dispatcher, pFtp, realSrcPath, realDstPath);
        for (int i = 0; i < threadNum; i++) {
            workThreads.push_back(std::thread(ftp2local_worker, pFtp, i, pRecv->verify));
            ++liveWorkThread;
        }
    } else if (compoundMode == 3) {  // source is ftp, destination is null
        taskThread = std::thread(ftp2null_dispatcher, pFtp, realSrcPath, realDstPath);
        for (int i = 0; i < threadNum; i++) {
            workThreads.push_back(std::thread(ftp2null_worker, pFtp, i, pRecv->verify));
            ++liveWorkThread;
        }
    } else if (compoundMode == 4) {  // source is null, destination is ftp
        fileNum = pow(pRecv->width, pRecv->layer) * pRecv->fileNum * threadNum;
        for (int i = 0; i < threadNum; i++) {
            workThreads.push_back(std::thread(null2ftp_worker, pFtp, pRecv, realDstPath, i));
            ++liveWorkThread;
        }
    }
    printThread = std::thread(task_print);

    if (compoundMode != 4) {
        taskThread.join();
    }
    for (auto & th : workThreads) {
        th.join();
    }
    gettimeofday(&end_time, NULL);

    endFlag = 1;
    printThread.join();

    len = snprintf(printBuf, sizeof(printBuf) - 1, "\n\n---------- statistic information ----------\n");

    long long interval = ((long long)end_time.tv_sec*1000000 + end_time.tv_usec) - ((long long)start_time.tv_sec*1000000 + start_time.tv_usec);
    len += snprintf(printBuf + len, sizeof(printBuf) - 1 - len, "total time: %.2f s\n", ((float)interval / 1000000));

    size_to_string(totalFileSize, buf);
    len += snprintf(printBuf + len, sizeof(printBuf) - 1 - len, "total size: %s (%ld B)\n", buf, totalFileSize);

    int64_t finishSizeTmp = finishSize;
    size_to_string(finishSizeTmp, buf);
    len += snprintf(printBuf + len, sizeof(printBuf) - 1 - len, "transfer size: %s (%ld B)\n", buf, finishSizeTmp);

    long errorSizeTmp = errorSize;
    size_to_string(errorSizeTmp, buf);
    len += snprintf(printBuf + len, sizeof(printBuf) - 1 - len, "failed size: %s (%ld B)\n", buf, errorSizeTmp);

    if (pRecv->verify) {
        if (compoundMode == 1 || compoundMode == 4) {  // ftp is destination, upload
            long md5UploadFailed = md5Failed;
            len += snprintf(printBuf + len, sizeof(printBuf) - 1 - len, "md5 file upload failed: %ld\n", md5UploadFailed);
        } else if (compoundMode == 2 || compoundMode == 3) {  // ftp is source, download
            long verifyFailedNum = verifyFailed;
            len += snprintf(printBuf + len, sizeof(printBuf) - 1 - len, "verify failed: %ld\n", verifyFailedNum);
        }
    }

    int uploadRate = ((double)finishSizeTmp/interval) * 1000000;
    size_to_string(uploadRate, buf);
    snprintf(printBuf + len, sizeof(printBuf) - 1 - len, "average copy rate: %s/s\n\n", buf);
    write(1, printBuf, strlen(printBuf));
    fwrite(printBuf, 1, strlen(printBuf), normalFd);

    long fileNumTmp = fileNum;
    if (failed == 0) {
        long finishedTmp = finished;
        len = snprintf(printBuf, sizeof(printBuf) - 1, "Data Migration Succeeded!\n");
        len += snprintf(printBuf + len, sizeof(printBuf) - 1 - len, "source path: %s, destintion path: %s, total number: %ld, Finished number: %ld\n",
                                                                     srcDirPath.c_str(), dstDirPath.c_str(), fileNumTmp, finishedTmp);
        snprintf(printBuf + len, sizeof(printBuf) - 1 - len, "=========================================================================================\n\n");
        fwrite(printBuf, 1, strlen(printBuf), normalFd);
    } else {
        long failedTmp = failed;
        len = snprintf(printBuf, sizeof(printBuf) - 1, "Data Migration failed!\n");
        len += snprintf(printBuf + len, sizeof(printBuf) - 1 - len, "Please view the file './FtpBenchMarkError.log' for detailed information.\n");
        len += snprintf(printBuf + len, sizeof(printBuf) - 1 - len, "source path: %s, destintion path: %s, total number: %ld, Failed number: %ld\n",
                                                                     srcDirPath.c_str(), dstDirPath.c_str(), fileNumTmp, failedTmp);
        snprintf(printBuf + len, sizeof(printBuf) - 1 - len, "=========================================================================================\n\n");
        fwrite(printBuf, 1, strlen(printBuf), normalFd);
    }
    fwrite(printBuf + len, 1, strlen(printBuf) - len, errorFd);

    if (normalFd) {
        fflush(normalFd);
        fclose(normalFd);
    }
    if (errorFd) {
        fflush(errorFd);
        fclose(errorFd);
    }
    delete [] buf;

    return 0;
}
