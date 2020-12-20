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
FILE *local_fp;
char *local_filename;
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
long long recv_bytes = 0;
long long send_bytes = 0;
int recv_time_out = RECVTIMEOUT_DEFAULT;
int send_time_out = SENDTIMEOUT_DEFAULT;
clock_t clk_sta, clk_end;

int err_type = 0;
int err_code = 0;
FILE* logfile;

void PrintError() {
    switch (err_type) {
    case ERRTYPE_CLIENT:
        printf("[ERROR] Client error: ");
        printf("%d", err_code);
        switch (err_code) {
        case ERRCODE_TIMEOUT0:
            printf("Session setup timeout.\n");
            break;
        case ERRCODE_SENDBUFLEN:
            printf("SENDBUFLEN is too small.\n");
            break;
        case ERRCODE_TIMEOUT:
            printf("Timeout.\n");
            break;
        case ERRCODE_TMPFILE:
            printf("Cannot open file: %s.\n", local_filename);
            break;
        }
        break;
    case ERRTYPE_TFTP:
        printf("[ERROR] TFTP error. Error code: %d.\nThe text description of the error can be found at https://tools.ietf.org/html/rfc1350\n", err_code);
        break;
    case ERRTYPE_SOCK:
        printf("[ERROR] Sock error. Error code: %d.\nThe text description of the error can be found at https://docs.microsoft.com/en-us/windows/win32/winsock/windows-sockets-error-codes-2\n", err_code);
        break;
    case ERRTYPE_NETASCII:
        printf("[ERROR] Netascii error: ");
        switch (err_code) {
        case INVALIDCHAR:
            printf("Unable to convert to netascii format, because invalid char is in file.\n");
            break;
        case ERRFPCHECK:
        case ERRFPW:
            printf("Cannot open file to write: %s.\n", local_filename);
            break;
        case ERRFPR:
            printf("Cannot open file to read: %s.\n", source);
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

#define SET_ERROR_AND_RETURN(type, code) { \
    err_type = type; \
    err_code = code; \
    return -1; \
}

int Send() {
    int ret = sendto(client_sockfd, sendbuf, sendbuf_len, 0, server_addr_ptr, sizeof(struct sockaddr));
    if (ret > 0) {
        send_bytes += ret;
        fprintf(logfile, "[INFO] Sent data, head: 0x%08lx, size: %d.\n", *(long*)sendbuf, ret);
    }
    return ret;
}

int Recv() {
    recvbuf_len = recvfrom(client_sockfd, recvbuf, RECVBUFMAXLEN, 0, recv_addr_ptr, &recv_addr_len);
    if (recvbuf_len > 0) {
        recv_bytes += recvbuf_len;
        fprintf(logfile, "[INFO] Received data, head: 0x%08lx, size: %d.\n", *(long*)recvbuf, recvbuf_len);
    }
    return recvbuf_len;
}

int SetupSession(short request, long respond) {
    fprintf(logfile, "[INFO] Start setuping Session.\n");
    clk_sta = clock();
    // request packet : request_code + filename + trans_mode
    *(short*)sendbuf = request;
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
        if (SOCKET_ERROR == Send())
            SET_ERROR_AND_RETURN(ERRTYPE_SOCK, WSAGetLastError());
        // recv from ip
        int ret;
        do {
            ret = Recv();
        } while (ret > 0  && recv_addr.sin_addr.s_addr != server_addr.sin_addr.s_addr);
        // Time-out Retransmisson
        if (ret == SOCKET_ERROR) {
            if (WSAGetLastError() == WSAETIMEDOUT) {
                trans_cnt++;
                if (trans_cnt == 3) SET_ERROR_AND_RETURN(ERRTYPE_CLIENT, ERRCODE_TIMEOUT0);
                fprintf(logfile, "[WARN] Time-out, retransmission: %d/3.\n", trans_cnt);
                continue;
            } else {
                SET_ERROR_AND_RETURN(ERRTYPE_SOCK, WSAGetLastError());
            }
        }
        // check packet
        if (*(short*)recvbuf == TFTP_OPCODEN_ERR) 
            SET_ERROR_AND_RETURN(ERRTYPE_TFTP, ntohs(*(short*)(recvbuf + 2)));
        // update port
        if (*(long*)recvbuf == respond) {
            server_addr.sin_port = recv_addr.sin_port;
            break;
        }
        SET_ERROR_AND_RETURN(ERRTYPE_UNEXPECTED, 0);
    }
    return 0;
    fprintf(logfile, "[INFO] Session successfully setuped, start transmiting. \n");
}

int RoundTrip(short respond) {
    for (int trans_cnt = 0; ; ) {
        // send packet
        if (SOCKET_ERROR == Send())
            SET_ERROR_AND_RETURN(ERRTYPE_SOCK, WSAGetLastError());
        // recv from ip:port
        int ret;
        do {
            ret = Recv();
        } while (ret > 0  && (recv_addr.sin_addr.s_addr != server_addr.sin_addr.s_addr || 
                                recv_addr.sin_port != server_addr.sin_port));
        // Time-out Retransmisson
        if (ret == SOCKET_ERROR) {
            if (WSAGetLastError() == WSAETIMEDOUT) {
                trans_cnt++;
                if (trans_cnt == 3) SET_ERROR_AND_RETURN(ERRTYPE_CLIENT, ERRCODE_TIMEOUT);
                fprintf(logfile, "[WARN] Time out, retransmission: %d/3.\n", trans_cnt);
                continue;
            } else {
                SET_ERROR_AND_RETURN(ERRTYPE_SOCK, WSAGetLastError());
            }
        }
        // check packet
        if (*(short*)recvbuf == TFTP_OPCODEN_ERR) 
            SET_ERROR_AND_RETURN(ERRTYPE_TFTP, ntohs(*(short*)(recvbuf + 2)));
        if (*(short*)recvbuf == respond) break;
    }
    return 0;
}

double CalcSpeed(long long s, long time) {
    return (double)s * (CLOCKS_PER_SEC) / time;
}

int Get() {
    if (SetupSession(TFTP_OPCODEN_RRQ, 0x01000300L)) // data_opcode(00 03) data_num(00 01)
        return -1;
    // write and update num
    fwrite(recvbuf + 4, 1, recvbuf_len - 4, local_fp);
    *(long*)sendbuf = 0x01000400L; // ack_opcode(00 04) ack_num(00 01)
    sendbuf_len = 4;
    for (short ackn = 1; recvbuf_len >= 516; ) {
        if (RoundTrip(TFTP_OPCODEN_DATA)) return -1;
        // check order
        short datan = ntohs(*(short*)(recvbuf + 2));
        if (datan != ackn + 1) continue;
        // write and update num
        fwrite(recvbuf + 4, 1, recvbuf_len - 4, local_fp);
        ackn++;
        *(short*)(sendbuf + 2) = htons(ackn);
    }
    // send ack
    Send();
    long time = clock() - clk_sta;
    printf("Read successed, total size: %ld, time: %ld ms.\n", ftell(local_fp), time); 
    printf("Sent bytes: %lld, speed: %.4lf bps.\n", send_bytes, CalcSpeed(send_bytes, time));
    printf("Recv bytes: %lld, speed: %.4lf bps.\n", recv_bytes, CalcSpeed(recv_bytes, time));
    fprintf(logfile, "[INFO] File downloaded successfully, size: %ld.\n", ftell(local_fp));
    return 0;
}

int Put() {
    if (SetupSession(TFTP_OPCODEN_WRQ, 0x00000400L)) // ack_opcode(00 04) ack_num(00 00)
        return -1;
    // read and update num
    sendbuf_len = fread(sendbuf + 4, 1, 512, local_fp) + 4;
    *(long*)sendbuf = 0x01000300L; // data_opcode(00 03) data_num(00 01)
    for (short datan = 1;;) {
        if (RoundTrip(TFTP_OPCODEN_ACK)) return -1;
        // check order
        short ackn = ntohs(*(short*)(recvbuf + 2));
        if (ackn != datan) continue;
        // read and update num
        sendbuf_len = fread(sendbuf + 4, 1, 512, local_fp) + 4;
        datan++;
        *(short*)(sendbuf + 2) = htons(datan);
        if (sendbuf_len == 4) break;  // recv last ack
    }
    long time = clock() - clk_sta;
    printf("Write successed, total size: %ld, time: %ld ms.\n", ftell(local_fp), time); 
    printf("Sent bytes: %lld, speed: %.4lf bps.\n", send_bytes, CalcSpeed(send_bytes, time));
    printf("Recv bytes: %lld, speed: %.4lf bps.\n", recv_bytes, CalcSpeed(recv_bytes, time));
    fprintf(logfile, "[INFO] File uploaded successfully, size: %d.\n", ftell(local_fp));
    return 0;
}

#define OPEN_LOCAL_FILE(mode) { \
    local_fp = fopen(local_filename, mode); \
    if (local_fp == NULL) SET_ERROR_AND_GOTOPRT(ERRTYPE_CLIENT, ERRCODE_TMPFILE); \
}

