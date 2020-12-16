#include <stdio.h>
#include <string.h>
#include <io.h>
#include <winsock2.h>
#include <windows.h>

#include "client.h"

const char *source;
const char *target;
const char *local_filename;
const char *remote_filename;
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

FILE* logfile;

// FIXME :
// 1 add Timeout-Retransmission(4)
// 2 吞吐量
// 3 日志信息
// 3 netascii : 当服务器仍然发送二进制文件时，由于FILE*打开模式为w，可能导致错误，解决方法参考RFC？

void Help() {
    printf("Usage: tftp <-r|-w|-rn|-wn> <server_ip> <source> [target]\n");
    exit(-1);
}

void ErrArg(char * info) {
    printf("[ERROR] arg error: %s.\n", info);
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
    printf("[ERROR] failed to open file, file: %s, trans mode: %s.\n", local_filename, trans_mode);
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

    // send read request and send request
    SendRequest(RRQ_OPCODE);
    fprintf(logfile, "[INFO] Sent read request.\n");
    FILE *fp = fopen(local_filename, "wb");
    if (fp == NULL) ErrFile();

    // receive data
    sbuf[0] = 0, sbuf[1] = 4; // ACK_OPCODE
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
    
    // open local file and send request
    FILE *fp = fopen(local_filename, "rb");
    if (fp == NULL) ErrFile();
    SendRequest(WRQ_OPCODE);
    fprintf(logfile, "[INFO] Sent write request.\n");

    // recv ack and send data
    bbuf[0] = 0, bbuf[1] = 3; // DATA_OPCODE
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
    // check args
    if (argc < 4 || argc > 5) Help();
    char *action = argv[1];
    char *addr = argv[2];
    source = argv[3];
    target = (argc == 5) ? argv[4] : source;
    if (!action[3] && action[2] == 'n') strcpy(trans_mode, "netascii"), action[2] = 0;
    if (strcmp(action, "-r") && strcmp(action, "-w")) Help();
    if (INADDR_NONE == inet_addr(addr)) ErrArg("invalid ip address");

    // init
    WSADATA wsa_data;
    WSAStartup(MAKEWORD(2,2), &wsa_data);

	// create client socket
	client_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    printf("\nAddr: %s\nSource: %s\nTarget: %s\nTransmode: %s\n\n", addr, source, target, trans_mode);

	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = inet_addr(addr);
	server_addr.sin_port = htons(69);

    // FIXME : time limit is static
    setsockopt(client_sockfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&recv_time_out, sizeof(int));
    setsockopt(client_sockfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&send_time_out, sizeof(int));

    logfile = fopen(LOGFILE, "w");
    if (strcmp(action, "-r") == 0) {
        remote_filename = source;
        local_filename = target;
        return Get();
    }
    if (strcmp(action, "-w") == 0) {
        remote_filename = target;
        local_filename = source;
        return Put();
    }
    return 0;
}