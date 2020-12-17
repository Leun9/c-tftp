#include "client.h"

#include <stdio.h>
#include <string.h>
#include <io.h>
#include <winsock2.h>
#include <time.h>

#include "netascii.h"

char *source;
char *target;
char *local_tmpfile;
char *remote_filename;
char trans_mode[9] = "octet";
struct sockaddr_in server_addr;
struct sockaddr* server_addr_ptr = (struct sockaddr*)&server_addr;
int server_addr_len = sizeof(struct sockaddr_in);
char bbuf[BBUFMAXLEN]; // big buf
int bbuf_len;
char sbuf[SBUFMAXLEN]; // small buf
int sbuf_len;
int recv_time_out = RECVTIMEOUT_DEFAULT;
int send_time_out = SENDTIMEOUT_DEFAULT;
int client_sockfd;

int err_type = 0;
int err_code = 0;
FILE* logfile;

void PrintError() {
    switch (err_type){
    case 0:
        break;
    case ERRTYPE_ARG:
        printf("[ERROR] BBUFMAXLEN is too small, you can change it in \"client.h\".\n");
        break;
    case ERRTYPE_TFTP:
        printf("[ERROR] TFTP error. Error code: %d.\nThe text description of the error can be found at https://tools.ietf.org/html/rfc1350\n", err_code);
        break;
    case ERRTYPE_SOCK:
        printf("[ERROR] Sock error. Error code: %d.\nThe text description of the error can be found at https://docs.microsoft.com/en-us/windows/win32/winsock/windows-sockets-error-codes-2\n", err_code);
        break;
    case ERRTYPE_FILE:
        printf("[ERROR] Cannot open temp file: %s.\n", local_tmpfile);
        break;
    case ERRTYPE_NETASCII:
        printf("[ERROR] Netascii error. ");
        switch (err_code) {
        case INVALIDCR:
            printf("Invalid carriage return.\n");
            break;
        case INVALIDCHAR:
            printf("Invalid char in file.\n");
            break;
        case ERRFPCHECK:
        case ERRFPW:
            printf("Cannot open temp file: %s.\n", local_tmpfile);
            break;
        case ERRFPR:
            printf("Cannot open file: %s.\n", source);
            break;
        }
        break;
    default:
        printf("[ERROR] Unexpected error.\n");
        break;
    }
}

void Help() {
    printf("Usage: tftp <-r|-w|-rn|-wn> <server_ip> <source> [target]\n");
    exit(-1);
}

#define SET_ERROR_AND_RETURN(type, code) {\
    err_type = type; \
    err_code = code; \
    fclose(fp); \
    return -1; \
}

#define SEND_BUF_TO_SERVER(buf, buf_len) \
    (send_bytes += buf_len, sendto(client_sockfd, buf, buf_len, 0, server_addr_ptr, sizeof(struct sockaddr)))

#define SendRequest(OPCode) {\
    *(short*)bbuf = htons(OPCode); \
	bbuf_len = 2; \
    for (int i = 0; remote_filename[i] && bbuf_len < BBUFMAXLEN; i++) bbuf[bbuf_len++] = remote_filename[i]; \
    bbuf[bbuf_len++] = 0; \
    for (int i = 0; trans_mode[i] && bbuf_len < BBUFMAXLEN; i++) bbuf[bbuf_len++] = trans_mode[i]; \
    bbuf[bbuf_len++] = 0; \
    if (bbuf_len == BBUFMAXLEN) SET_ERROR_AND_RETURN(ERRTYPE_ARG, ERRCODE_BBUFLEN); \
    if (SOCKET_ERROR == SEND_BUF_TO_SERVER(bbuf, bbuf_len)) SET_ERROR_AND_RETURN(ERRTYPE_SOCK, WSAGetLastError()); \
}

double CalcSpeed(long long s, clock_t time) {
    return (double)s * (CLOCKS_PER_SEC) / time;
}

