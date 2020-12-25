#include "netascii.h"

#include <stdio.h>

// RFC 5198

int CheckNetascii(const char *filename) {
    FILE* fp = fopen(filename, "rb");
    if (fp == NULL) return ERRFPCHECK;
    int ret = 0;
    char ch = 0, lastch;
    for (; ; ) {
        lastch = ch;
        ch = fgetc(fp);
        if (lastch == 0x0d && (ch != 0x0a && ch !=0x00)) /* 不合法的CR */
            ret = INVALID_CRorLF;
        if (ch == 0x0a && lastch != 0x0d) ret = INVALID_CRorLF; /* 不合法的LF */
        if (ch >= 0x20 && ch <= 0x7e) continue; /* 0x20到0x7e之间的可打印字符 */
        if (ch >= 0x07 && ch <= 0x0d) continue; /* 0x07到0x0d之的七个控制字符 */
        if (ch == 0) continue;                  /* 控制字符NUL(0x00) */
        if (ch == EOF) break;
        fclose(fp);
        return INVALIDCHAR; // invalid char
    }
    fclose(fp);
    return ret;
}

int Txt2Netascii(const char *filename, const char *tmpfile) {
    int ret = CheckNetascii(filename);
    if (ret != 0 && ret != INVALID_CRorLF) return ret;
    FILE* fpr = fopen(filename, "rb");
    if (fpr == NULL) return ERRFPR;
    FILE* fpw = fopen(tmpfile, "wb");
    if (fpw == NULL) {
        fclose(fpr);
        return ERRFPW;
    }
    char ch = 0, lastch;
    for (; ; ) {
        lastch = ch;
        ch = fgetc(fpr);
        if (lastch == 0x0d && (ch != 0x0a && ch !=0x00))fputc(0x00, fpw); // invalid CR
        if (ch == 0x0a && lastch != 0x0d) fputc(0x0d, fpw); // invalid LF
        if (ch == EOF) break;
        fputc(ch, fpw); 
    }
    printf("File raw size: %d, netascii size: %d.\n", ftell(fpr), ftell(fpw));
    fclose(fpr);
    fclose(fpw);
    return 0;
}