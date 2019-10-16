# ftp_benchmark
这是一个C/C++版本的ftp benchmark工具，运行环境为linux系统，方便对ftp server进行高并发测试。

采用单生产者-多消费者模型进行并发数据传输，生产者列举目录获得文件信息，放入队列，消费者从队列中取出文件信息，执行数据传输任务。

目前支持4种传输方式：
- 将ftp server指定目录下的所有文件下载到本地指定目录。
- 将本地指定目录下的所有文件上传到ftp server指定目录。
- 将ftp server指定目录下的所有文件下载到本地内存，不落盘。
- 内存产生随机大小文件上传到ftp server的指定目录下。

另外，该工具选项 -v 通过MD5对文件数据正确性进行检测，该功能要求上传和下载时都要使用选项 -v。

## 引用说明
该工具使用[ftplibpp](https://github.com/mkulke/ftplibpp)作为ftp客户端库。

使用的MD5算法库来自于[WjCryptLib](https://github.com/WaterJuice/WjCryptLib)。

## 编译安装
```
make
make install
```

## 使用说明
假定ftp server地址为192.168.1.200，用户名为user，密码为passwd
- 内存产生随机大小文件上传到ftp server的指定目录
    - 选项 -s 指定源目录，null表示内存产生随机大小的数据文件
    - 选项 -d 指定目的目录，ftp:/workdir/dir1表示ftp server的/workdir/dir1为目的目录
    - 选项 -w 指定目录树每层的宽度
    - 选项 -l 指定目录树的深度
    - 选项 -f 指定每个最深目录下创建的文件个数
    - 选项 -S 指定文件的大小范围。20K:100K表示大小从20KB到100KB随机，100K表示所有产生的文件大小都为100KB
    - 选项 -t 指定工作线程数
```
# ftp_benchmark -n 192.168.1.200 -u user -p passwd -s null -d ftp:/workdir/dir1 -w 3 -l 1 -f 100 -S 20K:100K -t 5

Starting a new Data Transfer Task.
source path: null, destintion path: ftp:/workdir/dir3/

total/finished/failed files number: 1500/1500/0, throughtout: 5.3 MB/s        

---------- statistic information ----------
total time: 4.31 s
total size: 86.6 MB (90837442 B)
transfer size: 86.6 MB (90837442 B)
failed size: 0 (0 B)
average copy rate: 20.1 MB/s
```
- 将ftp server指定目录下的所有文件下载到本地内存，不落盘
    - 选项 -d 为null，表示下载到本地的数据不进行落盘操作
```
# ftp_benchmark -n 192.168.1.200 -u user -p passwd -s ftp:/workdir/dir3 -d null -t 8

Starting a new Data Transfer Task.
source path: ftp:/workdir/dir3, destintion path: null

total/finished/failed files number: 1500/1500/0, throughtout: 11.6 MB/s       

---------- statistic information ----------
total time: 1.13 s
total size: 86.6 MB (90837442 B)
transfer size: 86.6 MB (90837442 B)
failed size: 0 (0 B)
average copy rate: 76.5 MB/s
```
- 将ftp server指定目录下的所有文件下载到本地指定目录
```
# mkdir tmp
# ftp_benchmark -n 192.168.1.200 -u user -p passwd -s ftp:/workdir/dir3 -d local:./tmp -t 8

Starting a new Data Transfer Task.
source path: ftp:/workdir/dir3, destintion path: local:./tmp/

total/finished/failed files number: 1500/1500/0, throughtout: 4.4 MB/s        

---------- statistic information ----------
total time: 7.43 s
total size: 86.6 MB (90837442 B)
transfer size: 86.6 MB (90837442 B)
failed size: 0 (0 B)
average copy rate: 11.7 MB/s
```
- 将本地指定目录下的所有文件上传到ftp server指定目录
```
# ftp_benchmark -n 192.168.1.200 -u user -p passwd -s local:./tmp -d ftp:/workdir -t 16

Starting a new Data Transfer Task.
source path: local:./tmp, destintion path: ftp:/workdir/

total/finished/failed files number: 1500/1500/0, throughtout: 2.3 MB/s        

---------- statistic information ----------
total time: 4.12 s
total size: 86.6 MB (90837442 B)
transfer size: 86.6 MB (90837442 B)
failed size: 0 (0 B)
average copy rate: 21.1 MB/s
```
- 使用选项 -v 则会计算每一个文件的MD5值，并将其MD5值作为一个单独的文件上传到与原始数据文件相同的目录下。例如使用如下命令上传数据。
```
# ftp_benchmark -n 192.168.20.183 -u slx -p passwd -s null -d ftp:/workdir/dir3 -w 3 -l 1 -f 100 -S 20K:100K -t 5 -v
Starting a new Data Transfer Task.
source path: null, destintion path: ftp:/workdir/dir3/

total/finished/failed files number: 1500/1500/0, throughtout: 11.2 MB/s       

---------- statistic information ----------
total time: 2.53 s
total size: 87.6 MB (91887863 B)
transfer size: 87.6 MB (91887863 B)
failed size: 0 (0 B)
md5 file upload failed: 0
average copy rate: 34.6 MB/s
```
然后，使用ftp命令行登录到ftp server上，列举其中一个目录如下。
```
ftp> ls workdir/dir3/slx_thread_0/slx_dir_2
---> PASV
227 Entering Passive Mode (192,168,20,183,156,239).
---> LIST workdir/dir3/slx_thread_0/slx_dir_2
150 Here comes the directory listing.
-rw-r--r--    1 1000     1000       101728 Oct 13 22:59 slx_file_0
-rw-r--r--    1 1000     1000           32 Oct 13 22:59 slx_file_0.md5
-rw-r--r--    1 1000     1000        86176 Oct 13 22:59 slx_file_1
-rw-r--r--    1 1000     1000           32 Oct 13 22:59 slx_file_1.md5
-rw-r--r--    1 1000     1000        23415 Oct 13 22:59 slx_file_10
-rw-r--r--    1 1000     1000           32 Oct 13 22:59 slx_file_10.md5
......
-rw-r--r--    1 1000     1000        74401 Oct 13 22:59 slx_file_97
-rw-r--r--    1 1000     1000           32 Oct 13 22:59 slx_file_97.md5
-rw-r--r--    1 1000     1000        76806 Oct 13 22:59 slx_file_98
-rw-r--r--    1 1000     1000           32 Oct 13 22:59 slx_file_98.md5
-rw-r--r--    1 1000     1000        24472 Oct 13 22:59 slx_file_99
-rw-r--r--    1 1000     1000           32 Oct 13 22:59 slx_file_99.md5
226 Directory send OK.
ftp>
```
- 为了验证选项 -v 的效果，将某3个文件添加几个字符，再使用如下命令下载数据。
```
# ftp_benchmark -n 192.168.20.183 -u slx -p passwd -s ftp:/workdir/dir3 -d null -t 8 -v
```
输出结果如下：
```
Starting a new Data Transfer Task.
source path: ftp:/workdir/dir3, destintion path: null

list directory /workdir/dir3/slx_thread_4/slx_dir_2/                   
verify file '/workdir/dir3/slx_thread_1/slx_dir_2/slx_file_35' failed: upload md5: 92335448ba4ebaa7266ea08ad91689b9, download md5: 0484203490a1eef2501669098c4db1d6
total/finished/failed files number: 1500/720/0, throughput: 41.6 MB/s       
verify file '/workdir/dir3/slx_thread_3/slx_dir_0/slx_file_5' failed: upload md5: c9b1891a80771fe866fb6bec43973bef, download md5: 49dbfa3d63021ca74f634da285da37b7

verify file '/workdir/dir3/slx_thread_4/slx_dir_0/slx_file_78' failed: upload md5: f6a42d26e92791f22b808649d4f0ce1e, download md5: 8df410768097d4e6d114e99acb5d17d4
total/finished/failed files number: 1500/1500/0, throughtout: 46.0 MB/s       

---------- statistic information ----------
total time: 1.82 s
total size: 87.6 MB (91887889 B)
transfer size: 87.6 MB (91887889 B)
failed size: 0 (0 B)
verify failed: 3
average copy rate: 48.1 MB/s
```
可以看到检测到3个文件的MD5值与上传时的MD5值不一致，upload md5是上传数据的MD5值，download md5是下载数据的MD5值。最后的statistic information中verify failed为3。
