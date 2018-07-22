
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <stdio.h>
#include <unistd.h>
#include <strings.h>
#include <stdlib.h>
#include <stdint.h>

#include "commands.h"
  
#define BAUDRATE B9600
#define MODEMDEVICE "/dev/ttyS7"
#define _POSIX_SOURCE 1 /* POSIX compliant source */
#define FALSE 0
#define TRUE 1
  
volatile int STOP=FALSE; 
uint32_t base = 0x10000000;
int serdev;
uint8_t buf[256];


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

int main(int argc, char* argv[])
{
    printf("Initialising\n");
    printf("Opening %s\n", argv[1]);

    int fd, c, res;
    FILE *fd_data;
    struct termios oldtio,newtio;
    
    struct stat st;
    
    serdev = open(MODEMDEVICE, O_RDWR | O_NOCTTY  ); 
    if (serdev <0) {perror(MODEMDEVICE); exit(-1); }
    printf("Opened %s\n", MODEMDEVICE);
    
    fd_data = fopen(argv[1], "rb");
    if (fd_data == NULL) {perror("FILE"); exit(-1); }
    printf("Opened %s\n", argv[1]);

    if (stat(argv[1], &st) != 0) {perror("ST"); exit(-1); }

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
    
    printf("Waiting for device\n");
    buf[0] = 0x0;
    if (send(buf, 1) == 0)
    {
        printf("device ready, sending data\n");
    }
    else
    { 
        printf("readback: %x\n", buf[0]);
        exit(-1);
    }
    
    buf[0] = 0x01;
    if (send(buf, 1) == 0) 
    {
        printf("write command ack\n");
    }
    else
    {
        printf("readback: %x\n", buf[0]);
        exit(-1);
    }
    
    buf[0] = (uint8_t)(base);
    buf[1] = (uint8_t)(base >> 8);
    buf[2] = (uint8_t)(base >> 16);
    buf[3] = (uint8_t)(base >> 24);
    printf("%x \n", buf[3]);
    if (send(buf, 4) == 0) 
    {
        printf("base adr ack\n");
    }
    else
    {
        printf("readback: %x\n", buf[0]);
        exit(-1);
    }

    buf[0] = (uint8_t)(st.st_size);
    buf[1] = (uint8_t)(st.st_size >> 8);
    buf[2] = (uint8_t)(st.st_size >> 16);
    buf[3] = (uint8_t)(st.st_size >> 24);
    if (send(buf, 4) == 0) 
    {
        printf("size ack\n");
    }
    else
    {
        printf("readback: %x\n", buf[0]);
        exit(-1);
    }

    uint8_t *file_data = malloc(st.st_size);
    if(fread(file_data, 1, st.st_size, fd_data) != st.st_size)
    {
        fputs ("Reading error\n",stderr); 
        exit (3);
    }

    if(send(file_data, st.st_size) != 0)
    {
        printf("Send error\n");
        exit(-1);
    } else {
        printf("ack data \n");
       
    }

    buf[0] = 0x04;
    if (send(buf, 1) == 0) 
    {
        printf("jump command ack\n");
    }
    else
    {
        printf("readback: %x\n", buf[0]);
        exit(-1);
    }

    buf[0] = (uint8_t)(base);
    buf[1] = (uint8_t)(base >> 8);
    buf[2] = (uint8_t)(base >> 16);
    buf[3] = (uint8_t)(base >> 24);
    if (send(buf, 4) == 0) 
    {
        printf("jump adr ack\n");
    }
    else
    {
        printf("readback: %x\n", buf[0]);
        exit(-1);
    }

    tcsetattr(fd,TCSANOW,&oldtio);
    if(file_data)
    {
        free(file_data);
    }
}
