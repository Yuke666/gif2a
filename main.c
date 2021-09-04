#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <ncurses.h>
#include <time.h>
#include <unistd.h>

#define MAX_CODES_IN_TABLE 400
#define MAX_CODE_TABLE_SIZE 4095
#define MAX_FRAMES 1000

enum {
    RED = 1,
    GREEN,
    YELLOW,
    BLUE,
    MAGENTA,
    CYAN,
    WHITE
};

typedef struct {
    unsigned char r;
    unsigned char g;
    unsigned char b;
} RGB;

typedef struct {
    unsigned short x;
    unsigned short y;
    unsigned short w;
    unsigned short h;
    unsigned char transparent;
    unsigned char tColor;
    unsigned short delayTime;
    RGB tColorRGB;
    char *localColorTable;
    char *pixels;
} Frame;

typedef struct {
    unsigned short w;
    unsigned short h;
    unsigned char screenDescripter;
    unsigned char bgColor;
    unsigned char aspectRatio;
    char *colorTable;
    int colorTableSize;
    int colorRes;
    Frame frames[MAX_FRAMES];
    int framesIndex;
} Image;

int codeTable[MAX_CODE_TABLE_SIZE][MAX_CODES_IN_TABLE];
char setTableIndecies[MAX_CODE_TABLE_SIZE];

int UncompressGif(unsigned char *data, int len, int *uncompressedData, int clearCode, int endCode, int *numOfColors, int codeLen){

    int tableIndex = 0;

    memset(codeTable, -1, sizeof(codeTable));
    memset(setTableIndecies, 0, sizeof(setTableIndecies));

    int startCodeLen = codeLen;
    int index = 0;
    int lastCode;
    unsigned int mask = 0x01;

    int k;
    for(k = 0; k < len;){

        int code = 0x00;
        int f;
        for(f = 0; f < codeLen+1; f++){
            int bit = ((data[k] & 0XFF) & mask) ? 1 : 0;
            mask <<= 1;
            if(mask == 0x100){
                mask = 0x01;
                k++;
            }
            code = code | (bit << f);
        }

        if(*numOfColors == 0) {
            *numOfColors = code;
            memset(codeTable, -1, sizeof(codeTable));
            memset(setTableIndecies, 0, sizeof(setTableIndecies));
            tableIndex= 0;
            int f;
            for(f = 0; f <= *numOfColors; f++){
                codeTable[f][0] = f;
                setTableIndecies[tableIndex++] = 1;
            }
            continue; 
        }

        if(code == endCode) break;
        if(code == clearCode) {
            codeLen = startCodeLen;
            memset(codeTable, -1, sizeof(codeTable));
            memset(setTableIndecies, 0, sizeof(setTableIndecies));
            tableIndex= 0;
            
            int f;
            for(f = 0; f <= *numOfColors; f++){
                codeTable[f][0] = f;
                setTableIndecies[tableIndex++] = 1;
            }
            continue;
        }

        if(code > 4095) continue;

        if(setTableIndecies[code]){

            int l = codeTable[code][0];
            
            int j;
            for(j = 0; codeTable[code][j] > -1 && j < MAX_CODES_IN_TABLE; j++)
                uncompressedData[index++] = codeTable[code][j];

            for(j = 0; codeTable[lastCode][j] > -1 && j < MAX_CODES_IN_TABLE-1; j++)
                codeTable[tableIndex][j] = codeTable[lastCode][j];

            codeTable[tableIndex][j] = l;
            setTableIndecies[tableIndex] = 1;

            if(tableIndex+1 < MAX_CODE_TABLE_SIZE) tableIndex++;
        }
         else {
            int l = codeTable[lastCode][0];
            int m;
            for(m = 0; codeTable[lastCode][m] > -1 && m < MAX_CODES_IN_TABLE-1; m++){
                codeTable[tableIndex][m] = codeTable[lastCode][m];
                uncompressedData[index++] = codeTable[lastCode][m];
            }
            codeTable[tableIndex][m] = l;
            uncompressedData[index++] = l;
            setTableIndecies[tableIndex] = 1;
            if(tableIndex+1 < MAX_CODE_TABLE_SIZE) tableIndex++;
        }

        lastCode = code;

        if(tableIndex == ( 1 << ( codeLen + 1 )) && codeLen < 11){
            codeLen++;
        }
    }

    return index;
}

void SkipBytes(FILE *fp, int len){
    fseek(fp, ftell(fp)+len, SEEK_SET);
}