int Get() { // bbuf used to send request and recv data, sbuf used to send ack 
    clock_t clock_start = clock(); // clock
    long long send_bytes = 0, recv_bytes = 0; // flow
    int trans_cnt = 0;

    // open local file and send read request
    FILE *fp = fopen(local_tmpfile, "wb");
    if (fp == NULL) SET_ERROR_AND_RETURN(ERRTYPE_FILE, ERRCODE_TMPFILE);
GetSendRequest:
    SendRequest(TFTP_OPCODE_RRQ);
    fprintf(logfile, "[INFO] Sent read request.\n");

    // receive data
    sbuf[0] = 0, sbuf[1] = 4; // ACK_OPCODE
    for (int expected_data_num = 1; ; ) {
        // recv data
        int ret = recvfrom(client_sockfd, bbuf, BBUFMAXLEN, 0, server_addr_ptr, &server_addr_len);
        int data_num = ntohs(*(short*)(bbuf + 2));
        if (ret > 0) {
            recv_bytes += ret;
            fprintf(logfile, "[INFO] Received block %d , size: %d.\n", data_num, ret);
        }

        // check packet
        if (ret == SOCKET_ERROR ) { // recvfrom error
            int serr_code = WSAGetLastError();
            if (serr_code != WSAETIMEDOUT) SET_ERROR_AND_RETURN(ERRTYPE_SOCK, WSAGetLastError()); 
            if (serr_code == WSAETIMEDOUT) { // Timeout-Retransmission
                if (trans_cnt++ == 3) SET_ERROR_AND_RETURN(ERRTYPE_SOCK, WSAGetLastError());
                fprintf(logfile, "[WARN] time-out, retransmission: %d/3.\n", trans_cnt);
            }
            if (expected_data_num == 1) goto GetSendRequest;
            goto GetSend;
        }
        trans_cnt = 0;
        
        if (bbuf[0] == 0 && bbuf[1] == 5) // tftp error
            SET_ERROR_AND_RETURN(ERRTYPE_TFTP, ntohs(*(short*)(bbuf + 2)));
        if (bbuf[0] != 0 && bbuf[1] != 3) SET_ERROR_AND_RETURN(ERRTYPE_UNEXPECTED, 0); // unexpected error

        // check order
        if (data_num == expected_data_num)  { // right order : write file && update sbuf
            expected_data_num++;
            fwrite(bbuf + 4, 1, ret - 4, fp);
            *(short*)(sbuf + 2) = htons(expected_data_num - 1); // ACK_num
        }
GetSend:
        // send ack
        if (SOCKET_ERROR == SEND_BUF_TO_SERVER(sbuf, 4)) SET_ERROR_AND_RETURN(ERRTYPE_SOCK, WSAGetLastError());
        fprintf(logfile, "[INFO] Sent ack %d.\n", expected_data_num - 1);
        if (data_num + 1 == expected_data_num && ret < 516) { // last packet
            clock_t time = clock() - clock_start;
            printf("Read successed, total size: %d, time: %d ms.\n", ftell(fp), time); 
            printf("Send bytes: %d, speed: %.4lf bps.\n", send_bytes, CalcSpeed(send_bytes, time));
            printf("Recv bytes: %d, speed: %.4lf bps.\n", recv_bytes, CalcSpeed(recv_bytes, time));
            fclose(fp); // success
            break;
        }
    }
    fclose(fp);
    return 0;
}

