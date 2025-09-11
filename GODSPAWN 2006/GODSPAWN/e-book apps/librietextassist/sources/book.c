#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "endian.h"

extern BYTE * checked_malloc(unsigned int length);


#include <zlib.h>

#include <sys/stat.h>
#include <fcntl.h>

#ifndef O_BINARY
#define O_BINARY 0
#endif
#define CREAT_PERMS S_IREAD|S_IWRITE

/* Magic.. Ask Sony why all LRF files start here */
WORD global_subfile = 0x32;

typedef unsigned char * SPTR;
#define SPTRDATA(s)  (s + sizeof(unsigned int) )
#define SPTRLEN(s)  (*((unsigned int *)s))
#define SPTRSETLEN(s,len)  {*((unsigned int *)s) = len;}

SPTR salloc(unsigned int len)
{
    unsigned char * p;

    p = checked_malloc(len + sizeof(unsigned int));
    *((unsigned int *)p) = len;
    return p;
}

SPTR read_whole_file(char * fname)
{
        off_t   filesize;
        unsigned char * fileptr;
        int     fd;
        int     nbytes;

        fd = open(fname, O_RDONLY|O_BINARY);
        if (fd < 0) {
                fprintf(stderr,"Unable to open \"%s\" for reading.\n",
                        fname);
                perror("Open");
                return NULL;
        }
        filesize = lseek(fd, 0, SEEK_END);
        (void)lseek(fd, 0, SEEK_SET);

        fileptr = salloc(filesize);

        nbytes = read(fd, SPTRDATA(fileptr), filesize);
        if (nbytes != filesize) {
                fprintf(stderr,"Failed reading \"%s\" - Only got %d bytes "
                        "out of %d.\n", fname, nbytes, filesize);
                perror("Reading");
                free(fileptr);
                close(fd);
                return NULL;
        }
        close(fd);
        return fileptr;
}

typedef struct tag
{
    struct tag * next;
    BYTE   tagid;
    SPTR   data;
    int    len;
    BYTE   shortdata[16];
} tag;

typedef struct subfile
{
    struct  subfile * next;
    int     size;   /* size of data associated with this */
    DWORD   location; /* Where was I put in the file */
    DWORD   id;       /* what # am I */
    WORD    type;     
    WORD    dataflags;
    SPTR    data;
    tag     * taglist;
} subfile;

tag * new_tag(BYTE tagid, int len, const BYTE * data, DWORD val)
{
    tag * q;

    q = (tag *)checked_malloc(sizeof(tag));
    memset(q, 0, sizeof(tag));
    q->tagid = tagid;
    if (len <= sizeof(q->shortdata)) 
    {
        q->len = len;
        if (data)
            memcpy(&q->shortdata[0], data, len);
        else 
            memcpy(&q->shortdata[0], &val, len);
        //printf("Here... Q->len = %d q->data %08lx\n",q->len, q->data);
    } else {
        q->len = 0;
        q->data = salloc(len);
        memcpy(SPTRDATA(q->data),data, len);
        //printf("Here2... Q->len = %d q->data %08lx\n",q->len, q->data);
    }
    return q;
}

void free_tag(tag * q)
{
    if (q->data) free(q->data);
    free(q);
}

void add_tag_to_subfile(subfile * q, tag * r)
{
    tag * p;
    /* inefficient. fix. */
    if (q->taglist) {
        p = q->taglist;
        while (p->next) {
            p = p->next;
        }        
        p->next = r;
        return;
    }
    else 
        q->taglist = r;
}

subfile * new_subfile(WORD type)
{
    subfile * q;

    q = (subfile *)checked_malloc(sizeof(subfile));
    memset(q, 0, sizeof(subfile));
    q->id = global_subfile++;
    q->type = type;
    return q;
}

void free_subfile(subfile * q)
{
    if (q->data) free(q->data);
    if (q->taglist) 
    {
        tag * newp, *p = q->taglist;
        while (p)
        {
            newp = p->next;
            free_tag(p);
            p = newp;
        } 
    }
    free(q);
}

SPTR compress_sptr(SPTR in)
{
    DWORD   newlen;
    SPTR    out;
    BYTE    * outp;
    int     zstatus;

    /* Recommended maximum length for compressed data from
     * zlib documentation */
    newlen = SPTRLEN(in) + ((SPTRLEN(in) / 1000) + 1) + 12 + 4;
    out = salloc(newlen + sizeof(DWORD));
    outp = SPTRDATA(out);
    WRITE_LE_DWORD(outp, SPTRLEN(in)); outp += sizeof(DWORD);
    zstatus = compress(outp,&newlen,
            SPTRDATA(in), SPTRLEN(in));
    if (zstatus != Z_OK) {
        fprintf(stderr,"Compress failed = %d\n", zstatus);
            return NULL;
    }  
    SPTRSETLEN(out, (newlen + sizeof(DWORD)));
    return out;
}

#define LRF_VERSION     0x08
#define LRF_PSUEDOKEY   0x0A
#define LRF_HEADSUBFILE 0x0C
#define LRF_SUBFILECOUNT   0x10
#define LRF_SUBFILEOFFSET  0x18
#define LRF_DIRECTION   0x24  
#define LRF_DIRECTION_FORWARDS  0x01
#define LRF_DIRECTION_BACKWARDS  0x10
#define LRF_UNK1     0x26
#define LRF_UNK2     0x2A
#define LRF_UNK3     0x2C
#define LRF_UNK4     0x2E
#define LRF_ALTSUBFILE  0x44        /* doesn't seem to be used */
#define LRF_UNK5     0x48
#define LRF_INFOLEN  0x4C
#define LRF_IMGLEN   0x50
#define LRF_UNK6     0x4E

