#ifndef _ENDIAN_H_
#define _ENDIAN_H_

typedef     unsigned char       BYTE;
typedef     unsigned short int  WORD;
typedef     unsigned long int   DWORD;
#ifdef _MSC_VER
typedef     unsigned __int64    QWORD;
#else
typedef     unsigned long long  QWORD;
#endif

#define READ_LE_DWORD(pv) (  (DWORD)*((BYTE *)(pv))                + \
                        ((DWORD)(*((BYTE *)(pv) + 1)) <<  8)  + \
                        ((DWORD)(*((BYTE *)(pv) + 2)) << 16)  + \
                        ((DWORD)(*((BYTE *)(pv) + 3)) << 24)  )

#define READ_LE_WORD(pv) (int)((WORD)*((BYTE *)(pv))                + \
                           ((WORD)(*((BYTE *)(pv) + 1)) <<  8)  )
                        

#define WRITE_LE_DWORD(p,x) ( *((p)+0)=(BYTE)((x)&0xff),      \
                         *((p)+1)=(BYTE)(((x)>>8)&0xff), \
                         *((p)+2)=(BYTE)(((x)>>16)&0xff),\
                         *((p)+3)=(BYTE)(((x)>>24)&0xff) )

#define WRITE_LE_WORD(p,x) ( *((p)+0)=(BYTE)((x)&0xff),      \
                         *((p)+1)=(BYTE)(((x)>>8)&0xff) )  

#endif
