#include "client.h"

#include <stdio.h>
#include <string.h>
#include <io.h>
#include <winsock2.h>
#include <time.h>

#include "netascii.h"

// user arg
char *source;       /* 源文件名, 下载操作时指服务器上的文件，上传操作时指本地文件 */
char *target;       /* 目标文件名 */
FILE *local_fp;     /* 指向本地缓存文件的文件指针 */
char *local_filename;       /* 缓存文件名，其后缀为.ftptmp */
char *remote_filename;      /* 服务器上文件的名称 */
char trans_mode[9] = "octet";   /* 传输模式，用于构造请求包 */

// server arg
struct sockaddr_in server_addr;     /* 服务器的通信地址，其端口号初始值为69，
                                       第一次往返后变更为另一个固定的端口号 */
struct sockaddr* server_addr_ptr = (struct sockaddr*)&server_addr;
int server_addr_len = sizeof(struct sockaddr_in);
struct sockaddr_in recv_addr;       /* 存放recvfrom接收到的数据包的发送方 */
struct sockaddr* recv_addr_ptr = (struct sockaddr*)&recv_addr;
int recv_addr_len = sizeof(struct sockaddr_in);

// client arg
int client_sockfd;              /* 客户端的套接字 */
char recvbuf[RECVBUFMAXLEN];    /* 用于接收数据的缓冲区 */
int recvbuf_len;
char sendbuf[SENDBUFMAXLEN];    /* 用于发送数据的缓冲区 */
int sendbuf_len;

long recv_bytes = 0;        /* 接收的总字节数 */
long send_bytes = 0;        /* 发送的总字节数 */
int recv_time_out = RECVTIMEOUT_DEFAULT;    /* recvfrom的超时设置，动态变化 */
int send_time_out = SENDTIMEOUT_DEFAULT;    /* sendto的超时设置，不发生变化 */
int max_timeout_retrans_cnt = RETRANSCNT_DEFAULT;   /* 最大重传次数，动态变化 */
int to_retrans_cnt = 0;                 /* 超时（timeout）重传的次数 */
int ooo_retrans_cnt = 0;                /* 因失序(out of order)导致重传的次数 */
int unexpected_retrans_cnt = 0;     /* 因接收到意料之外的数据包导致重传的次数 */

/* 平滑接收时间，实际存储估计值的8倍，与TCP中的srtt相似 */
int smooth_recv_time = RECVTIMEOUT_DEFAULT << 3;
/* 接收时间的偏差，实际存储偏差值的4倍，与TCP中的mdev相似 */
int recv_time_dev = RECVTIMEOUT_DEFAULT << 1;

clock_t clk_sta; /* 会话开始的时间，用于计算最终的速度 */

long rt_recv_bytes = 0; /* 距离上一次刷新实时速度接收的字节总数 */
long rt_send_bytes = 0; /* 距离上一次刷新实时速度发送的字节总数 */
clock_t clk_prt_speed;  /* 上一次刷新实时速度的时间 */

int err_type = 0;   /* 错误类型，参考client.h */
int err_code = 0;   /* 错误代码，参考client.h */
FILE* logfile;      /* 日志文件 */

/* 根据err_type和err_code打印错误信息 */
void PrintError() {
    switch (err_type) {
    case ERRTYPE_CLIENT:
        printf("[ERROR] Client error: ");
        switch (err_code) {
        case ERRCODE_TIMEOUT0:
            printf("Session setup failed: Timeout. \n");
            break;
        case ERRCODE_SENDBUFLEN:
            printf("SENDBUFLEN is too small.\n");
            break;
        case ERRCODE_TIMEOUT:
            printf("Transmission interrupted: Timeout.\n");
            break;
        case ERRCODE_TMPFILE:
            printf("Cannot open file: %s.\n", local_filename);
            break;
        }
        break;
    case ERRTYPE_TFTP:
        printf("[ERROR] TFTP error. Error code: %d. The text description of the error can be found at https://tools.ietf.org/html/rfc1350\n", err_code);
        break;
    case ERRTYPE_SOCK:
        printf("[ERROR] Sock error. Error code: %d. The text description of the error can be found at https://docs.microsoft.com/en-us/windows/win32/winsock/windows-sockets-error-codes-2\n", err_code);
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
        printf("[ERROR] An unexpected error has occurred. errtype: %d, errcode: %d.\n", err_type, err_code);
        break;
    }
}