int write_lrf_file( char * fname, SPTR infop, SPTR imagep, 
    subfile * subfilelist)
{
    BYTE  lrfheader[0x54];       
    DWORD running;
    int     nsubfiles;
    int     newlen;
    int     needed;
    SPTR    dataptr;
    int     fd = -1;
    int     error = -1, zstatus;
    unsigned char align[16];
    subfile * subp;

    memset(align, 0, sizeof(align));

    memset(lrfheader, 0, sizeof(lrfheader));
    running = sizeof(lrfheader);
    lrfheader[0] = 'L';
    lrfheader[2] = 'R';
    lrfheader[4] = 'F';

    /* Why? I don't know why. */
    WRITE_LE_WORD(&lrfheader[LRF_VERSION], 0x3E7);

    /* Completely arbitrary - I expect. I don't use this encryption anyway */
    WRITE_LE_WORD(&lrfheader[LRF_PSUEDOKEY], 0x30);
   
    lrfheader[LRF_DIRECTION] = LRF_DIRECTION_FORWARDS; 

    /* Dimensions?  */
    WRITE_LE_WORD(&lrfheader[LRF_UNK1],800*2);
    WRITE_LE_WORD(&lrfheader[LRF_UNK2],600);
    WRITE_LE_WORD(&lrfheader[LRF_UNK3],800);
    /* No clue at all*/
    lrfheader[LRF_UNK4] = 0x18;
    WRITE_LE_WORD(&lrfheader[LRF_UNK5], 0x1536);
    lrfheader[LRF_UNK6] = 0x14;

    dataptr = NULL;
    do {
        fd = open(fname, O_WRONLY|O_BINARY|O_EXCL|O_CREAT, CREAT_PERMS);
        if (fd < 0) {
                fprintf(stderr,"Unable to open \"%s\" for writing -- "
                        "Maybe it already exists?\n", fname);
                perror("Open (writing)");
                return -1;
        }
        running = sizeof(lrfheader);
        lseek(fd, running, SEEK_SET);

        /* XML file containing metadata */
        newlen = SPTRLEN(infop) + ((SPTRLEN(infop) / 1000) + 1) + 12 + 4;
        dataptr = checked_malloc(newlen + sizeof(DWORD));
        if (!dataptr) break;
        WRITE_LE_DWORD(dataptr, SPTRLEN(infop));
        zstatus = compress(dataptr + sizeof(DWORD),&newlen,
            SPTRDATA(infop), SPTRLEN(infop));
        if (zstatus != Z_OK) {
            fprintf(stderr,"Compress failed = %d\n", zstatus);
            break;
        }    
        newlen += sizeof(DWORD);
        if (newlen != write(fd, dataptr, newlen)) break;

        running += newlen;
        WRITE_LE_WORD(&lrfheader[LRF_INFOLEN], newlen);
        free(dataptr); dataptr = NULL; 

        /* Image file, 60x80 */
        WRITE_LE_DWORD(&lrfheader[LRF_IMGLEN], SPTRLEN(imagep));
        if (SPTRLEN(imagep) != write(fd, SPTRDATA(imagep), SPTRLEN(imagep)))
            break;

        running += SPTRLEN(imagep);;
    
        /* 16-byte align the first subfile. Unnecessary but done in 
         * every sample LRF file 
         */
        needed = (16 - (running & 0xF)) & 0x0f;
        if (needed != write(fd, align, needed)) break;
        running += needed;

        subp = subfilelist;
        /* First, write out the files */
        /* Lots of silly redundant writes here */
        nsubfiles = 0;
        while (subp) 
        {
            int     size;
            BYTE    tagbuffer[18];
            tag     * tagp;
            
            nsubfiles++;
            if ( subp->type == 0x1C) 
                WRITE_LE_WORD(&lrfheader[LRF_HEADSUBFILE], subp->id);
            if ( subp->type == 0x1e) 
                WRITE_LE_WORD(&lrfheader[LRF_ALTSUBFILE], subp->id);
            /* Lots of silly writes here */
            tagbuffer[0] = 0x0;
            tagbuffer[1] = 0xF5;
            WRITE_LE_DWORD(&tagbuffer[2], subp->id);
            WRITE_LE_WORD(&tagbuffer[6], subp->type);
            if (8 != write(fd, tagbuffer, 8)) break;
            size = 8;

            tagp = subp->taglist;
            while (tagp)
            {
                int len = 2;

                tagbuffer[0] = tagp->tagid;
                tagbuffer[1] = 0xF5;
                if (tagp->len <= 16) {
                    memcpy(&tagbuffer[2], tagp->shortdata, tagp->len);
                    len += tagp->len;
                } 
                if (len != write(fd,tagbuffer, len)) break;
                size += len;
                if (tagp->data)
                {
                    if (SPTRLEN(tagp->data) != write(fd, SPTRDATA(tagp->data), 
                        SPTRLEN(tagp->data))) break;
                    size += SPTRLEN(tagp->data); 
                }
                tagp = tagp->next;
            }    
            if (tagp) break;

            if (subp->data)
            {
                tagbuffer[0] = 0x54; /* dataflags */
                tagbuffer[1] = 0xF5;
                WRITE_LE_WORD(&tagbuffer[2], subp->dataflags);
                tagbuffer[4] = 0x04; /* length */
                tagbuffer[5] = 0xF5; 
                WRITE_LE_DWORD(&tagbuffer[6], SPTRLEN(subp->data));
                tagbuffer[10] = 0x05; /* start the data already */
                tagbuffer[11] = 0xF5; 
                if (12 != write(fd, tagbuffer, 12)) break;
                size += 12;
                
                if (SPTRLEN(subp->data) != write(fd, SPTRDATA(subp->data), 
                        SPTRLEN(subp->data))) break;
                size += SPTRLEN(subp->data); 

                tagbuffer[0] = 0x06; /* end of data */
                tagbuffer[1] = 0xF5;
                if (2 != write(fd, tagbuffer, 2)) break;
                size += 2;
            }
            tagbuffer[0] = 0x01; /* end of subfile */
            tagbuffer[1] = 0xF5;
            if (2 != write(fd, tagbuffer, 2)) break;
            size += 2;

            subp->location = running;
            running += size;
            subp->size = size;
            subp = subp->next;
        }
        if (subp) break;

        /* 16-byte align the subfile list. Unnecessary but done in 
         * every sample LRF file 
         */
        needed = (16 - (running & 0xF)) & 0x0f;
        if (needed != write(fd, align, needed)) break;
        running += needed;

        WRITE_LE_DWORD(&lrfheader[LRF_SUBFILECOUNT], nsubfiles);
        WRITE_LE_DWORD(&lrfheader[LRF_SUBFILEOFFSET], running);

        subp = subfilelist;
        while (subp)
        {
            BYTE   subrec[16];
           
            WRITE_LE_DWORD(&subrec[12],0);
            WRITE_LE_DWORD(&subrec[0], subp->id);
            WRITE_LE_DWORD(&subrec[4], subp->location);
            WRITE_LE_DWORD(&subrec[8], subp->size);
            if (sizeof(subrec) != write(fd, subrec, sizeof(subrec))) break;
            running += sizeof(subrec);
            subp = subp->next;
        }
        if (subp) break;
        lseek(fd, 0, SEEK_SET);
        write(fd, lrfheader, sizeof(lrfheader));
        error = 0;
    } while (0);
    if (dataptr) free(dataptr);
    if (fd < 0) close(fd);
    if (error) {
        fprintf(stderr,"Last position was: %d\n", running);
        perror("lRF writing process failed");
    }
    return error;
}