#define SET_ERROR_AND_GOTOPRT(type, code) { \
    err_type = type; \
    err_code = code; \
    goto PrtErr; \
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
        local_filename = malloc(target_len + 8);
        strcpy(local_filename, target);
        strcpy(local_filename + target_len, ".ftptmp");
        OPEN_LOCAL_FILE("wb");
        int ret = Get();
        fclose(local_fp);
        if (ret) { // read failed : delete temp file and print error soon
            remove(local_filename);
        } else { // read successed
            if (trans_mode[0] == 'n') { // netascii
                if (CheckNetascii(local_filename)) printf("[Warning] Received non-netascii mode data.\n");
            }
            remove(target);
            rename(local_filename, target);
        }
    } else if (strcmp(action, "-w") == 0) {
        remote_filename = target;
        if (trans_mode[0] == 'o') { // octet
            local_filename = source;
            OPEN_LOCAL_FILE("rb");
            Put(); 
            fclose(local_fp);
        } else { // netascii
            int source_len = strlen(source);
            local_filename = malloc(source_len + 8);
            strcpy(local_filename, source);
            strcpy(local_filename + source_len, ".ftptmp");
            int ret = Txt2Netascii(source, local_filename);
            if (ret) SET_ERROR_AND_GOTOPRT(ERRTYPE_NETASCII, ret);
            OPEN_LOCAL_FILE("rb");
            Put(); 
            fclose(local_fp);
            remove(local_filename);
        }
    }
PrtErr:
    if (err_type) PrintError(); // print error
    return 0;
}