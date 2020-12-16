#include <stdio.h>
#include <string.h>
#include <winsock2.h>
#include <windows.h>

#include "client.h"

const char *source;
const char *target;
const char *local_filename;
const char *remote_filename;
const char *trans_mode;
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

FILE* logfile;

void Help() {
    printf("Usage: tftp <get|put> <server_ip> <source> <target> <transmode> [-v]\n");
    exit(-1);
}

void ErrArg(char * info) {
    printf("[ERROR] arg error: %s.", info);
    exit(-1);
}

void ErrTftp(short err_code) {
    printf("[ERROR] FTP error, error code: %hd.\n", err_code);
    exit(-1);
}

void ErrWinSock() {
    printf("[ERROR] winsock error, error code : %d\n", WSAGetLastError());
    printf("The text description of the error are listed under Windows Sockets Error Codes: \
        https://docs.microsoft.com/en-us/windows/win32/winsock/windows-sockets-error-codes-2\n");
    exit(-1);
}

void ErrUnexpected() {
    printf("[ERROR] unexpected error. \n");
    exit(-1);
}

void ErrFile() {
    printf("[ERROR] failed to open file.\n");
    exit(-1);
}

#define SEND_BUF_TO_SERVER(buf, buf_len) \
sendto(client_sockfd, buf, buf_len, 0, server_addr_ptr, sizeof(struct sockaddr))

void SendRequest(short OPCode) {
    *(short*)bbuf = htons(OPCode);
	bbuf_len = 2;
    for (int i = 0; remote_filename[i] && bbuf_len < BBUFMAXLEN; i++) bbuf[bbuf_len++] = remote_filename[i];
    bbuf[bbuf_len++] = 0;
    for (int i = 0; trans_mode[i] && bbuf_len < BBUFMAXLEN; i++) bbuf[bbuf_len++] = trans_mode[i];
    bbuf[bbuf_len++] = 0;
    if (bbuf_len == BBUFMAXLEN) ErrArg("buf size too short"); 
    if (SOCKET_ERROR == SEND_BUF_TO_SERVER(bbuf, bbuf_len)) ErrWinSock();
}

int Get() { // bbuf used to send request and recv data, sbuf used to send ack 
    // send read request
    SendRequest(RRQ_OPcode);
    fprintf(logfile, "[INFO] Sent read request.\n");
    
    // open local file
    FILE *fp = fopen(local_filename, "wb");
    if (fp == NULL) ErrFile();

    // receive data
    sbuf[0] = 0, sbuf[1] = 4; // ACK_OPCode
    for (int expected_data_num = 1; ; ) {
        // recv data
        int ret = recvfrom(client_sockfd, bbuf, BBUFMAXLEN, 0, server_addr_ptr, &server_addr_len);
        int data_num = ntohs(*(short*)(bbuf + 2));
        fprintf(logfile, "[INFO] Received block %d , size: %d.\n", data_num, ret);

        // check packet
        if (ret == SOCKET_ERROR ) ErrWinSock(); // recvfrom error
        if (bbuf[0] == 0 && bbuf[1] == 5) ErrTftp(ntohs(*(short*)(bbuf + 2))); // tftp error
        if (bbuf[0] != 0 && bbuf[1] != 3) ErrUnexpected(); // unexpected error

        // check order
        if (data_num == expected_data_num)  { // right order : write file && update sbuf
            expected_data_num++;
            fwrite(bbuf + 4, 1, ret - 4, fp);
            *(short*)(sbuf + 2) = htons(expected_data_num - 1); // ACK_num
        }

        // send ack
        if (SOCKET_ERROR == SEND_BUF_TO_SERVER(sbuf, 4)) ErrWinSock();
        fprintf(logfile, "[INFO] Sent ack %d.\n", expected_data_num - 1);
        if (data_num + 1 == expected_data_num && ret < 516) { // last packet
            printf("Read successed, total size: %d.\n", ftell(fp)); 
            fclose(fp); // success
            break;
        }
    }
    return 0;
}

int Put() { // bbuf used to send request and send data, sbuf used to recv ack
    // open local file
    FILE *fp = fopen(local_filename, "rb");
    if (fp == NULL) ErrFile();

    // send write request
    SendRequest(WRQ_OPcode);
    fprintf(logfile, "[INFO] Sent write request.\n");

    // recv ack and send data
    bbuf[0] = 0, bbuf[1] = 3; // DATA_OPCode
    bbuf_len = 516; // for ack0
    for (int expected_ack_num = 0; ; ) {
        // recv ack
        int ret = recvfrom(client_sockfd, sbuf, SBUFMAXLEN, 0, server_addr_ptr, &server_addr_len);
        int ack_num = ntohs(*(short*)(sbuf + 2));
        fprintf(logfile, "[INFO] Received ack %d.\n", ack_num, ret);

        // check packet
        if (ret == SOCKET_ERROR) ErrWinSock(); // recvfrom error
        if (sbuf[0] == 0 && sbuf[1] == 5) ErrTftp(ntohs(*(short*)(sbuf + 2))); // tftp error
        if (sbuf[0] != 0 || sbuf[1] != 4) ErrUnexpected(); // unexpected error

        // check order
        if (ack_num == expected_ack_num) { // right order
            if (bbuf_len < 516) { // last packet
                printf("Write successed, total size: %d.\n", ftell(fp)); 
                fclose(fp); // success
                break;
            }
            expected_ack_num++;
            bbuf_len = fread(bbuf + 4, 1, PUT_DATALEN, fp) + 4;
            *(short*)(bbuf + 2) = htons(expected_ack_num); // data_num
        }

        // send data
        if (SOCKET_ERROR == SEND_BUF_TO_SERVER(bbuf, bbuf_len)) ErrWinSock();
        fprintf(logfile, "[INFO] Sent block %d, size: %d.\n", bbuf_len);
    }
    return 0;
}

int main(int argc, char* argv[]) {
    if (strcmp(argv[1], "-h") == 0) Help();
    if (strcmp(argv[1], "--help") == 0) Help();

    // check args
    if (argc != 6) ErrArg("the number of arguments should be 6");
    const char *action = argv[1];
    const char *addr = argv[2];
    source = argv[3];
    target = argv[4];
    trans_mode = argv[5];
    if (strcmp(action, "get") && strcmp(action, "put")) ErrArg("action should be \"get\" or \"put\"");
    if (INADDR_NONE == inet_addr(addr)) ErrArg("invalid ip address");
    if (strcmp(trans_mode, "octet") && strcmp(trans_mode, "netascii")) ErrArg("transmode should be \"octet\" or \"netascii\"");

    // init
    WSADATA wsa_data;
    WSAStartup(MAKEWORD(2,2), &wsa_data);

	// create client socket
	client_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    printf("\nAction: %s\nAddr: %s\nSource: %s\nTarget: %s\nTransmode: %s\n\n", action, addr, source, target, trans_mode);

	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = inet_addr(addr);
	server_addr.sin_port = htons(69);

    // FIXME : time limit is static
    setsockopt(client_sockfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&recv_time_out, sizeof(int));
    setsockopt(client_sockfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&send_time_out, sizeof(int));

    logfile = fopen(LOGFILE, "w");
    if (strcmp(action, "get") == 0) {
        remote_filename = source;
        local_filename = target;
        return Get();
    }
    if (strcmp(action, "put") == 0) {
        remote_filename = target;
        local_filename = source;
        return Put();
    }
    return 0;
}