subfile * make_one_long_para(BYTE * page_ptr, DWORD byteCount)
{
    subfile * psub;
    SPTR    ppara;
    BYTE    *p, *q;
    char    *s;
    WORD    c;
    DWORD   j;

    psub = new_subfile(0x0A);
    ppara = salloc( byteCount + 6 + 4 + 10 /* 10 is slop.. */ );
    p = SPTRDATA(ppara);
    q = page_ptr;

    c = READ_LE_WORD(q);
    if (c == 0xFEFF) q += 2;

    *(p++) = 0xA1;
    *(p++) = 0xF5;
    WRITE_LE_DWORD(p, 0); p+=sizeof(DWORD);
    for (j = 0; j < byteCount/2; j++) 
    { 
        c = READ_LE_WORD(q); q += 2;
        if ((c == '\r') || (!c)) continue;
        if (c == '\n') c = 0xF5D2;
        WRITE_LE_WORD(p,c); p+=2;
    }
    *(p++) = 0xA2;
    *(p++) = 0xF5;
    SPTRSETLEN(ppara,(p - SPTRDATA(ppara)));


    if (SPTRLEN(ppara) > 64) 
    {
    SPTR    comppara;

    comppara = compress_sptr(ppara);
    if (comppara) { free(ppara); ppara = comppara; }
    psub->dataflags |= 0x100;   /* enable Compression */
    }
    psub->data = ppara;
    return psub;
}

/* simple way of splitting into pages */
void get_next_page(SPTR file, BYTE ** page_ptr, DWORD * page_len)
{
    int i, lines, nls;
    DWORD len, maxlen;
    BYTE * q;

    if (!*page_len) *page_ptr = SPTRDATA(file);
    if ( (*page_ptr - (SPTRDATA(file)) + *page_len) == SPTRLEN(file)) {
        *page_len = 0;
        return;
    }
    *page_ptr = (*page_ptr) + *page_len;
    q = *page_ptr;
    len = lines = nls = 0;
    maxlen = SPTRLEN(file) - (*page_ptr - SPTRDATA(file));
    while (len < maxlen) 
    {
        int c;
        c = READ_LE_WORD(q+len);
        if ( (c == '\n') || (c == '\r'))
        { 
            nls++; lines++;
        }
        else 
        {
            nls = 0;
        }
        len += 2;
        if (lines > 199) break;
        if ((nls == 6) && (lines > 30)) break;
    };
    *page_len = len; 
}

/* This is not how you do XML.  But adding a MB with libxml isn't worthwhile,
 * not to mention temporary files and other junk.
 */
SPTR  update_tag(SPTR data, char * tag, char * modification)
{
    BYTE * p, *q, * r;
    BYTE * endp = SPTRDATA(data) + SPTRLEN(data);
    SPTR newdata;
    char * s;
    int  found;

    newdata = salloc(SPTRLEN(data) + strlen(modification));
    p = SPTRDATA(data);
    q = SPTRDATA(newdata);
    found = 0;
    do 
    {
        int c;
        c = READ_LE_WORD(p);
        p+=2;
        WRITE_LE_WORD(q, c);     
        q+=2;
        if (c == '<')
        {
            BYTE * r;
            s = tag;
            r = p;
            while (*s) 
            {
                c = READ_LE_WORD(r); r += 2;
                if (r >= endp) break;
                if (c != *s) break;  
                s++;  
            }
            if (!*s) 
            {
                c = READ_LE_WORD(r); r+= 2;
                if ((c == '>') || (c == ' ')) 
                {
                    found++;
                    s = tag;
                    while (*s)  {
                        c = *s;
                        WRITE_LE_WORD(q, c); q += 2;
                        s++;
                    }
                    WRITE_LE_WORD(q, '>'); q += 2;
    
                    s = modification;
                    while (s && *s) 
                    {
                        c = *s;
                        WRITE_LE_WORD(q, c); q += 2;
                        s++;
                    }
                    WRITE_LE_WORD(q,'<');
                    while ((c = READ_LE_WORD(r)) != '<') {
                        if (r >= endp) break;
                        r += 2;
                    }
                    p = r;
                } 
            }
        }
    } while (p < endp);
    SPTRSETLEN(newdata,(q - SPTRDATA(newdata))); 
    return newdata;
}

const BYTE fontname78[] = {22, 0,'I',0,'W',0,'A',0,0x0E,0x66,'-',0,
        0x2D,0x4E,0x30,0x7D,'N',0,'-',0,'e',0,'b',0, /* HACK for 78 */
        0xFB,0x30};
const BYTE fontname[] = {22, 0,'I',0,'W',0,'A',0,0x0E,0x66,'-',0,
        0x2D,0x4E,0x30,0x7D,'N',0,'-',0,'e',0,'b',0};

#define MAX_ENTS 1000

static SPTR metadata = NULL;
static SPTR imagefile = NULL;
static subfile * head = NULL, *tail = NULL;
static DWORD font_id, margins_id, page_box_id;
static subfile * book;

static int pages = 0;

