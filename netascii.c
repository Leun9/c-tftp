#include "netascii.h"

#include <stdio.h>

// RFC 854

int CheckNetascii(const char *filename) {
    FILE* fp = fopen(filename, "rb");
    if (fp == NULL) return ERRFPCHECK;
    int ret = 0;
    for (; ; ) {
        char ch = fgetc(fp);
        if (ch == EOF) break;
        if (ch == 0x13) { // cr
            ch = fgetc(fp);
            if (ch != 0x10 || ch !=0x00) ret = INVALIDCR; // invalid CR
            fseek(fp, -1L, SEEK_CUR);
            continue;
        }
        if (ch >= 0x20 && ch <= 0x7e) continue;
        if (ch >= 0x07 && ch <= 0x12) continue;
        if (ch == 0) continue;
        // printf("Invalid char 0x%02x.\n", (int)ch & 0xff);
        fclose(fp);
        return INVALIDCHAR; // invalid char
    }
    fclose(fp);
    return ret;
}

int Txt2Netascii(const char *filename, const char *tmpfile) {
    int ret = CheckNetascii(filename);
    if (ret != 0 && ret != INVALIDCR) return ret;
    FILE* fpr = fopen(filename, "rb");
    if (fpr == NULL) return ERRFPR;
    FILE* fpw = fopen(tmpfile, "wb");
    if (fpw == NULL) {
        fclose(fpr);
        return ERRFPW;
    }
    for (; ;) {
        char ch = fgetc(fpr);
        if (ch == EOF) break;
        fputc(ch, fpw);
        if (ch == 0x13) { // cr
            ch = fgetc(fpr);
            if (ch != 0x00 && ch != 0x10) fputc(0x10, fpw);
            fseek(fpr, -1L, SEEK_CUR);
        }
    }
    printf("File raw size: %d, netascii size: %d.\n", ftell(fpr), ftell(fpw));
    fclose(fpr);
    fclose(fpw);
    return 0;
}