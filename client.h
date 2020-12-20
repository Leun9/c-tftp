/* client args */
        
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
#define ERRTYPE_NETASCII        7
#define ERRTYPE_UNEXPECTED      10

// error code
#define ERRCODE_TIMEOUT0        301
#define ERRCODE_SENDBUFLEN      302
#define ERRCODE_TIMEOUT         304
#define ERRCODE_TMPFILE         305


/* TFTP args */

#define TFTP_OPCODEN_RRQ        0x0100
#define TFTP_OPCODEN_WRQ        0x0200
#define TFTP_OPCODEN_DATA       0x0300
#define TFTP_OPCODEN_ACK        0x0400
#define TFTP_OPCODEN_ERR        0x0500