/* 打印命令行的用法 */
void Help() {
    printf("\nUsage: tftp [option] server_ip source [target]\n"
            "Options:\n"
            "  -w\tUpload binary file.\n"
            "  -r\tDownload binary file.\n"
            "  -wn\tUpload netascii file.\n"
            "  -rn\tDownload netascii file.\n"
            "The target is the same as source if it is not assigned.\n\n");
    exit(-1);
}

#define SET_ERROR_AND_RETURN(type, code) { \
    err_type = type; \
    err_code = code; \
    return -1; \
}

double CalcSpeed(long s, long time) {
    return (double)s * (CLOCKS_PER_SEC) / time;
}

/* 实时显示速度 */
void PrtSpeed() {
    clock_t now = clock();
    long time = now - clk_prt_speed;
    if (time < SPEED_REFRESH_FRE) return; /* 距离上一次刷新小于刷新间隔，则退出 */

    // calc real-time speed and print
    printf("send speed: %-9.0lf bps  Recv speed: %-9.0lf bps.\r",
            CalcSpeed(rt_send_bytes, time), CalcSpeed(rt_recv_bytes, time));

    // update
    clk_prt_speed = now;
    rt_recv_bytes = 0;
    rt_send_bytes = 0;
}

/* 将sendbuf中长度为sendbuf_len的数据包发送给server_addr */
int Send() {
    int ret = sendto(client_sockfd, sendbuf, sendbuf_len, 0, server_addr_ptr, sizeof(struct sockaddr));
    if (ret > 0) {      /* 成功发送则更新相应变量 */
        send_bytes += ret;
        rt_send_bytes += ret;
        fprintf(logfile, "[INFO] Sent data, head: 0x%08lx, size: %d.\n", ntohl(*(long*)sendbuf), ret);
    }
    PrtSpeed(); /* 刷新实时速度显示 */
    return ret;
}

/* 接收数据包，将数据存放在recvbuf中，其长度存放在recvbuf_len中，发送方的信息存放在recv_addr中 */
int Recv() {
    clock_t sta = clock();
    recvbuf_len = recvfrom(client_sockfd, recvbuf, RECVBUFMAXLEN, 0, recv_addr_ptr, &recv_addr_len);

    if (recvbuf_len > 0) {
        /* 计算平滑接收时间与接收时间偏差， 计算方法与TCP的srtt和mdev相同 */
        // update smooth_recv_time and recv_time_out
        //     imitate the update rules of RTO(Retransmission Timeout) in TCP protocol (linux/net/rxrpc/rtt.c)
        int m = clock() - sta;
        m -= (smooth_recv_time >> 3);
        smooth_recv_time += m;      /* srt = 7/8 srt + 1/8 new */
        if (m < 0) {
            m = -m;
            m -= (recv_time_dev >> 2);
            /* 1. 防止sto降，rto反而升高； 2. 防止rto降低得太快 */
            if (m > 0) m >>= 3;     /* prevents growth of rto, limits too fast rto decreases */
        } else {
            m -= (recv_time_dev >> 2);
        }
        recv_time_dev += m;         /* dev = 3/4 dev + 1/4 new */

        /* 更新接收等待的时间：rto = srt + 4*dev */
        // update the timeout for blocking receive calls
        recv_time_out = (smooth_recv_time >> 3) + recv_time_dev;
        setsockopt(client_sockfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&recv_time_out, sizeof(int));

        recv_bytes += recvbuf_len;
        rt_recv_bytes += recvbuf_len;
        fprintf(logfile, "[INFO] Received data, head: 0x%08lx, size: %d.\n", ntohl(*(long*)recvbuf), recvbuf_len);
    }
    PrtSpeed();     /* 刷新实时速度显示 */
    return recvbuf_len;
}