int Put() { // bbuf used to send request and send data, sbuf used to recv ack
    clock_t clock_start = clock(); // clock
    long long send_bytes = 0, recv_bytes = 0; // flow
    int trans_cnt = 0;
    
    // open local file and send request
    FILE *fp = fopen(local_tmpfile, "rb");
    if (fp == NULL) SET_ERROR_AND_RETURN(ERRTYPE_FILE, ERRCODE_TMPFILE);
PutSendRequest:
    SendRequest(TFTP_OPCODE_WRQ);
    fprintf(logfile, "[INFO] Sent write request.\n");

    // recv ack and send data
    bbuf[0] = 0, bbuf[1] = 3; // DATA_OPCODE
    bbuf_len = 516; // for ack0
    for (int expected_ack_num = 0; ; ) {
        // recv ack
        int ret = recvfrom(client_sockfd, sbuf, SBUFMAXLEN, 0, server_addr_ptr, &server_addr_len);
        int ack_num = ntohs(*(short*)(sbuf + 2));
        if (ret > 0) {
            recv_bytes += ret;
            fprintf(logfile, "[INFO] Received ack %d.\n", ack_num, ret);
        }

        // check packet
        if (ret == SOCKET_ERROR) { // recvfrom error
            int serr_code = WSAGetLastError();
            if (serr_code != WSAETIMEDOUT) SET_ERROR_AND_RETURN(ERRTYPE_SOCK, WSAGetLastError());
            if (serr_code == WSAETIMEDOUT) { // Timeout-Retransmission
                if (trans_cnt++ == 3)  SET_ERROR_AND_RETURN(ERRTYPE_SOCK, WSAGetLastError());
                fprintf(logfile, "[WARN] time-out, retransmission: %d/3.\n", trans_cnt);
            } 
            if (expected_ack_num == 0) goto PutSendRequest;
            goto PutSend;
        }
        trans_cnt = 0;
        
        if (sbuf[0] == 0 && sbuf[1] == 5) // tftp error
            SET_ERROR_AND_RETURN(ERRTYPE_TFTP, ntohs(*(short*)(sbuf + 2)));
        if (sbuf[0] != 0 || sbuf[1] != 4) SET_ERROR_AND_RETURN(ERRTYPE_UNEXPECTED, 0); // unexpected error

        // check order
        if (ack_num == expected_ack_num) { // right order
            if (bbuf_len < 516) { // last packet
                clock_t time = clock() - clock_start;
                printf("Write successed, total size: %d, time: %d ms.\n", ftell(fp), time); 
                printf("Send bytes: %d, speed: %.2lf bps.\n", send_bytes, CalcSpeed(send_bytes, time));
                printf("Recv bytes: %d, speed: %.2lf bps.\n", recv_bytes, CalcSpeed(recv_bytes, time));
                fclose(fp); // success
                break;
            }
            expected_ack_num++;
            bbuf_len = fread(bbuf + 4, 1, PUT_DATALEN, fp) + 4;
            *(short*)(bbuf + 2) = htons(expected_ack_num); // data_num
        }
PutSend:
        // send data
        if (SOCKET_ERROR == SEND_BUF_TO_SERVER(bbuf, bbuf_len)) SET_ERROR_AND_RETURN(ERRTYPE_SOCK, WSAGetLastError());
        fprintf(logfile, "[INFO] Sent block %d, size: %d.\n", bbuf_len);
    }
    fclose(fp);
    return 0;
}

int main(int argc, char* argv[]) {
    // check args
    if (argc < 4 || argc > 5) Help();
    char *action = argv[1];
    char *addr = argv[2];
    source = argv[3];
    target = (argc == 5) ? argv[4] : source;
    if (!action[3] && action[2] == 'n') strcpy(trans_mode, "netascii"), action[2] = 0;
    if (strcmp(action, "-r") && strcmp(action, "-w")) Help();
    if (INADDR_NONE == inet_addr(addr)) {
        printf("[ERROR] Invalid ip address.\n");
        return 0;
    }
    printf("\nAddr: %s\nSource: %s\nTarget: %s\nTransmode: %s\n\n", addr, source, target, trans_mode);

    // init
    WSADATA wsa_data;
    WSAStartup(MAKEWORD(2,2), &wsa_data);
    logfile = fopen(LOGFILE, "w");

	// create client socket && server sockaddr
	client_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = inet_addr(addr);
	server_addr.sin_port = htons(69);

    // set time limit
    // FIXME : time limit is static
    setsockopt(client_sockfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&recv_time_out, sizeof(int));
    setsockopt(client_sockfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&send_time_out, sizeof(int));

    if (strcmp(action, "-r") == 0) {
        remote_filename = source;
        int target_len = strlen(target);
        local_tmpfile = malloc(target_len + 8);
        strcpy(local_tmpfile, target);
        strcpy(local_tmpfile + target_len, ".ftptmp");
        int ret = Get();
        if (ret) { // read failed : delete temp file and print error soon
            remove(local_tmpfile);
        } else { // read successed
            if (trans_mode[0] == 'n') { // netascii
                if (CheckNetascii(local_tmpfile)) printf("[Warning] Received non-netascii mode data.\n");
            }
            remove(target);
            rename(local_tmpfile, target);
        }
    } else if (strcmp(action, "-w") == 0) {
        remote_filename = target;
        if (trans_mode[0] == 'o') { // octet
            local_tmpfile = source;
            Put(); 
        } else { // netascii
            int source_len = strlen(source);
            local_tmpfile = malloc(source_len + 8);
            strcpy(local_tmpfile, source);
            strcpy(local_tmpfile + source_len, ".ftptmp");
            int ret = Txt2Netascii(source, local_tmpfile);
            if (ret) err_type = ERRTYPE_NETASCII, err_code = ret; else Put(); // print error soon
            remove(local_tmpfile);
        }
    }
    PrintError(); // print error
    return 0;
}