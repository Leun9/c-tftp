#include "netascii.h"

#include <stdio.h>

// RFC 854

int CheckNetascii(const char *filename) {
    FILE* fp = fopen(filename, "rb");
    if (fp == NULL) return ERRFPCHECK;
    int ret = 0;
    char ch = 0, lastch;
    for (; ; ) {
        lastch = ch;
        ch = fgetc(fp);
        if (lastch == 0x0d && (ch != 0x0a && ch !=0x00)) ret = INVALID_CRorLF; // invalid CR
        if (ch == 0x0a && lastch != 0x0d) ret = INVALID_CRorLF; // invalid LF
        if (ch >= 0x20 && ch <= 0x7e) continue;
        if (ch >= 0x07 && ch <= 0x0d) continue;
        if (ch == 0) continue;
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
        if (lastch == 0x0d && (ch != 0x0a && ch !=0x00)) fputc(0x00, fpw); // invalid CR
        if (ch == 0x0a && lastch != 0x0d) fputc(0x0d, fpw); // invalid LF
        if (ch == EOF) break;
        fputc(ch, fpw); 
    }
    printf("File raw size: %d, netascii size: %d.\n", ftell(fpr), ftell(fpw));
    fclose(fpr);
    fclose(fpw);
    return 0;
}