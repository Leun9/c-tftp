/* user args */

#define BBUFMAXLEN              516         
#define PUT_DATALEN             512         
#define SBUFMAXLEN              4  

#define RECVTIMEOUT_DEFAULT     1000
#define SENDTIMEOUT_DEFAULT     1000

#define LOGFILE                 "tftp.log"

/* client error args */

// error type

#define ERRTYPE_ARG         3
#define ERRTYPE_TFTP        4
#define ERRTYPE_SOCK        5
#define ERRTYPE_FILE        6
#define ERRTYPE_NETASCII    7
#define ERRTYPE_UNEXPECTED  10

#define ERRCODE_BBUFLEN     301
#define ERRCODE_TMPFILE     601


/* TFTP args */

#define TFTP_OPCODE_RRQ     1
#define TFTP_OPCODE_WRQ     2