int ReadExt(FILE *fp, Image *img){
     if(fgetc(fp) != 0xF9) return 0;
     int len = fgetc(fp);
    if(len > 4){
        SkipBytes(fp, len);         
        len = fgetc(fp);
        SkipBytes(fp, len);
    } else {
        Frame *frame = &img->frames[img->framesIndex];
        frame->transparent = fgetc(fp) & 0x01;
        fread(&frame->delayTime, 1, 2, fp);
        if(frame->delayTime == 0) frame->delayTime = 10;
        frame->tColor = fgetc(fp);
    }
    fgetc(fp); //skip end of GCE block
    return 1;
}

void NewFrame(FILE *fp, Image *img){

    Frame *frame = &img->frames[img->framesIndex++];
    fread(frame, 1, 8, fp);

    frame->pixels = (char *)malloc(frame->w*frame->h*3*sizeof(char));
    int lcT = fgetc(fp);

    if(lcT & 0x80){ // Local color table flag set LCT follows
        int lcTSize = lcT & 0x07;
        int m = pow( 2, lcTSize+1);
        frame->localColorTable = (char *)malloc(m*3*sizeof(char));
        fread(frame->localColorTable, 1, m*3, fp);
    }

    int LZWminSize = fgetc(fp);
    if(LZWminSize > 8 || LZWminSize < 2) return;

    int clearCode   = pow(2,LZWminSize);
    int endCode     = clearCode+1;
    int pixelIndex  = 0;
    int numOfColors = 0;

    int dataLen = 0;
    int dataIndex = 0;
    unsigned char *data = NULL;
    
    while(1){
        int len = fgetc(fp);
        if(len == 0x00) break;
        dataLen += len;
        data = (unsigned char *)realloc(data,dataLen*sizeof(unsigned char));
        fread(&data[dataIndex],1,len,fp);
        dataIndex += len;
    }

    int *uncompressedData = (int *)malloc(frame->w*frame->h*sizeof(int));
    int index = UncompressGif(data, dataLen, uncompressedData, clearCode, endCode, &numOfColors, LZWminSize);
    
    if(frame->transparent){
        if(!frame->localColorTable){
            frame->tColorRGB.r = (img->colorTable[(frame->tColor*3)])   & 0xFF;
            frame->tColorRGB.g = (img->colorTable[(frame->tColor*3)+1]) & 0xFF;
            frame->tColorRGB.b = (img->colorTable[(frame->tColor*3)+2]) & 0xFF;
        } else {
            frame->tColorRGB.r = (frame->localColorTable[(frame->tColor*3)])   & 0xFF;
            frame->tColorRGB.g = (frame->localColorTable[(frame->tColor*3)+1]) & 0xFF;
            frame->tColorRGB.b = (frame->localColorTable[(frame->tColor*3)+2]) & 0xFF;
        }
    }
    int k;
    for(k = 0; k < index; k++){
        if(!frame->localColorTable){
            frame->pixels[(pixelIndex*3)  ] = (img->colorTable[(uncompressedData[k]*3)])   & 0xFF;
            frame->pixels[(pixelIndex*3)+1] = (img->colorTable[(uncompressedData[k]*3)+1]) & 0xFF;
            frame->pixels[(pixelIndex*3)+2] = (img->colorTable[(uncompressedData[k]*3)+2]) & 0xFF;
        } else {
            frame->pixels[(pixelIndex*3)  ] = (frame->localColorTable[(uncompressedData[k]*3)])   & 0xFF;
            frame->pixels[(pixelIndex*3)+1] = (frame->localColorTable[(uncompressedData[k]*3)+1]) & 0xFF;
            frame->pixels[(pixelIndex*3)+2] = (frame->localColorTable[(uncompressedData[k]*3)+2]) & 0xFF;
        }
        pixelIndex++;
    }
    free(data);
    free(uncompressedData);
    free(frame->localColorTable);
    frame->localColorTable = NULL;
    return;
}

int LoadGIF(char *path, Image *img){

    FILE *fp = fopen(path,"rb");
    
    if( fp == NULL ){
        printf("Error loading GIF %s: No such file.\n", path);
        return 0;
    }

    char header[6];
    fread(header, 1, 6, fp);
    if(strcmp(header, "GIF89a") != 0 && strcmp(header, "GIF87a") != 0){
        printf("Error loading GIF %s: No valid GIF header.\n", path);
        fclose(fp);
        return 0;
    }

    fread(img, 1, 7, fp);

    if(img->screenDescripter & 0x80){ // Global color table flag set GCT follows
        int size = img->screenDescripter & 0x07; // Size of the global color table
        img->colorTableSize = pow(2, size+1);
        img->colorTable = (char *)malloc(img->colorTableSize*3*sizeof(char));
        fread(img->colorTable, 1, img->colorTableSize*3, fp);
    }

    while(1){
        if(feof(fp)) break;
        char b = fgetc(fp);
        switch(b){
            case 0x21:
                ReadExt(fp, img); // Graphic control extension byte
                break;
            case 0x2C: // Image Descriptor byte
                NewFrame(fp, img);
                break;
        }
    }

    free(img->colorTable);
    img->colorTable = NULL;
    fclose(fp);
    return 1;
}

