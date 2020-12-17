# TFTP

C语言实现的TFTP客户端。

## 目录

* [项目介绍](#项目介绍)

* [使用方式](#使用方式)

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

| request | mode     | file format | command                         | result                                                       |
| ------- | -------- | ----------- | ------------------------------- | ------------------------------------------------------------ |
| write   | octet    | bin         | tftp -w %server_ip% sample_bin  | successed                                                    |
| read    | octet    | bin         | tftp -r %server_ip% sample_bin  | successed                                                    |
| write   | netascii | netascii    | tftp -wn %server_ip% sample_txt | successed                                                    |
| read    | netascii | netascii    | tftp -rn %server_ip% sample_txt | successed                                                    |
| write   | netascii | bin         | tftp -wn %server_ip% sample_bin | 客户端报错：文件格式错误。                                   |
| read    | netascii | bin         | tftp -rn %server_ip% sample_bin | 服务器以二进制格式发送文件，客户端接收并警告：接收了非netascii格式的文件。 |

