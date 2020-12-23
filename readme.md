# TFTP

C语言实现的TFTP Windows客户端。

## 目录

* [项目介绍](#项目介绍)
* [使用方式](#使用方式)
* [实现思路](#实现思路)
  * [下载文件](#下载文件)
  * [上传文件](#上传文件)
  * [netascii](#netascii)
* [测试](#测试)

## 项目介绍

实现了[RFC1350](https://www.ietf.org/rfc/rfc1350.txt)描述的TFTP客户端：

- 提供对octet格式和netascii格式（[RFC854](https://www.ietf.org/rfc/rfc854.txt)）的读写支持（netascii.c）
- 动态的超时设置（模仿TCP协议，具体见client.c的Recv() 函数）
- 超时重传机制（具体见client.c的Timeout-Retransmission注释）
- 实时发送/接收速度显示

## 使用方式

Windows平台下运行make.cmd编译，生成可执行文件tftp.exe。

```
make
```

命令行格式如下：

```
Usage: tftp <-w|-r|-wn|-rn> server_ip source [target]
Options:
  -w    Upload binary file.
  -r    Download binary file.
  -wn   Upload netascii file.
  -rn   Download netascii file.
The target is the same as source if it is not assigned.
```

## 实现思路

#### 下载文件

1. 发送读请求后，接收数据包并将数据按序写入缓存文件中。

2. 若读请求为netascii格式，传输完成后，检查文件格式是否正确。

3. 若传输成功，将缓存文件重命名为指定文件（可能覆盖同名文件）。
4. 若传输出错，删除缓存文件。

#### 上传文件

对于octet，直接打开原始文件上传数据；对于netascii，工作流程如下：

1. 从原始文件读数据，转换成netascii模式，写入缓存文件中。若无法转换，则报错。
2. 发送缓存文件。
3. 传输结束后，删除缓存文件。

#### netascii

- 参考[RFC854](https://www.ietf.org/rfc/rfc854.txt)，netascii格式要求如下：
  - 合法的字符包括：
    - ASCII码在0x20到0x7e之间的可打印字符
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

## 测试

与正常的服务器通信，测试客户端，指令及结果如下：

| request | mode     | file_format | command                         | result                                             |
| ------- | -------- | ----------- | ------------------------------- | -------------------------------------------------- |
| write   | octet    | bin         | tftp -w %server_ip% sample_bin  | successed                                          |
| read    | octet    | bin         | tftp -r %server_ip% sample_bin  | successed                                          |
| write   | netascii | netascii    | tftp -wn %server_ip% sample_txt | successed                                          |
| read    | netascii | netascii    | tftp -rn %server_ip% sample_txt | successed                                          |
| write   | netascii | bin         | tftp -wn %server_ip% sample_bin | 客户端报错：文件格式无法转换为netascii。           |
| read    | netascii | bin         | tftp -rn %server_ip% sample_bin | 接收并检查是否为netascii格式。<br>不是则警告用户。 |

在极度恶劣的网络环境下测试客户端。

首先在服务器上（Linux环境）将 wlp3s0 网卡设置30%的丢包率，时延为100±30ms。

```bash
tc qdisc add dev wlp3s0 root netem loss 30% delay 100ms 30ms
```

客户端上的测试指令及详细输出如下。

```
>tftp -w %server_ip% client.c

Addr: xx.xx.xx.xx
Source: client.c
Target: client.c
Transmode: octet

Write succeed, total size: 15948, time: 62464 ms, speed: 255 bps.
Summary:
        Max data num: 32
        Retrans count: 27
                - Timeout retrans     : 7
                - Out of order retrans: 20
        Send bytes: 28528       speed: 457       bps
        Recv bytes: 212         speed: 3         bps
```

撤销服务器的更改。

```bash
tc qdisc del dev wlp3s0 root netem loss 30% delay 100ms 30ms
```