void FreeImage(Image *img){
    if(img->colorTable) free(img->colorTable);
    int k;
    for(k = 0; k < MAX_FRAMES; k++){
        if(img->frames[k].localColorTable) free(img->frames[k].localColorTable);
        if(img->frames[k].pixels != NULL) free(img->frames[k].pixels);
    }
}

int lastCols, lastRows;

void DrawFrame(Frame *frame, Image img, char *pixels, char useColor, char *characters){
    
    int cols, rows;
    getmaxyx(stdscr, rows, cols);

    if(cols != lastCols || rows != lastRows) clear();
    lastCols = cols;
    lastRows = rows;

    int xPlus = 1;
    int yPlus = 1;
    
    xPlus = ceil((float)img.w / (float)cols);
    yPlus = xPlus;

    int x, y;
    for(y = 1; y < img.h && y/(yPlus*2) < rows; y+=yPlus*2){
        for(x = 0; x < img.w && x/xPlus < cols-1; x+=xPlus){

            int r = pixels[(((y*img.w) + x)*3)  ] & 0xFF;
            int g = pixels[(((y*img.w) + x)*3)+1] & 0xFF;
            int b = pixels[(((y*img.w) + x)*3)+2] & 0xFF;

            if((x-frame->x) < frame->w && x > frame->x && (y-frame->y) < frame->h && y > frame->y){
                r = frame->pixels[((((y-frame->y)*frame->w) + (x-frame->x))*3)  ] & 0xFF;
                g = frame->pixels[((((y-frame->y)*frame->w) + (x-frame->x))*3)+1] & 0xFF;
                b = frame->pixels[((((y-frame->y)*frame->w) + (x-frame->x))*3)+2] & 0xFF;
    
                if(r != frame->tColorRGB.r || g != frame->tColorRGB.g || b != frame->tColorRGB.b){
                    pixels[(((y*img.w) + x)*3)  ] = r & 0xFF;
                    pixels[(((y*img.w) + x)*3)+1] = g & 0xFF;
                    pixels[(((y*img.w) + x)*3)+2] = b & 0xFF;
                } else if(frame->transparent) {
                    r = pixels[(((y*img.w) + x)*3)  ] & 0xFF;
                    g = pixels[(((y*img.w) + x)*3)+1] & 0xFF;
                    b = pixels[(((y*img.w) + x)*3)+2] & 0xFF;
                }
            }

            int color = 7;
            if(useColor){
                if( round(r/20)*20 >  round(g/20)*20 && round(r/20)*20 >  round(b/20)*20) color = 1;
                if( round(g/20)*20 >  round(r/20)*20 && round(g/20)*20 >  round(b/20)*20) color = 2;
                if( round(b/20)*20 >  round(r/20)*20 && round(b/20)*20 >  round(g/20)*20) color = 6;
                if( round(r/20)*20 == round(b/20)*20 && round(r/20)*20 >  floor(g/20)*20) color = 5;
                if( round(r/20)*20 == round(g/20)*20 && round(g/20)*20 >  floor(b/20)*20) color = 3;
                if( round(g/10)*10 == round(r/10)*10 && round(r/10)*10 == floor(b/10)*10) color = 7;
            }
            attron(COLOR_PAIR(color));
            int gray = (0.21*r + 0.72*g + 0.07*b);

            if(gray > 240)      printw("%c", characters[25]);
            else if(gray > 230) printw("%c", characters[24]);
            else if(gray > 220) printw("%c", characters[23]);
            else if(gray > 210) printw("%c", characters[22]);
            else if(gray > 200) printw("%c", characters[21]);
            else if(gray > 190) printw("%c", characters[20]);
            else if(gray > 180) printw("%c", characters[19]);
            else if(gray > 170) printw("%c", characters[18]);
            else if(gray > 160) printw("%c", characters[17]);
            else if(gray > 150) printw("%c", characters[16]);
            else if(gray > 140) printw("%c", characters[15]);
            else if(gray > 130) printw("%c", characters[14]);
            else if(gray > 120) printw("%c", characters[13]);
            else if(gray > 110) printw("%c", characters[12]);
            else if(gray > 100) printw("%c", characters[11]);
            else if(gray > 90)  printw("%c", characters[10]);
            else if(gray > 80)  printw("%c", characters[9]);
            else if(gray > 70)  printw("%c", characters[8]);
            else if(gray > 60)  printw("%c", characters[7]);
            else if(gray > 50)  printw("%c", characters[6]);
            else if(gray > 40)  printw("%c", characters[5]);
            else if(gray > 30)  printw("%c", characters[4]);
            else if(gray > 20)  printw("%c", characters[3]);
            else if(gray > 10)  printw("%c", characters[2]);
            else if(gray > 0)   printw("%c", characters[1]);
            else if(gray == 0)  printw("%c", characters[0]);
            attroff(COLOR_PAIR(color));
        }
        printw("\n");
    }

    printw("\n");
}

