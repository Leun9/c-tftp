/* client args */
        
#define PUT_DATALEN             512
#define RECVBUFMAXLEN           516
#define SENDBUFMAXLEN           516

#define RECVTIMEOUT_DEFAULT     1000
#define SENDTIMEOUT_DEFAULT     1000

#define LOGFILE                 "tftp.log"

/* client error args */

// error type
#define ERRTYPE_CLIENT          3
#define ERRTYPE_TFTP            4
#define ERRTYPE_SOCK            5
#define ERRTYPE_FILE            6
#define ERRTYPE_NETASCII        7
#define ERRTYPE_UNEXPECTED      10

// error code
#define ERRCODE_SENDBUFLEN      301
#define ERRCODE_SETUPSESSION    302
#define ERRCODE_TIMEOUT         303
#define ERRCODE_TMPFILE         601


/* TFTP args */

#define TFTP_OPCODE_RRQ         1
#define TFTP_OPCODE_WRQ         2
