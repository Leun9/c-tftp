#include "client.h"

#include <stdio.h>
#include <string.h>
#include <io.h>
#include <winsock2.h>
#include <time.h>

#include "netascii.h"

// user arg
char *source;
char *target;
char *local_tmpfile;
char *remote_filename;
char trans_mode[9] = "octet";

// server arg
struct sockaddr_in server_addr;
struct sockaddr* server_addr_ptr = (struct sockaddr*)&server_addr;
int server_addr_len = sizeof(struct sockaddr_in);
struct sockaddr_in recv_addr; // used to recv
struct sockaddr* recv_addr_ptr = (struct sockaddr*)&recv_addr;
int recv_addr_len = sizeof(struct sockaddr_in);

// client arg
int client_sockfd;
char recvbuf[RECVBUFMAXLEN]; // send buf
char sendbuf[SENDBUFMAXLEN]; // recv buf
int recvbuf_len;
int sendbuf_len;
int recv_bytes = 0;
int send_bytes = 0;
int recv_time_out = RECVTIMEOUT_DEFAULT;
int send_time_out = SENDTIMEOUT_DEFAULT;

int err_type = 0;
int err_code = 0;
FILE* logfile;

void PrintError() {
    switch (err_type) {
    case ERRTYPE_CLIENT:
        // TODO
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
        case INVALID_CRorLF:
            printf("Invalid CR or LF.\n");
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
    return -1; \
}

#define SEND_BUF_TO_SERVER() \
    (send_bytes += buf_len, sendto(client_sockfd, sendbuf, sendbuf_len, 0, server_addr_ptr, sizeof(struct sockaddr)))

int SetupSession(short request) {
    // make request : request_code + filename + trans_mode
    *(short*)sendbuf = htons(request);
	sendbuf_len = 2;
    
    for (int i = 0; remote_filename[i] && sendbuf_len < SENDBUFMAXLEN; i++)
        sendbuf[sendbuf_len++] = remote_filename[i];
    sendbuf[sendbuf_len++] = 0;

    for (int i = 0; trans_mode[i] && sendbuf_len < SENDBUFMAXLEN; i++)
        sendbuf[sendbuf_len++] = trans_mode[i];
    sendbuf[sendbuf_len++] = 0;

    if (sendbuf_len == SENDBUFMAXLEN)
        SET_ERROR_AND_RETURN(ERRTYPE_CLIENT, ERRCODE_SENDBUFLEN);

    for (int trans_cnt = 0; ; ) {
        // send read request
        if (SOCKET_ERROR == SEND_BUF_TO_SERVE())
            SET_ERROR_AND_RETURN(ERRTYPE_SOCK, WSAGetLastError());

        // recv from ip
        int ret;
        do {
            ret = recvfrom(client_sockfd, recvbuf, RECVBUFMAXLEN, 0, recv_addr_ptr, &recv_addr_len);
        } while (ret > 0  && recv_addr.sin_addr.s_addr != server_addr.sin_addr.s_addr);

        // Time-out Retransmisson
        if (ret == SOCKET_ERROR && WSAGetLastError() == WSAETIMEDOUT) {
            trans_cnt++;
            if (trans_cnt == 3) return -1;
            continue;
        }

        // check packet
        // here : how to check?


        // update port
	    server_addr.sin_port = recv_addr.sin_port;
        break;
    }
    return 0;
}

int Get() {
    // write
    for (;;) {
        // send ack
        // recv from ip:port
        // Time-out Retransmisson
        // check packet
        // check order
        // write and update num
    }
    return 0;
}

int Put() {
    for (;;) {
        // send data
        // recv from ip:port
        // Time-out Retransmisson
        // check packet
        // check order
        // read and update num
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
    if (err_type) PrintError(); // print error
    return 0;
}