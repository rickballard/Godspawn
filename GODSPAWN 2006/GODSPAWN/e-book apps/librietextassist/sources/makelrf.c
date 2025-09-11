
#include <stdio.h>
#include <stdlib.h>
#include "endian.h"

BYTE * checked_malloc(unsigned int len)
{
    unsigned char * p;

    p = malloc(len); 
    if (!p)
    {
        fprintf(stderr,"malloc(%d) failed. Bad. Won't continue.\n");
        exit(-1);
    }    
    return p;
}

char * bookid = NULL;
char * author = NULL;
char * title = NULL;
char * description = NULL;
char * infofile = NULL;
char * imagefile = NULL;
char * outputfile = NULL;
int  full = 0; 
int  files_processed = 0;

extern int start_new_book(void);
extern int add_txt_to_book(const char * filename, int chapterize);
extern int add_gif_to_book(const char * filename);
extern int add_ttf_to_book(const char * filename);
extern int provide_metadata(const char * xmlfilename, 
            const char * imagefilename, const char * bookid);
extern int update_metadata(const char * tag, const char * newdata);
extern int write_book_to_file(const char * filename);
extern void end_book();

int process_one_arg(const char * s)
{
    static char ** storage = NULL;

    if (storage) 
    {
        char * news;
        news = checked_malloc(strlen(s) + 1); 
        memcpy(news, s, strlen(s)+1);
        *storage = news;
        storage = NULL;
        return 0;
    }
    if (s[0] == '-')
    {
        switch (tolower(s[1])) 
        {
        case 'a': storage = &author; break;
        case 'b': storage = &bookid; break;
        case 'd': storage = &description; break;
        case 'f': full = full ^ 1; break;
        case 'i': storage = &imagefile; break;
        case 'o': storage = &outputfile; break;
        case 't': storage = &title; break;
        case 'x': storage = &infofile; break;
        default:
            return 1;
        }
    }
    else 
    {
        const char * ext;
        ext = s + strlen(s) - 4;
        if ( ext[0] == '.' && (tolower(ext[1]) == 'g') &&
             (tolower(ext[2]) == 'i') && (tolower(ext[3]) == 'f'))
        {
            files_processed++;
            return add_gif_to_book(s);
        }
        else if ( ext[0] == '.' && (tolower(ext[1]) == 't') &&
             (tolower(ext[2]) == 'x') && (tolower(ext[3]) == 't')) 
        {
            files_processed++;
            return add_txt_to_book(s, full ^ 1);
        }
        else if ( ext[0] == '.' && (tolower(ext[1]) == 't') &&
             (tolower(ext[2]) == 't') && (tolower(ext[3]) == 'f')) 
        {
            return add_ttf_to_book(s);
        }
        else 
        {
            return 1;
        }
    }
    return 0;
}

static char linebuf[160];

int main(int argc, char ** argv)
{
    int i, trouble;

    if (argc <= 1) {
        fprintf(stderr,
            "makelrf "\
            "\t\t\tVersion 0.3\n"\
            "Command Line Switches: \n"\
            "\t -b \"bookid\"  \t\tDefault - Random! \n"\
            "\t -d \"One line description\" \t\tDescription.\n"\
            "\t -o \"output.lrf\" \t\tOutput filename. No default. \n"\
            "\t -x \"info.xml\" \t\tChange metadata XML file.\n"\
            "\t -a \"Author Name\"  \t\t Provide an author's name.\n"\
            "\t -t \"Title \"  \t\tProvide a title.\n"\
            "\t -i \"image.gif\" \t\tDifferent image - Image.gif is default.\n"\
            "\t -f \t\t Toggle Full text - no chapterization. \n"\
            "\t @filename \t\t Read parameters from a file \n"\
            "\t filename.gif \t\t Must be 600 wide by 800 high or LESS. \n"\
            "\t filename.txt \t\t Text file to include. \n");
        exit(0);
    }
    start_new_book();
    for (i = 1; i < argc; i++)
    {
        if (argv[i][0] == '@') 
        {
            FILE * f;
            trouble++;
            f = fopen(&argv[i][1], "r");
            if (f) {
                trouble = 0;
                while (NULL != fgets(linebuf,sizeof(linebuf), f))
                {
                    int k, j = 0;
                    while (linebuf[j] != '\0') 
                    {
                        while (isspace(linebuf[j]) && linebuf[j]) j++;
                        k = j;
                        while (linebuf[k] && (linebuf[k] != '\n')) k++;
                        if (j != k) 
                        {
                            if (!linebuf[k]) linebuf[k+1] = '\0';
                            linebuf[k] = '\0';
                            trouble = process_one_arg(&linebuf[j]);
                        }
                        break; /* One command per line */
                        /*j = k + 1;*/
                    }
                }
                fclose(f);
            } 
        }
        else trouble = process_one_arg(argv[i]);
        if (trouble) 
        {
            fprintf(stderr,"Error parsing switch (%d) \"%s\" \n", i, argv[i]);  
            exit(-1);
        }
    }
    if (!files_processed || !outputfile) 
    {
        fprintf(stderr,
"Cannot create book. You must provide an output file and some input files.\n");
        exit(-1);
    }
    trouble = provide_metadata(
        infofile?infofile:"info.xml", 
        imagefile?imagefile:"image.gif",
        bookid);
    if (!trouble) {
        if (title) update_metadata("Title", title);
        if (author) update_metadata("Author", author);
        if (description) update_metadata("FreeText", description);
        trouble = write_book_to_file(outputfile);
        if (trouble) {
            fprintf(stderr,"Writing to \"%s\" failed.\n", outputfile);
        }
    }

    if (author) { free(author); }
    if (bookid) { free(bookid); }
    if (description) { free(description); }
    if (title) { free(title); }


    if (infofile) { free(infofile); }
    if (imagefile) { free(imagefile); }
    if (outputfile) { free(outputfile); }
    end_book();
    exit(trouble);
}