int provide_metadata(char * infofile, char * imagefilename, char * bookid)
{
    SPTR newmeta;
    char randbookid[16+1];
    char date[32];
    time_t  now;
    struct tm * timeptr;

    
    metadata = read_whole_file(infofile);
    if (!metadata) { 
        fprintf(stderr,"Unable to read file \"%s\".\n", infofile);
        return -1;
    }
    imagefile = read_whole_file(imagefilename);
    if (!imagefile) { 
        fprintf(stderr,"Unable to read file \"%s\".\n", imagefilename);
        free(metadata);
        return -1;
    }
    time(&now); 
    if (!bookid) 
    {
        sprintf(randbookid,"MAKELRF0%08lx",now);
        bookid = randbookid;
    } 
    newmeta = update_tag(metadata,"BookID",bookid);
    free(metadata); metadata = newmeta;
    newmeta = update_tag(metadata,"Producer","MakeLRF 0.3");
    free(metadata); metadata = newmeta;

    timeptr = localtime(&now);
    sprintf(date," %04d-%02d-%02d ",timeptr->tm_year + 1900,timeptr->tm_mon+1, timeptr->tm_mday);
    newmeta = update_tag(metadata,"CreationDate",date);
    free(metadata); metadata = newmeta;

    return 0;
}

int start_new_book()
{
    subfile * fontrec, * margins, * page_box;

    pages = 0;
    head = new_subfile(0x1C); /* Head node */
    add_tag_to_subfile(head, new_tag(0x75,sizeof(WORD),0,2));
    add_tag_to_subfile(head, new_tag(0x76,sizeof(WORD),0,0));
    add_tag_to_subfile(head, new_tag(0x77,sizeof(WORD),0,1));

    /* There _must_ be a 78 tag here for ANY font to work.
     -- Commenting both out, internal fonts are blank!?!
     */
    {
        BYTE tmp78[] = {0,0,0,0,0x16,0xf5,0,0, 0x1,0x30};
        add_tag_to_subfile(head, new_tag(0x78,sizeof(tmp78),tmp78,0));
    }
#if 0
    /* Eek, Handling 78 is aweful. This needs fixed */
    add_tag_to_subfile(head, new_tag(0x78,sizeof(DWORD),0,0));
    add_tag_to_subfile(head, new_tag(0x16,sizeof(fontname78),fontname78,0));
    /* End of 78/16 pair */
#endif 
    add_tag_to_subfile(head, new_tag(0x79,sizeof(WORD),0,2));
    add_tag_to_subfile(head, new_tag(0x7A,sizeof(WORD),0,0x10));
    add_tag_to_subfile(head, new_tag(0xDA,sizeof(WORD),0,2));

    tail = head;
    {
        subfile * temp; 
        SPTR unkdat;

        temp = new_subfile(0x1E); /* special ? */
        unkdat = salloc(4);
        memset(SPTRDATA(unkdat),0,SPTRLEN(unkdat));
        temp->dataflags = 0x51; 
        temp->data = unkdat;
        tail->next = temp;
    }
    tail = tail->next;

    /* Global font record for the file */
    fontrec = new_subfile(0x0b);
    add_tag_to_subfile(fontrec,new_tag(0x76,sizeof(WORD),0,0x0));
    add_tag_to_subfile(fontrec,new_tag(0x77,sizeof(WORD),0,0x1));
    add_tag_to_subfile(fontrec,new_tag(0x79,sizeof(WORD),0,0x1));
    add_tag_to_subfile(fontrec,new_tag(0x7A,sizeof(WORD),0,0x0));
    add_tag_to_subfile(fontrec,new_tag(0x11,sizeof(WORD),0,0x64));
    add_tag_to_subfile(fontrec,new_tag(0x12,sizeof(WORD),0,0xFFF6));
    add_tag_to_subfile(fontrec,new_tag(0x13,sizeof(WORD),0,0x0));
    add_tag_to_subfile(fontrec,new_tag(0x14,sizeof(WORD),0,0x0));
    add_tag_to_subfile(fontrec,new_tag(0x15,sizeof(WORD),0,0x190));
    add_tag_to_subfile(fontrec,new_tag(0x16,sizeof(fontname),fontname,0));
    add_tag_to_subfile(fontrec,new_tag(0x17,sizeof(DWORD),0,0));
    add_tag_to_subfile(fontrec,new_tag(0x18,sizeof(DWORD),0,0xff));
    add_tag_to_subfile(fontrec,new_tag(0x19,sizeof(WORD),0,0x19));
    add_tag_to_subfile(fontrec,new_tag(0x1a,sizeof(WORD),0,0));
    add_tag_to_subfile(fontrec,new_tag(0x1b,sizeof(WORD),0,0x8C));
    add_tag_to_subfile(fontrec,new_tag(0x1c,sizeof(WORD),0,0x0a));
    add_tag_to_subfile(fontrec,new_tag(0x1d,sizeof(WORD),0,0));
    add_tag_to_subfile(fontrec,new_tag(0x1e,sizeof(WORD),0,0));
    add_tag_to_subfile(fontrec,new_tag(0xf1,sizeof(WORD),0,2));
    add_tag_to_subfile(fontrec,new_tag(0xf2,sizeof(DWORD),0,0));
    add_tag_to_subfile(fontrec,new_tag(0x3c,sizeof(WORD),0,1));
    add_tag_to_subfile(fontrec,new_tag(0x3d,sizeof(WORD),0,1));
    add_tag_to_subfile(fontrec,new_tag(0x3e,sizeof(WORD),0,0));
    add_tag_to_subfile(fontrec,new_tag(0x75,sizeof(WORD),0,1));
    tail->next = fontrec;  tail = tail->next;
    font_id = fontrec->id; 

    book = new_subfile(0x01);
    /* will be filled in later */
    tail->next = book; tail = tail->next;

    add_tag_to_subfile(head, new_tag(0x7B,sizeof(DWORD),0,book->id));
   

    margins = new_subfile(0x05);
    /* Margins */
    add_tag_to_subfile(margins,new_tag(0x21,sizeof(WORD),0,5));
    add_tag_to_subfile(margins,new_tag(0x22,sizeof(WORD),0,0x35));
    add_tag_to_subfile(margins,new_tag(0x23,sizeof(WORD),0,5));
    add_tag_to_subfile(margins,new_tag(0x24,sizeof(WORD),0,0x2a));
    add_tag_to_subfile(margins,new_tag(0x2C,sizeof(WORD),0,0x2a));
    add_tag_to_subfile(margins,new_tag(0x25,sizeof(WORD),0,0x2a2));
    add_tag_to_subfile(margins,new_tag(0x26,sizeof(WORD),0,0x204));
    add_tag_to_subfile(margins,new_tag(0x27,sizeof(WORD),0,0x3a));
    add_tag_to_subfile(margins,new_tag(0x28,sizeof(WORD),0,0x35));
    add_tag_to_subfile(margins,new_tag(0x35,sizeof(WORD),0,0x34));
    add_tag_to_subfile(margins,new_tag(0x2b,sizeof(WORD),0,0));
    add_tag_to_subfile(margins,new_tag(0x2a,sizeof(WORD),0,1));
    add_tag_to_subfile(margins,new_tag(0xda,sizeof(WORD),0,2));
    {
        BYTE sixbytes[6];
        memset(sixbytes, 0, sizeof(sixbytes));
        sixbytes[0] = 1;
        add_tag_to_subfile(margins,new_tag(0x29,6,sixbytes,0));
    }
    tail->next = margins; tail = tail->next;
    margins_id = margins->id;

    page_box = new_subfile(0x07);
    add_tag_to_subfile(page_box,new_tag(0x31,sizeof(WORD),0,600));
    add_tag_to_subfile(page_box,new_tag(0x32,sizeof(WORD),0,800));
    add_tag_to_subfile(page_box,new_tag(0x33,sizeof(WORD),0,0x12));
    //add_tag_to_subfile(page_box,new_tag(0x31,sizeof(WORD),0,0x204));
    //add_tag_to_subfile(page_box,new_tag(0x32,sizeof(WORD),0,0x64));
    //add_tag_to_subfile(page_box,new_tag(0x33,sizeof(WORD),0,0x12));
    add_tag_to_subfile(page_box,new_tag(0x34,sizeof(DWORD),0,0xFF));
    add_tag_to_subfile(page_box,new_tag(0x35,sizeof(WORD),0,0x34));
    //add_tag_to_subfile(page_box,new_tag(0x35,sizeof(WORD),0,0));
    add_tag_to_subfile(page_box,new_tag(0x36,sizeof(WORD),0,0));
    add_tag_to_subfile(page_box,new_tag(0x37,sizeof(DWORD),0,0));
    add_tag_to_subfile(page_box,new_tag(0x2e,sizeof(WORD),0,1));
    add_tag_to_subfile(page_box,new_tag(0x38,sizeof(WORD),0,0));
    add_tag_to_subfile(page_box,new_tag(0x39,sizeof(WORD),0,0));
    {
        BYTE sixbytes[6];
        memset(sixbytes, 0, sizeof(sixbytes));
        sixbytes[0] = 1;
        add_tag_to_subfile(page_box,new_tag(0x29,6,sixbytes,0));
    }
    tail->next = page_box;  tail = tail->next;
    page_box_id = page_box->id;
    return 0;
}



