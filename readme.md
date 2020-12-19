# TFTP

C语言实现的TFTP客户端。

## 目录

* [项目介绍](#项目介绍)
* [使用方式](#使用方式)
* [实现思路](#实现思路)
  * [读（下载文件）](#读（下载文件）)
  * [写（上传文件）](#写（上传文件）)
  * [netascii](#netascii)
* [测试](#测试)
  * [启动服务器](#启动服务器)
  * [客户端测试](#客户端测试)

## 项目介绍

实现了[RFC1350](https://www.ietf.org/rfc/rfc1350.txt)描述的TFTP客户端：

- 提供对octet格式和netascii格式（[RFC854](https://www.ietf.org/rfc/rfc854.txt)）的读写支持（netascii.c）
- 超时重传机制（具体可见client.c的Timeout-Retransmission注释处）
- 客户端参数可调（client.h）
- 接收、发送、超时重传记录日志
- 详细的报错信息

## 使用方式

Windows平台下运行make.cmd编译，生成可执行文件tftp.exe。

```
make
```

命令行格式如下：

```cmd
tftp <-r|-w|-rn|-wn> <server_ip> <source> [target]
```

其中-r和-w表示读请求和写请求，-n表示传输模式为netascii；若不指定target，则target与source相同。

## 实现思路

#### 读（下载文件）

1. 发送读请求后，接收数据包并将数据按序写入缓存文件中。

2. 若读请求为netascii格式，传输完成后，检查文件格式是否正确。

3. 若传输成功，将缓存文件重命名为指定文件（可能覆盖同名文件）。
4. 若传输出错，删除缓存文件。

#### 写（上传文件）

对于octet，直接打开原始文件上传数据；对于netascii，工作流程如下：

1. 从原始文件读数据，转换成netascii模式，写入缓存文件中。若无法转换，则报错。
2. 发送缓存文件。
3. 传输结束后，删除缓存文件。

#### netascii

- 参考[RFC854](https://www.ietf.org/rfc/rfc854.txt)，netascii格式要求如下：
  - 合法的字符包括：
    - ASCII码在0x20到0x7f之间的可打印字符
    - ASCII码在0x07到0x0d之的七个控制字符
    - 控制字符NUL(0x0)
  - 换行符必须为CRLF。
  - CR字符后只能是NUL或者LF。

- 代码中定义的文件类型：
  - 合法文件：符合netascii格式要求
  - 可转换的文件：不符合netascii格式要求，但文件中字符均为合法字符
  - 不可转换的文件：文件中存在不合法字符
- 对于可转换的文件，其转换规则：
  - 对于单独的LF（其前一字符不为CR），在其前插入CR。
  - 对于单独的CR（其后一字符不为LF或NUL）在其后插入NUL。

## 启动服务器测试

### 启动服务器

首先在Linux环境下启动tftp服务器。

```bash
pip3 install tftpy
mkdir tftp_dir # 用作tftp服务器的文件目录
python3 tftp_server.py
```

其中tftp_server.py如下：

```python
import tftpy

server = tftpy.TftpServer('./tftp_dir')
server.listen('0.0.0.0', 69)
```

### 客户端测试

在windows环境下编译客户端并测试。

```cmd
make
set server_ip=[server_ip]
```

测试的指令及结果如下：

| request | mode     | file_format | command                         | result                                                       |
| ------- | -------- | ----------- | ------------------------------- | ------------------------------------------------------------ |
| write   | octet    | bin         | tftp -w %server_ip% sample_bin  | successed                                                    |
| read    | octet    | bin         | tftp -r %server_ip% sample_bin  | successed                                                    |
| write   | netascii | netascii    | tftp -wn %server_ip% sample_txt | successed                                                    |
| read    | netascii | netascii    | tftp -rn %server_ip% sample_txt | successed                                                    |
| write   | netascii | bin         | tftp -wn %server_ip% sample_bin | 客户端报错：文件格式无法转换为netascii。                                   |
| read    | netascii | bin         | tftp -rn %server_ip% sample_bin | 服务器以二进制格式发送文件，客户端接收并警告：接收了非netascii格式的文件。 |

