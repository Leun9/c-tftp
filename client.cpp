#include <iostream>
#include <cstdio>
#include <string>
#include <thread>

#include <winsock2.h>
#include <windows.h>

#include "client.h"

using namespace std;

const char *local_filename;
const char *remote_filename;
const char *trans_mode;
struct sockaddr_in server_addr;
struct sockaddr* server_addr_ptr = (struct sockaddr*)&server_addr;
int server_addr_len = sizeof(sockaddr_in);
char bbuf[BBUFMAXLEN]; // big buf
int bbuf_len;
char sbuf[SBUFMAXLEN]; // small buf
int sbuf_len;
int recv_time_out = RECVTIMEOUT_DEFAULT;
int send_time_out = SENDTIMEOUT_DEFAULT;
int client_sockfd;

// FIXME : 传输模式、参数检查、报错、重命名、详细信息

void Help() {
    cout << "Usage : tftp [action] [server_ip] [filename] [transmode]\n";
    exit(-1);
}

void ErrTftp() {
    cout << "err tftp\n"; exit(-1);
}

void ErrWinSock() {
    cout << "[ERROR] winsock error, error code : " << WSAGetLastError() << "\n";
    cout << "The text description of the error are listed under Windows Sockets Error Codes : ";
    cout << "https://docs.microsoft.com/en-us/windows/win32/winsock/windows-sockets-error-codes-2\n";
    exit(-1);
}

void ErrUnexpected() { cout << "err unexpected\n"; exit(-1);}

void ErrFile() {cout << "err file\n"; exit(-1);}

#define SEND_BUF_TO_SERVER(buf, buf_len) \
sendto(client_sockfd, buf, buf_len, 0, server_addr_ptr, sizeof(struct sockaddr))

int Get() { // bbuf used to send request and recv data, sbuf used to send ack 

    // send read request
    bbuf[0] = 0, bbuf[1] = 1; // RRQ_OPCode
	bbuf_len = 2;
	strcpy(bbuf + bbuf_len, remote_filename);
	bbuf_len += strlen(remote_filename) + 1;
	strcpy(bbuf + bbuf_len, trans_mode);
	bbuf_len += strlen(trans_mode) + 1;
    if (SOCKET_ERROR == SEND_BUF_TO_SERVER(bbuf, bbuf_len)) ErrWinSock();
    
    // open local file
    FILE *fp = fopen(local_filename, "wb");
    if (fp == NULL) ErrFile();

    // receive data
    sbuf[0] = 0, sbuf[1] = 4; // ACK_OPCode
    for (int expected_data_num = 1; ; ) {
        // recv data
        int ret = recvfrom(client_sockfd, bbuf, BBUFMAXLEN, 0, server_addr_ptr, &server_addr_len);
        int data_num = ntohs(*(short*)(bbuf + 2));

        // check packet
        if (ret == SOCKET_ERROR ) ErrWinSock(); // recvfrom error
        if (bbuf[0] == 0 && bbuf[1] == 5) ErrTftp(); // tftp error
        if (bbuf[0] != 0 && bbuf[1] != 3) ErrUnexpected(); // unexpected error

        // check order
        if (data_num == expected_data_num)  { // right order : write file && update sbuf
            expected_data_num++;
            fwrite(bbuf + 4, 1, ret - 4, fp);
            *(short*)(sbuf + 2) = htons(expected_data_num - 1); // ACK_num
        }

        // send ack
        if (SOCKET_ERROR == SEND_BUF_TO_SERVER(sbuf, 4)) ErrWinSock();
        if (data_num + 1 == expected_data_num && ret < 516) { // last packet    
            fclose(fp); // success
            break;
        }
    }
    cout << "success\n";
    return 0;
}

int Put() { // bbuf used to send request and send data, sbuf used to recv ack

    // open local file
    FILE *fp = fopen(local_filename, "rb");
    if (fp == NULL) ErrFile();

    // send write request
    bbuf[0] = 0, bbuf[1] = 2; // WRQ_OPCode
	bbuf_len = 2;
	strcpy(bbuf + bbuf_len, remote_filename);
	bbuf_len += strlen(remote_filename) + 1;
	strcpy(bbuf + bbuf_len, trans_mode);
	bbuf_len += strlen(trans_mode) + 1;
    if (SOCKET_ERROR == SEND_BUF_TO_SERVER(bbuf, bbuf_len)) ErrWinSock();

    // recv ack and send data
    bbuf[0] = 0, bbuf[1] = 3; // DATA_OPCode
    bbuf_len = 516; // for ack0
    for (int expected_ack_num = 0; ; ) {
        // recv ack
        int ret = recvfrom(client_sockfd, sbuf, SBUFMAXLEN, 0, server_addr_ptr, &server_addr_len);

        // check packet
        if (ret == SOCKET_ERROR) ErrWinSock(); // recvfrom error
        if (sbuf[0] == 0 && sbuf[1] == 5) ErrTftp(); // tftp error
        if (sbuf[0] != 0 || sbuf[1] != 4) ErrUnexpected(); // unexpected error
        int ack_num = ntohs(*(short*)(sbuf + 2));
        cout << "recv ack : " << ack_num << '\n';

        // check order
        if (ack_num == expected_ack_num) { // right order
            if (bbuf_len < 516) { // last packet
                fclose(fp); // success
                break;
            }
            expected_ack_num++;
            bbuf_len = fread(bbuf + 4, 1, PUT_DATALEN, fp) + 4;
            *(short*)(bbuf + 2) = htons(expected_ack_num); // data_num
        }

        // send data
        if (SOCKET_ERROR == SEND_BUF_TO_SERVER(bbuf, bbuf_len)) ErrWinSock();
    }
    cout << "success\n";
    return 0;
}

int main(int argc, char* argv[]) {
    // check args
    if (argc != 5) return -1; // TODO
    string action = string(argv[1]);
    string addr = string(argv[2]);
    string filename = string(argv[3]);
    trans_mode = argv[4];
    if (action != "get" && action != "put") return -1; // TODO
    //if (strcmp(trans_mode, "octet") ) return -1; // TODO

    // init
    local_filename = filename.c_str();
    remote_filename = filename.c_str();
    WSADATA wsa_data;
    WSAStartup(MAKEWORD(2,2), &wsa_data);

	// create client socket
	client_sockfd = socket(AF_INET, SOCK_DGRAM, 0);

    cout << "Action: " << action;
    cout << "\nAddr: " << addr;
    cout << "\nFilename: " << filename;
    cout << "\nTransmode: " << trans_mode << "\n\n"; 

	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = inet_addr(addr.c_str());
	server_addr.sin_port = htons(69);

    // FIXME : static time limit
    if (SOCKET_ERROR == setsockopt(client_sockfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&recv_time_out, sizeof(int))) ErrWinSock();
    if (SOCKET_ERROR == setsockopt(client_sockfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&send_time_out, sizeof(int))) ErrWinSock();

    if (action == "get") return Get();
    if (action == "put") return Put();
    return 0;
}