int update_metadata(char * tag, char * newdata)
{
    SPTR newmeta;

    if (!metadata) return -1;
    newmeta = update_tag(metadata, tag, newdata);
    free(metadata);
    metadata = newmeta;
    return 0;
}

/* Convert to UTF16, or do nothing if its already the case */
SPTR convert_to_utf16(SPTR file)
{
    BYTE * p, * q;
    int     i, c;
    SPTR    newtext;

    if (SPTRLEN(file) < 2) return NULL;
    p = SPTRDATA(file);
    if (READ_LE_WORD(p) == 0xFEFF) return NULL;
    newtext = salloc(2*SPTRLEN(file) + 2);
    q = SPTRDATA(newtext);
    WRITE_LE_WORD(q, 0xFEFF); q+= 2;
    for (i = 0; i < SPTRLEN(file); i++) 
    {
        c = *(p++);
        WRITE_LE_WORD(q, c); q+= 2;
    }
    return newtext;
}

int add_txt_to_book(char * textfilename, int chapterize)
{
    SPTR textfile, newtext;
    int  page_len;
    static BYTE pagefiles[MAX_ENTS*4 + 2];
    int  nfile = 0;
    BYTE * page_ptr; 
    subfile * page, * box, * para, *physical_pages;

    if ((!head)||(!tail)) return -1;

    textfile = read_whole_file(textfilename);
    if (!textfile) {
        fprintf(stderr,"Unable to read file \"%s\".\n", textfilename);
        return -1;
    }
    newtext = convert_to_utf16(textfile);
    if (newtext) { free(textfile); textfile = newtext; }

    page_len = 0;

    WRITE_LE_DWORD(&pagefiles[nfile*4+2],font_id); nfile++;
    do {
        if (chapterize) {
            get_next_page(textfile,&page_ptr, &page_len);
            if (!page_len) break;
        } 
        else 
        {
            page_ptr = SPTRDATA(textfile);
            page_len = SPTRLEN(textfile);  
        }
        //printf("chapterize %d page_ptr %x page_len %d of %d \n", chapterize, 
        //    page_ptr, page_len, SPTRLEN(textfile));
        /* One box is all I need */
        para = make_one_long_para(page_ptr, page_len);
        tail->next = para;  tail = tail->next;

        /* Font Size */
        // add_tag_to_subfile(para,new_tag(0x11,sizeof(WORD),0,0x50));
        add_tag_to_subfile(para,new_tag(0x03,sizeof(DWORD),0,font_id));
        WRITE_LE_DWORD(&pagefiles[nfile*4+2],para->id); nfile++;

        /* the para goes into a bounding box */
        box = new_subfile(0x06);
        add_tag_to_subfile(box, new_tag(0x03,sizeof(DWORD),0, page_box_id));

        /** ?? */
        // add_tag_to_subfile(box,new_tag(0x31,sizeof(WORD),0,600));
        // add_tag_to_subfile(box,new_tag(0x32,sizeof(WORD),0,800));
        // add_tag_to_subfile(box,new_tag(0x33,sizeof(WORD),0,0x22));
        {
            SPTR boxdat;
            BYTE * p;
            boxdat = salloc(6);
            p = SPTRDATA(boxdat);
            *(p++) = 0x03; *(p++) = 0xF5;
            WRITE_LE_DWORD(p, para->id);
            box->data = boxdat;
        }
        tail->next = box;  tail = tail->next;
        WRITE_LE_DWORD(&pagefiles[nfile*4+2],box->id); nfile++;

        page = new_subfile(0x02);
        WRITE_LE_WORD(&pagefiles[0], nfile);

        /* page points back to book */ 
        add_tag_to_subfile(page, new_tag(0x7C,sizeof(DWORD),0,book->id));
        
        add_tag_to_subfile(page,new_tag(0x0b,nfile*4+2,pagefiles,0));
        add_tag_to_subfile(page,new_tag(0x03,sizeof(DWORD),0,margins_id));

        /* The pages's data is the box I just made */
        {
            SPTR pagedat = salloc(6);
            BYTE * boxlist = SPTRDATA(pagedat);
            boxlist[0] = 0x03;
            boxlist[1] = 0xF5;
            WRITE_LE_DWORD(&boxlist[2], box->id);
            page->data = pagedat;
        }
        tail->next = page;  tail = tail->next;

        pages++;

        physical_pages = new_subfile(0x1a);
        physical_pages->dataflags = 0x82;
        {
            SPTR laydat;
            BYTE * p;
            laydat = salloc(24); /* even for 1 box, extra allows */
                                 /* splitting paragraphs to their own pages */
            p = SPTRDATA(laydat);
            memset(SPTRDATA(laydat), 0, 24);
            WRITE_LE_DWORD(p, 1); p +=  sizeof(DWORD);
            WRITE_LE_DWORD(p, box->id); p +=  sizeof(DWORD);
            physical_pages->data = laydat;
        }
        tail->next = physical_pages;  tail = tail->next;
        add_tag_to_subfile(page,new_tag(0x02,sizeof(DWORD),0,physical_pages->id));
    } while (chapterize);
    return 0;
}

