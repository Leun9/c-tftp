
#define INVALIDCR   2
#define INVALIDCHAR 3
#define ERRFPCHECK  4
#define ERRFPR      5
#define ERRFPW      6

int CheckNetascii(const char *filename);

int Txt2Netascii(const char *filename, const char *tmpfile);