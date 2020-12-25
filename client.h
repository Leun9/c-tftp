/* client args */
        
#define BLOCKSIZE               512         // 发送的块大小
#define RECVBUFMAXLEN           516         // 接收缓冲区的长度
#define SENDBUFMAXLEN           516         // 发送缓冲区的长度（至少等于BLOCKSIZE + 4）

#define RECVTIMEOUT_DEFAULT     500         // 默认的RTO
#define RECVTIME_DEV_UBOUND     128         // 4倍RTO偏差的下届
#define SENDTIMEOUT_DEFAULT     1000        // 发送数据的timeout

#define FIRST_MAX_RETRANSCNT    10          // 第一次发送数据的重传次数
#define RETRANSCNT_DEFAULT      20          // 后续数据往返时的重传次数初始值

#define SPEED_REFRESH_FRE       1000

#define LOGFILE                 "tftp.log"  // 日志文件名

/* client error args */

// error type
#define ERRTYPE_CLIENT          3
#define ERRTYPE_TFTP            4
#define ERRTYPE_SOCK            5
#define ERRTYPE_NETASCII        7
#define ERRTYPE_UNEXPECTED      10

// error code
#define ERRCODE_TIMEOUT0        301
#define ERRCODE_SENDBUFLEN      302
#define ERRCODE_TIMEOUT         304
#define ERRCODE_TMPFILE         305


/* TFTP args */

#define TFTP_OPCODE_RRQ         1
#define TFTP_OPCODE_WRQ         2
#define TFTP_OPCODE_DATA        3
#define TFTP_OPCODE_ACK         4
#define TFTP_OPCODE_ERR         5