int get_gif_dimensions(SPTR giffile, int * x, int * y)
{
    unsigned char * pIn = SPTRDATA(giffile);
    int remaining = SPTRLEN(giffile);
    unsigned char flags;
    int len, blocklen;
    int state;
    unsigned char *p;

    int width = 0;
    int curx = 0, cury = 0;
    int height = 0;

    len = 13;
    state = 0;
    p = pIn + len;
    while ((len <= remaining) && (state < 100))
    {
        switch (state)
        {
        case 0:
            flags = *(p - 7 + 4);
            /* GCT */
            if (flags & 0x80) {

                len += (3 * ( 1 << ((flags & 7)+1)));
            }
            len++;
            state++;
            break;
        case 1:
            switch ( *(p - 1) ) {
                case 0x2C:
                    width = READ_LE_WORD(p + 4);         
                    height = READ_LE_WORD(p + 6);         
                    if (width > curx) curx = width;
                    if (height > cury) cury = height;
                    len += 10;
                    state++;
                    break;
                case 0x3B:
                    state = 100;
                    break;
                case 0x21:
                    len += 2;
                    state = 3;
                    break;
                default: /* Error Condition probably */
                    return -1;
            }
            break;
        case 2:
            /* Local color table, or skip the "LZW Minimum code size */
            flags = *((p - 2));
            if (flags & 0x80) {
                len += (3 * ( 1 << ((flags & 7)+1)));
            }
            else {
                len++; 
            }
            state++;
            break;
        case 3:
            /* data block */
            blocklen = *(p - 1);
            if (blocklen) {
                len += blocklen;
            }
            else state = 1;
            len++;
            break;
        default:
            return -1;
        }
        p = pIn + len;
    }
    if (state != 100) {
        return -1;
    }

    *x = curx;
    *y = cury;
    return 0;
}

SPTR string_to_pascal_utf16(char * string, int chars)
{
    char * s;
    int len, c;
    SPTR dat;
    BYTE * q;

    if (!chars) len = strlen(string);
    else len = chars;
    s = string;
    dat = salloc(2*len + 2);
    q = SPTRDATA(dat);
    WRITE_LE_WORD(q, 2*len); q += 2;
    while (len--)
    {
        c = *s;
        if (c) s++;
        WRITE_LE_WORD(q, c); q += 2;
    }
    return dat;
}

unsigned char testdata[] =
{
30,0,
0x49,0x00,0x57,0x00,0x41,0x00,0x2A,0x59,0xB4,0x30,0xB7,0x30,0xC3,0x30,0xAF,
0x30,0xAA,0x30,0xFC,0x30,0xEB,0x30,0xC9,0x30,0x2D,0x00,0x65,0x00,0x62,0x00   
};