/* 建立会话，即完成第一次数据往返，request为客户端的操作码，respond0、respond1为期待服务器返回的前四个字节 */
int SetupSession(u_short request, u_short respond0, u_short respond1) {
    fprintf(logfile, "[INFO] Start setuping Session.\n");
    clk_sta = clock();

    /* 构造请求包：request packet : request_code + filename + trans_mode */
    // request packet : request_code + filename + trans_mode
    *(u_short*)sendbuf = request;
	sendbuf_len = 2;
    for (int i = 0; remote_filename[i] && sendbuf_len < SENDBUFMAXLEN; i++)
        sendbuf[sendbuf_len++] = remote_filename[i];
    sendbuf[sendbuf_len++] = 0;
    for (int i = 0; trans_mode[i] && sendbuf_len < SENDBUFMAXLEN; i++)
        sendbuf[sendbuf_len++] = trans_mode[i];
    sendbuf[sendbuf_len++] = 0;

    /* 缓冲区不够存放request_code + filename + trans_mode则报错 */
    if (sendbuf_len == SENDBUFMAXLEN) 
        SET_ERROR_AND_RETURN(ERRTYPE_CLIENT, ERRCODE_SENDBUFLEN);

    for (int timeout_retrans_cnt = 0; ; ) {
        // send read request
        if (SOCKET_ERROR == Send())
            SET_ERROR_AND_RETURN(ERRTYPE_SOCK, WSAGetLastError());

        /* 接收一个来自服务器的数据包 */
        // recv from ip
        int ret;
        do {
            ret = Recv();
        } while (ret > 0  && recv_addr.sin_addr.s_addr != server_addr.sin_addr.s_addr);

        // Time-out Retransmisson
        if (ret == SOCKET_ERROR) {
            if (WSAGetLastError() == WSAETIMEDOUT) { /* 触发超时重传 */
                timeout_retrans_cnt++;
                to_retrans_cnt++;
                if (timeout_retrans_cnt == max_timeout_retrans_cnt) SET_ERROR_AND_RETURN(ERRTYPE_CLIENT, ERRCODE_TIMEOUT0);
                fprintf(logfile, "[WARN] Time-out, retransmission times: %d.\n", timeout_retrans_cnt);
                continue;
            } else {
                SET_ERROR_AND_RETURN(ERRTYPE_SOCK, WSAGetLastError());
            }
        }

        /* 若成功接收数据包，则更新最大重传次数: limit = max(limit, 当前重传次数*2) */
        // update max_timeout_retrans_cnt
        if (max_timeout_retrans_cnt < timeout_retrans_cnt * 2)
            max_timeout_retrans_cnt = timeout_retrans_cnt * 2;
        
        /* 检查是否为ERROR包 */
        // check packet
        if (ntohs(*(u_short*)recvbuf) == TFTP_OPCODE_ERR) 
            SET_ERROR_AND_RETURN(ERRTYPE_TFTP, ntohs(*(u_short*)(recvbuf + 2)));

        /* 接收到期待的数据包:将会话端口写入server_addr,并退出 */
        // update port
        if (*(u_short*)recvbuf == respond0 && *(u_short*)(recvbuf+2) == respond1) {
            server_addr.sin_port = recv_addr.sin_port;
            break;
        }

        /* 接收到意料之外的包（既不是错误包，也不是期待的应答包），则重传 */
        timeout_retrans_cnt++;
        unexpected_retrans_cnt++;
    }
    return 0;
    fprintf(logfile, "[INFO] Session setuped successfully, start transmiting. \n");
}

/* 建立会话后，与server_addr指定的ip:port完成一次数据往返，respond为期待的应答包类型 */
int RoundTrip(u_short respond) {
    for (int timeout_retrans_cnt = 0; ; ) {
        // send packet
        if (SOCKET_ERROR == Send())
            SET_ERROR_AND_RETURN(ERRTYPE_SOCK, WSAGetLastError());

        /* 接收一个来自服务器会话进程的数据包 */
        // recv from ip:port
        int ret;
        do {
            ret = Recv();
        } while (ret > 0  && (recv_addr.sin_addr.s_addr != server_addr.sin_addr.s_addr || 
                                recv_addr.sin_port != server_addr.sin_port));

        // Time-out Retransmisson
        if (ret == SOCKET_ERROR) {
            if (WSAGetLastError() == WSAETIMEDOUT) { /* 触发超时重传 */
                timeout_retrans_cnt++;
                to_retrans_cnt++;
                if (timeout_retrans_cnt == max_timeout_retrans_cnt) SET_ERROR_AND_RETURN(ERRTYPE_CLIENT, ERRCODE_TIMEOUT);
                fprintf(logfile, "[WARN] Time out, retransmission times: %d.\n", timeout_retrans_cnt);
                continue;
            } else {
                SET_ERROR_AND_RETURN(ERRTYPE_SOCK, WSAGetLastError());
            }
        }

        /* 若成功接收数据包，则更新最大重传次数: limit = max(limit, 当前重传次数*2) */
        // update max_timeout_retrans_cnt
        if (max_timeout_retrans_cnt < timeout_retrans_cnt * 2)
            max_timeout_retrans_cnt = timeout_retrans_cnt * 2;

        /* 检查是否为ERROR包 */
        // check packet
        if (ntohs(*(u_short*)recvbuf) == TFTP_OPCODE_ERR) 
            SET_ERROR_AND_RETURN(ERRTYPE_TFTP, ntohs(*(u_short*)(recvbuf + 2)));

        /* 接收到期待的数据包:退出循环 */
        if (*(u_short*)recvbuf == respond) break;
        
        /* 接收到意料之外的包（既不是错误包，也不是期待的应答包），则重传 */
        timeout_retrans_cnt++;
        unexpected_retrans_cnt++;
    }
    return 0;
}

