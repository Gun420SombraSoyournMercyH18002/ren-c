EXTERN_C REBYTE *SHA1(const REBYTE *data, REBLEN data_len, REBYTE *md);
EXTERN_C void SHA1_Init(void *c);
EXTERN_C void SHA1_Update(void *c, const REBYTE *data, REBLEN len);
EXTERN_C void SHA1_Final(REBYTE *md, void *c);
EXTERN_C int SHA1_CtxSize(void);

