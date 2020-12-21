/* client args */
        
#define RECVBUFMAXLEN           516
#define SENDBUFMAXLEN           516

#define RECVTIMEOUT_DEFAULT     1000
#define SENDTIMEOUT_DEFAULT     1000
#define RETRANSCNT_DEFAULT      3

#define LOGFILE                 "tftp.log"

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