int add_ttf_to_book(char * fname) 
{
    SPTR    fontfile, facename, filename, compfontfile;
    subfile * fontstream, * fontrec;
    char    * sbase, *sdot;

    if ((!head)||(!tail)) return -1;
    fontfile = read_whole_file(fname);
    if (!fontfile) {
        fprintf(stderr,"Unable to read gif file \"%s\".\n", fname);
        return -1;
    }
    compfontfile = NULL;
#if 1
    compfontfile = compress_sptr(fontfile);
    if (compfontfile) {
        free(fontfile);
        fontfile = compfontfile;
    }
    else {
        printf("*** Compressing the font file failed?!\n");
    }
#endif
#ifdef _MSC_VER
    sbase = strrchr(fname, '\\');
#else 
    sbase = strrchr(fname, '/'); 
#endif
    if (!sbase) sbase = fname;
    else sbase++;
    sdot = strchr(sbase,'.');
    if (!sdot) sdot = sbase + strlen(sbase);
    /* Yes, this isn't accurate. But any name in a pinch */
    facename = string_to_pascal_utf16(sbase, sdot - sbase);
#if 0
    facename = salloc(sizeof(testdata));
    memcpy(SPTRDATA(facename),testdata, sizeof(testdata));
#endif
    filename = string_to_pascal_utf16(sbase, 0);

    fontstream  = new_subfile(0x19); 
    add_tag_to_subfile(fontstream, new_tag(0x59,SPTRLEN(filename), 
        SPTRDATA(filename),0));
    add_tag_to_subfile(fontstream, new_tag(0x5d,SPTRLEN(facename), 
        SPTRDATA(facename),0));
    fontstream->dataflags = 0x41;
    if (compfontfile)
        fontstream->dataflags |= 0x100;
    fontstream->data = fontfile;
    tail->next = fontstream;  tail = tail->next;

    // Link in the font record 
    add_tag_to_subfile(head,new_tag(0xD8,sizeof(DWORD),0,fontstream->id));
    
    fontrec = new_subfile(0x0b);
    add_tag_to_subfile(fontrec,new_tag(0x76,sizeof(WORD),0,0x0));
    add_tag_to_subfile(fontrec,new_tag(0x77,sizeof(WORD),0,0x1));
    add_tag_to_subfile(fontrec,new_tag(0x79,sizeof(WORD),0,0x1));
    add_tag_to_subfile(fontrec,new_tag(0x7A,sizeof(WORD),0,0x0));
    add_tag_to_subfile(fontrec,new_tag(0x11,sizeof(WORD),0,0x64));
    add_tag_to_subfile(fontrec,new_tag(0x12,sizeof(WORD),0,0xFFF6));
    add_tag_to_subfile(fontrec,new_tag(0x13,sizeof(WORD),0,0x0));
    add_tag_to_subfile(fontrec,new_tag(0x14,sizeof(WORD),0,0x0));
    add_tag_to_subfile(fontrec,new_tag(0x15,sizeof(WORD),0,0x190));
    add_tag_to_subfile(fontrec,new_tag(0x16,SPTRLEN(facename),
        SPTRDATA(facename),0));
    add_tag_to_subfile(fontrec,new_tag(0x17,sizeof(DWORD),0,0));
    add_tag_to_subfile(fontrec,new_tag(0x18,sizeof(DWORD),0,0xff));
    add_tag_to_subfile(fontrec,new_tag(0x19,sizeof(WORD),0,0x19));
    add_tag_to_subfile(fontrec,new_tag(0x1a,sizeof(WORD),0,0));
    add_tag_to_subfile(fontrec,new_tag(0x1b,sizeof(WORD),0,0x8C));
    add_tag_to_subfile(fontrec,new_tag(0x1c,sizeof(WORD),0,0x0a));
    add_tag_to_subfile(fontrec,new_tag(0x1d,sizeof(WORD),0,0));
    add_tag_to_subfile(fontrec,new_tag(0x1e,sizeof(WORD),0,0));
    add_tag_to_subfile(fontrec,new_tag(0xf1,sizeof(WORD),0,2));
    add_tag_to_subfile(fontrec,new_tag(0xf2,sizeof(DWORD),0,0));
    add_tag_to_subfile(fontrec,new_tag(0x3c,sizeof(WORD),0,1));
    add_tag_to_subfile(fontrec,new_tag(0x3d,sizeof(WORD),0,1));
    add_tag_to_subfile(fontrec,new_tag(0x3e,sizeof(WORD),0,0));
    add_tag_to_subfile(fontrec,new_tag(0x75,sizeof(WORD),0,1));
    tail->next = fontrec;  tail = tail->next;
    font_id = fontrec->id; 

    return 0;
}