/* 下载文件，写入local_fp中 */
int Get() {
    /* 建立会话 */
    if (SetupSession(htons(TFTP_OPCODE_RRQ), htons(TFTP_OPCODE_DATA), htons(1))) // data_opcode(00 03) data_num(00 01)
        return -1;

    /* 写入第一个数据包的数据 */
    // write and update num
    fwrite(recvbuf + 4, 1, recvbuf_len - 4, local_fp);

    /* 构造应答数据包 */
    *(u_short*)sendbuf = htons(TFTP_OPCODE_ACK); // ack_opcode(00 04)
    *(u_short*)(sendbuf+2) = htons(1); // ack_num(00 01)
    sendbuf_len = 4;
    u_short ackn = 1;

    for (; recvbuf_len >= 516; ) {
        /* 完成一次数据往返 */
        if (RoundTrip(htons(TFTP_OPCODE_DATA))) return -1;
        
        /* 检查数据包是否失序，失序则重传 */    // check order
        u_short datan = ntohs(*(u_short*)(recvbuf + 2));
        if (datan != ackn + 1) {
            fprintf(logfile, "[WARN] Out of order.\n");
            ooo_retrans_cnt++;
            continue;
        }

        /* 数据包顺序正确，则写入文件并更新ack_num与待发送数据 */      // write and update num
        fwrite(recvbuf + 4, 1, recvbuf_len - 4, local_fp);
        ackn++;
        *(u_short*)(sendbuf + 2) = htons(ackn);
    }
    Send(); /* 发送最后一个ack */

    /* 输出传输概要 */
    long time = clock() - clk_sta;
    printf( "Read succeed, total size: %ld, time: %ld ms, speed: %.0lf bps.\nSummary:\n"
            "\tMax data num: %hd\n"
            "\tRetrans count: %d\n\t\t- Timeout retrans     : %d\n\t\t- Out of order retrans: %d\n"
            "\tSend bytes: %-10lld  speed: %-9.0lf bps\n"
            "\tRecv bytes: %-10lld  speed: %-9.0lf bps\n"
            , ftell(local_fp), time, CalcSpeed(ftell(local_fp), time)
            , ackn, to_retrans_cnt + ooo_retrans_cnt, to_retrans_cnt, ooo_retrans_cnt
            , send_bytes, CalcSpeed(send_bytes, time)
            , recv_bytes, CalcSpeed(recv_bytes, time));
    fprintf(logfile, "[INFO] File downloaded successfully, size: %ld.\n", ftell(local_fp));
    return 0;
}