int main(int argc, char **argv){
    
    char useColor = 0;
    int speed = -1;

    char *characters = " ..,;accceeexxxddeeCCXXXWWW";

    int k;
    for(k = 0; argv[k]; k++){

        if(strcmp(argv[k], "--help") == 0){

            printf(
                "gif2a 0.7\n"
                "\n"
                "Usage: gif2a [ file ] [ options ]\n"
                "Convert an animated GIF to ASCII.\n"
                "\n"
                "OPTIONS\n"
                " --colors            Use colors.\n"
                " --speed             Specify play speed in thousandths of a second.\n"
                " --characters \"\"     Specify characters used in the next argument.\n"
                "                     Example: gif2a gif.gif --characters  \" ..,;accceeexxxddeeCCXXXWWW\"\n"
                "                     Must specify at least 26 characters, only the first 26 will be used.\n"
                "\n"
                "Report bugs to <yukizini@yukiz.in>\n"
            );
            return 0;

        }
        
        if(strcmp(argv[k], "--colors") == 0){
            useColor = 1;
            continue;
        }

        if(strcmp(argv[k], "--speed") == 0){
            if(!argv[k+1]){
                printf("--speed option set and you didn't enter a number.\n");
                return 0;
            }
            int m;
            for(m = 0; m < strlen(argv[k+1]); m++){
                if((int)argv[k+1][m] < 48 || (int)argv[k+1][m] > 57){
                    printf("%i, %c\n", (int)argv[k+1][m], argv[k+1][m]);
                    printf("--speed option set and you didn't enter a number.\n");
                    return 0;
                }
            }
            speed = atoi(argv[k+1]);
            k++;
            continue;
        }
        
        if(strcmp(argv[k], "--characters") == 0){
            if(!argv[k+1] || strlen(argv[k+1]) < 27){
                printf("--characters option set and you entered less than 26 characters.\n");
                return 0;
            }
            characters = argv[k+1];
            k++;
            continue;
        }
    }

    Image img = {};
    if(!LoadGIF(argv[1], &img)) return 0;

    if(img.framesIndex == 1){
        printf("Image must be an animated GIF.\n");
        FreeImage(&img);
        return 0;
    }
    
    initscr();
    curs_set(0);
    cbreak();
    noecho();
    keypad(stdscr, TRUE);

    if(useColor){
        if(has_colors() == FALSE){
            printf("--colors option set and your terminal does not support colors. \n");
            endwin();p
            FreeImage(&img);
            return 0;
        }
        start_color();
        use_default_colors();
        init_pair(RED, COLOR_RED, -1);
        init_pair(GREEN, COLOR_GREEN, -1);
        init_pair(YELLOW, COLOR_YELLOW, -1);
        init_pair(BLUE, COLOR_BLUE, -1);
        init_pair(MAGENTA, COLOR_MAGENTA, -1);
        init_pair(CYAN, COLOR_CYAN, -1);
        init_pair(WHITE, -1, -1);
    }

    int frameIndex = 0;

    char pixels[img.w*img.h*3];
    memset(pixels, 0x00, sizeof(pixels));

    while(1){

        if(speed == -1)
            usleep((float)img.frames[frameIndex].delayTime*10000);
        else
            usleep((float)speed*1000);


        Frame *frame = &img.frames[frameIndex];

        move(0,0);
        DrawFrame(frame, img, pixels, useColor, characters);
        refresh();

        frameIndex++;
        if(frameIndex == img.framesIndex){
            frameIndex = 0;
            memset(pixels, 0x00, sizeof(pixels));
        }
    }


    FreeImage(&img);
    endwin();
    return 0;
}