int add_gif_to_book(char * giffilename)
{
    SPTR    giffile;
    int     trouble;
    int     x, y;
    subfile * margins;
    static  BYTE pagefiles[MAX_ENTS*4 + 2];
    int     nfile = 0;
    subfile * page, * box, * image, *imageholder, *  physical_pages;
    subfile * blockattr;


    if ((!head)||(!tail)) return -1;
    giffile = read_whole_file(giffilename);
    if (!giffile) {
        fprintf(stderr,"Unable to read gif file \"%s\".\n", giffilename);
        return -1;
    }
    trouble = get_gif_dimensions(giffile, &x, &y);
    if ((x > 600) || (y > 800)) 
    {
        fprintf(stderr,"GIF is too big (%d x %d) > (600 x 800).\n", 
            x, y);
        return 1;
    }
    
    margins = new_subfile(0x05);
    /* Margins */
    add_tag_to_subfile(margins,new_tag(0x21,sizeof(WORD),0,0));
    add_tag_to_subfile(margins,new_tag(0x22,sizeof(WORD),0,0));
    add_tag_to_subfile(margins,new_tag(0x23,sizeof(WORD),0,(800 - y)/2));
    add_tag_to_subfile(margins,new_tag(0x24,sizeof(WORD),0,(600 - x)/2));
    add_tag_to_subfile(margins,new_tag(0x2C,sizeof(WORD),0,(600 - x)/2));
    add_tag_to_subfile(margins,new_tag(0x25,sizeof(WORD),0,y));
    add_tag_to_subfile(margins,new_tag(0x26,sizeof(WORD),0,x));
    add_tag_to_subfile(margins,new_tag(0x27,sizeof(WORD),0,0));
    add_tag_to_subfile(margins,new_tag(0x28,sizeof(WORD),0,0));
    add_tag_to_subfile(margins,new_tag(0x35,sizeof(WORD),0,0x41));
    add_tag_to_subfile(margins,new_tag(0x2b,sizeof(WORD),0,2));
    add_tag_to_subfile(margins,new_tag(0x2a,sizeof(WORD),0,1));
    add_tag_to_subfile(margins,new_tag(0xda,sizeof(WORD),0,1));        
    {
        BYTE sixbytes[6];
        memset(sixbytes, 0, sizeof(sixbytes));
        add_tag_to_subfile(margins,new_tag(0x29,6,sixbytes,0));
    }
    tail->next = margins; tail = tail->next;

    image = new_subfile(0x11); 
    image->dataflags = 0x14;
    image->data = giffile;
    tail->next = image;  tail = tail->next;
    WRITE_LE_DWORD(&pagefiles[nfile*4+2],image->id); nfile++;

    /* the image holder class keeps around the dimensions of the image */
    imageholder = new_subfile(0x0c);
    {
        BYTE imagedat[8];

        WRITE_LE_DWORD(&imagedat[0], 0);
        WRITE_LE_WORD(&imagedat[4], x);
        WRITE_LE_WORD(&imagedat[6], y);
        /* Redundant, but seems common */
        add_tag_to_subfile(imageholder,new_tag(0x4a,8,imagedat,0));
        add_tag_to_subfile(imageholder,new_tag(0x4b,4,imagedat+4,0));
    } 
    add_tag_to_subfile(imageholder,new_tag(0x4c,4,0,image->id));
    tail->next = imageholder;  tail = tail->next;
    WRITE_LE_DWORD(&pagefiles[nfile*4+2],imageholder->id); nfile++;

    blockattr = new_subfile(0x07);
    //add_tag_to_subfile(blockattr,new_tag(0x31,sizeof(WORD),0,600));
    //add_tag_to_subfile(blockattr,new_tag(0x32,sizeof(WORD),0,800));
    //add_tag_to_subfile(blockattr,new_tag(0x33,sizeof(WORD),0,0x12));
    add_tag_to_subfile(blockattr,new_tag(0x31,sizeof(WORD),0,0x204));
    add_tag_to_subfile(blockattr,new_tag(0x32,sizeof(WORD),0,0x64));
    add_tag_to_subfile(blockattr,new_tag(0x33,sizeof(WORD),0,0x12));
    add_tag_to_subfile(blockattr,new_tag(0x34,sizeof(DWORD),0,0xFF));
    add_tag_to_subfile(blockattr,new_tag(0x35,sizeof(WORD),0,0x34));
    //add_tag_to_subfile(blockattr,new_tag(0x35,sizeof(WORD),0,0));
    add_tag_to_subfile(blockattr,new_tag(0x36,sizeof(WORD),0,0));
    add_tag_to_subfile(blockattr,new_tag(0x37,sizeof(DWORD),0,0));
    add_tag_to_subfile(blockattr,new_tag(0x2e,sizeof(WORD),0,1));
    add_tag_to_subfile(blockattr,new_tag(0x38,sizeof(WORD),0,0));
    add_tag_to_subfile(blockattr,new_tag(0x39,sizeof(WORD),0,0));
    add_tag_to_subfile(blockattr,new_tag(0x3a,sizeof(WORD),0,0));
    {
        BYTE sixbytes[6];
        memset(sixbytes, 0, sizeof(sixbytes));
        sixbytes[0] = 1;
        add_tag_to_subfile(blockattr,new_tag(0x29,6,sixbytes,0));
    }
    tail->next = blockattr;  tail = tail->next;
    WRITE_LE_DWORD(&pagefiles[nfile*4+2],blockattr->id); nfile++;

    /* the image holder goes into a bounding box */
    box = new_subfile(0x06);
    add_tag_to_subfile(box, new_tag(0x03,sizeof(DWORD),0, blockattr->id));
    add_tag_to_subfile(box,new_tag(0x31,sizeof(WORD),0,x));
    add_tag_to_subfile(box,new_tag(0x32,sizeof(WORD),0,y));
    add_tag_to_subfile(box,new_tag(0x33,sizeof(WORD),0,0x22));
    {
        SPTR boxdat;
        BYTE * p;
        boxdat = salloc(6);
        p = SPTRDATA(boxdat);
        *(p++) = 0x03; *(p++) = 0xF5;
        WRITE_LE_DWORD(p, imageholder->id);
        box->data = boxdat;
    }
    tail->next = box;  tail = tail->next;
    WRITE_LE_DWORD(&pagefiles[nfile*4+2],box->id); nfile++;

    page = new_subfile(0x02);
    WRITE_LE_WORD(&pagefiles[0], nfile);

    /* page points back to book */ 
    add_tag_to_subfile(page, new_tag(0x7C,sizeof(DWORD),0,book->id));
        
    add_tag_to_subfile(page,new_tag(0x0b,nfile*4+2,pagefiles,0));
    add_tag_to_subfile(page,new_tag(0x03,sizeof(DWORD),0,margins->id));

    /* The pages's data is the box I just made */
    {
        SPTR pagedat = salloc(6);
        BYTE * boxlist = SPTRDATA(pagedat);
        boxlist[0] = 0x03;
        boxlist[1] = 0xF5;
        WRITE_LE_DWORD(&boxlist[2], box->id);
        page->data = pagedat;
    }
    tail->next = page;  tail = tail->next;

    pages++;

    physical_pages = new_subfile(0x1a);
    physical_pages->dataflags = 0x82;
    {
        SPTR laydat;
        BYTE * p;
        laydat = salloc(24); /* even for 1 box, extra allows */
            /* splitting paragraphs to their own pages */
        p = SPTRDATA(laydat);
        WRITE_LE_DWORD(p, 1); p +=  sizeof(DWORD);
        WRITE_LE_DWORD(p, box->id); p +=  sizeof(DWORD);
        physical_pages->data = laydat;
    }
    tail->next = physical_pages;  tail = tail->next;
    add_tag_to_subfile(page,new_tag(0x02,sizeof(DWORD),0,physical_pages->id));
    return 0;
}

int write_book_to_file(char * outfile)
{
    SPTR newmeta;
    BYTE * bookpageslist;
    subfile * pagenums; 
    SPTR pagenumsdata;
    char pagetext[10];
    int i;
    subfile * ps;
    BYTE * p, * q;

    if (!metadata) return -1;
    sprintf(pagetext,"%d",pages);
    newmeta = update_tag(metadata, "Page", pagetext);
    free(metadata); metadata = newmeta;

    pagenumsdata = salloc(6 * pages + 4); 
    p = SPTRDATA(pagenumsdata);
    WRITE_LE_DWORD(p, pages); p+=4;
    bookpageslist = checked_malloc(4 * pages + 2);
    q = bookpageslist;
    WRITE_LE_WORD(q, pages); q+=2;
    ps = head;
    i = 0;
    while (ps)
    {
        if (ps->type == 2) 
        {
            if (i >= pages) 
            {
                fprintf(stderr,"Too many page records found in list.\n");
                return 1;
            }
            WRITE_LE_DWORD(p,ps->id); p += 4;
            WRITE_LE_WORD(p, 1);  p+= 2; /* 1 physical page per page... not accurate */
            WRITE_LE_DWORD(q,ps->id); q += 4;
            i++;
        }
        ps = ps->next;
    }
    pagenums = new_subfile(0x1a);
    pagenums->dataflags = 0x81;
    pagenums->data = pagenumsdata;
    tail->next = pagenums; tail = tail->next;

    add_tag_to_subfile(book, new_tag(0x02,sizeof(DWORD),0,pagenums->id));
    add_tag_to_subfile(book, new_tag(0x5C,pages*4 + 2,bookpageslist,0));

    return write_lrf_file(outfile,metadata,imagefile,head);
}

void end_book()
{
    if (head) { free_subfile(head); }
    if (metadata) { free(metadata); }
    if (imagefile) { free(imagefile); }
    head = tail = NULL;
    metadata = NULL;
    imagefile = NULL;
    pages = 0;
}