/* 上传文件，从local_fp中取数据 */
int Put() {
    /* 建立会话 */
    if (SetupSession(htons(TFTP_OPCODE_WRQ), htons(TFTP_OPCODE_ACK), htons(0))) // ack_opcode(00 04) ack_num(00 00)
        return -1;

    /* 构造应答数据包 */
    // read and update num
    sendbuf_len = fread(sendbuf + 4, 1, 512, local_fp) + 4;
    *(u_short*)sendbuf =  htons(TFTP_OPCODE_DATA); // data_opcode(00 03) 
    *(u_short*)(sendbuf + 2) = htons(1); // data_num(00 01)
    u_short datan = 1;
    for (;;) {
        /* 完成一次数据往返 */
        if (RoundTrip(htons(TFTP_OPCODE_ACK))) return -1;
        
        /* 检查ack是否失序，失序则重传 */    // check order
        u_short ackn = ntohs(*(u_short*)(recvbuf + 2));
        if (ackn != datan)  {
            fprintf(logfile, "[WARN] Out of order.\n");
            ooo_retrans_cnt++;
            continue;
        }
        
        /* 顺序正确，则更新data_num和待发送数据 */ // read and update num
        sendbuf_len = fread(sendbuf + 4, 1, 512, local_fp) + 4;
        datan++;
        *(u_short*)(sendbuf + 2) = htons(datan);
        if (sendbuf_len == 4) break;  // recv last ack
    }
    
    /* 输出传输概要 */
    long time = clock() - clk_sta;
    printf( "Write succeed, total size: %ld, time: %ld ms, speed: %.0lf bps.\nSummary:\n"
            "\tMax data num: %hd\n"
            "\tRetrans count: %d\n\t\t- Timeout retrans     : %d\n\t\t- Out of order retrans: %d\n"
            "\tSend bytes: %-10lld  speed: %-9.0lf bps\n"
            "\tRecv bytes: %-10lld  speed: %-9.0lf bps\n"
            , ftell(local_fp), time, CalcSpeed(ftell(local_fp), time)
            , datan - 1, to_retrans_cnt + ooo_retrans_cnt, to_retrans_cnt, ooo_retrans_cnt
            , send_bytes, CalcSpeed(send_bytes, time)
            , recv_bytes, CalcSpeed(recv_bytes, time));
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
    /* 参数检查：检查参数个数是否正确，各参数是否合法 */
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

    /* 初始化
     * 1. 绑定Socket库
     * 2. 打开日志文件
     * 3. 创建客户端套接字与服务器地址
     * 4. 设置发送等待时间与接收等待时间
     */

    WSADATA wsa_data;
    WSAStartup(MAKEWORD(2,2), &wsa_data);
    logfile = fopen(LOGFILE, "w");

	// create client socket && server sockaddr
	client_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = inet_addr(addr);
	server_addr.sin_port = htons(69);

    // set time limit
    setsockopt(client_sockfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&recv_time_out, sizeof(int));
    setsockopt(client_sockfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&send_time_out, sizeof(int));

    /* 下载操作 */
    if (strcmp(action, "-r") == 0) {
        remote_filename = source;

        /* 构造缓存文件名，并打开缓存文件 */
        int target_len = strlen(target);
        local_filename = malloc(target_len + 8);
        strcpy(local_filename, target);
        strcpy(local_filename + target_len, ".ftptmp");
        OPEN_LOCAL_FILE("wb");

        int ret = Get();    /* 将文件下载到缓存文件中 */
        fclose(local_fp);
        if (ret) {          /* 下载失败，则删除缓存文件，并跳转至打印错误信息 */
            remove(local_filename);
            goto PrtErr;
        
        } else {        /* 下载成功 */      // read successed
            if (trans_mode[0] == 'n') { // netascii
                if (CheckNetascii(local_filename)) printf("[Warning] Received non-netascii mode data.\n");
            }
            /* 下载成功，将缓存文件重命名为目标文件 */
            remove(target);
            rename(local_filename, target);
        }
    
    /* 上传操作 */
    } else if (strcmp(action, "-w") == 0) {
        remote_filename = target;

        /* octet模式的上传操作：直接上传 */
        if (trans_mode[0] == 'o') { // octet
            local_filename = source;
            OPEN_LOCAL_FILE("rb");
            Put(); 
            fclose(local_fp);
            goto PrtErr;
        
        } else {         /* netascii模式的上传操作 */
            /* 将源文件转换为netascii模式，存放在缓存文件中，若转换失败则报错 */
            int source_len = strlen(source);
            local_filename = malloc(source_len + 8);
            strcpy(local_filename, source);
            strcpy(local_filename + source_len, ".ftptmp");
            int ret = Txt2Netascii(source, local_filename);
            if (ret) SET_ERROR_AND_GOTOPRT(ERRTYPE_NETASCII, ret);

            /* 转换成功：上传并删除缓存文件 */
            OPEN_LOCAL_FILE("rb");
            Put(); 
            fclose(local_fp);
            remove(local_filename);
            goto PrtErr;
        }
    }

    /* 打印错误信息 */
PrtErr:
    if (err_type) PrintError(); // print error
    return 0;
}