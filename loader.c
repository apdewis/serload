
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <stdio.h>
#include <unistd.h>
#include <strings.h>
#include <stdlib.h>
#include <stdint.h>
#include <argparse.h>

#include "commands.h"
  
#define BAUDRATE B9600
#define _POSIX_SOURCE 1 /* POSIX compliant source */

uint32_t base = 0x10000000;
int serdev;
uint8_t buf[256];
int fd, c, res;
FILE *fd_data;
struct termios oldtio,newtio;
const char *port = NULL;
const char *file = NULL;
struct stat st;
uint8_t *file_data;

static const char *const usage[] = 
{
    "serload [options] [[--] args]",
    "serload [options]",
    NULL,
};

void terminate(int code)
{
    tcsetattr(fd,TCSANOW,&oldtio);
    if(file_data)
    {
        free(file_data);
    }
    exit(code);
}

int8_t send(uint8_t *buf, uint32_t len)
{
    int32_t res;
    res = write(serdev,buf,len);
    res = read(serdev,buf,1);
    if(buf[0] == ACK)
    {
        return 0;
    }
    else
    {
        return -1;
    }
}

int8_t recv(uint8_t *buf, uint32_t len)
{
    int32_t res;
    for(uint8_t count = 0; count < len; count++)
    {
        res = read(serdev,&buf[count],1);
    }

    return 0;
}

void wordToBytes(uint8_t *buf, uint32_t word)
{
    buf[0] = (uint8_t)(word);
    buf[1] = (uint8_t)(word >> 8);
    buf[2] = (uint8_t)(word >> 16);
    buf[3] = (uint8_t)(word >> 24);
}

void process_args(int argc, const char **argv)
{
    struct argparse_option options[] = {
        OPT_GROUP("Basic options"),
        OPT_STRING('f', "file", &file, "path to input file"),
        OPT_STRING('p', "port", &port, "serial device"),
        OPT_END()
    };

    struct argparse argparse;
    argparse_init(&argparse, options, usage, 0);
    argparse_describe(&argparse, "\nA serial program loader for use with Queball's BROM.", "\n");
    argc = argparse_parse(&argparse, argc, argv);

    if(file == NULL)
    {
        printf("filename required\n");
        terminate(-1);
    }

    if(port == NULL)
    {
        printf("serial device required\n");
        terminate(-1);
    }
}

void init()
{
    serdev = open(port, O_RDWR | O_NOCTTY  ); 
    if (serdev <0) {perror(port); exit(-1); }
    printf("Opened %s\n", port);
    
    fd_data = fopen(file, "rb");
    if (fd_data == NULL) {perror("FILE"); exit(-1); }
    printf("Opened %s\n", file);

    if (stat(file, &st) != 0) {perror("ST"); exit(-1); }

    tcgetattr(fd,&oldtio); /* save current port settings */
    
    bzero(&newtio, sizeof(newtio));
    newtio.c_cflag = BAUDRATE | CS8 | CLOCAL | CREAD;
    newtio.c_iflag = IGNPAR;
    newtio.c_oflag = 0;
    
    /* set input mode (non-canonical, no echo,...) */
    newtio.c_lflag = 0;
    
    newtio.c_cc[VTIME]    = 0;   /* inter-character timer unused */
    newtio.c_cc[VMIN]     = 1;   /* blocking read until 5 chars received */
    
    tcflush(fd, TCIFLUSH);
    tcsetattr(fd,TCSANOW,&newtio);
}

int main(int argc, const char **argv)
{   
    process_args(argc, argv);
    printf("Initialising\n");
    printf("Opening %s\n", file);
    
    init();
    
    printf("Waiting for device\n");
    buf[0] = CMD_CHKRDY;
    if (send(buf, 1) == 0)
    {
        printf("device ready, sending data\n");
    }
    else
    { 
        printf("readback: %x\n", buf[0]);
        terminate(-1);
    }
    
    buf[0] = CMD_WRITE;
    if (send(buf, 1) == 0) 
    {
        printf("write command ack\n");
    }
    else
    {
        printf("readback: %x\n", buf[0]);
        terminate(-1);
    }
    
    wordToBytes(buf, base);
    printf("%x \n", buf[3]);
    if (send(buf, 4) == 0) 
    {
        printf("base adr ack\n");
    }
    else
    {
        printf("readback: %x\n", buf[0]);
        terminate(-1);
    }

    wordToBytes(buf, st.st_size);
    if (send(buf, 4) == 0) 
    {
        printf("size ack\n");
    }
    else
    {
        printf("readback: %x\n", buf[0]);
        terminate(-1);
    }

    file_data = malloc(st.st_size);
    if(fread(file_data, 1, st.st_size, fd_data) != st.st_size)
    {
        fputs ("Reading error\n",stderr); 
        terminate(-1);
    }

    if(send(file_data, st.st_size) != 0)
    {
        printf("Send error\n");
        terminate(-1);
    } else {
        printf("ack data \n");
    }

    buf[0] = CMD_JUMP;
    if (send(buf, 1) == 0) 
    {
        printf("jump command ack\n");
    }
    else
    {
        printf("readback: %x\n", buf[0]);
        terminate(-1);
    }

    wordToBytes(buf, base);
    if (send(buf, 4) == 0) 
    {
        printf("jump adr ack\n");
    }
    else
    {
        printf("readback: %x\n", buf[0]);
        terminate(-1);
    }

    terminate(0);
}
