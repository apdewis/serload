
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
//#include <termios.h>
#include <stdio.h>
//#include <unistd.h>
//#include <strings.h>
#include <stdlib.h>
#include <stdint.h>
#include <argparse.h>
#include <c_serial.h>

#include "commands.h"
  
#define BAUDRATE 921600
#define _POSIX_SOURCE 1 /* POSIX compliant source */
#define DEFAULT_BASE 0x40000000
#define BLOCK_SIZE 65536

uint32_t base = DEFAULT_BASE;
int serdev;
uint8_t buf[BLOCK_SIZE];
int fd, c, res;
FILE *fd_data;
int status;
const char *port = NULL;
const char *file = NULL;
uint8_t *run = NULL;
struct stat st;
uint8_t *file_data;
uint8_t revert_tc = 0;
int32_t data_length = 0;
c_serial_port_t* m_port;
c_serial_control_lines_t m_lines;

static const char *const usage[] = 
{
    "serload [options]",
    NULL,
};

void terminate(int code)
{
    if(file_data)
    {
        free(file_data);
    }
    exit(code);
}

int8_t cmd_send(uint8_t *buf, uint32_t len)
{
    int32_t res;
    data_length = len;
    res = c_serial_write_data(m_port, buf, &data_length);
    if(res < 0) return -1;

    data_length = 1;
    do 
    {
        res = c_serial_read_data(m_port, buf, &data_length, &m_lines);
    } while(res = 0);

    if(buf[0] == ACK)
    {
        return 0;
    }
    else
    {
        return -1;
    }
}

int8_t cmd_recv(uint8_t *buf, uint32_t len)
{
    int32_t res;
    data_length = 1;
    for(uint8_t count = 0; count < len; count++)
    {
        res = c_serial_read_data(m_port, &buf[count], &data_length, &m_lines );
        //res = read(serdev,&buf[count],1);
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

int process_args(int argc, const char **argv)
{
    struct argparse_option options[] = {
        OPT_HELP(),
        OPT_GROUP("Basic options"),
        OPT_STRING('f', "file", &file, "path to input file"),
        OPT_STRING('p', "port", &port, "serial device"),
        OPT_INTEGER('b', "base", &base, "base address"),
        OPT_BOOLEAN('r', "run", &run, "run on completion"),
        OPT_END()
    };

    struct argparse argparse;
    argparse_init(&argparse, options, usage, 0);
    argparse_describe(&argparse, "\nA serial program loader for use with Queball's BROM.", "\n");
    argc = argparse_parse(&argparse, argc, argv);

    if(file == NULL)
    {
        argparse_usage(&argparse);
        printf("filename required\n");
        return -1;
    }

    if(port == NULL)
    {
        argparse_usage(&argparse);
        printf("serial device required\n");
        return -1;
    }

    return 0;
}

void init()
{
    //serdev = open(port, O_RDWR | O_NOCTTY  ); 
    //if (serdev <0) {perror(port); exit(-1); }
    
    fd_data = fopen(file, "rb");
    if (fd_data == NULL) {perror("FILE"); exit(-1); }

    if (stat(file, &st) != 0) {perror("ST"); exit(-1); }

    //tcgetattr(fd,&oldtio); /* save current port settings */
    //
    //bzero(&newtio, sizeof(newtio));
    //newtio.c_cflag = BAUDRATE | CS8 | CLOCAL | CREAD;
    //newtio.c_iflag = IGNPAR;
    //newtio.c_oflag = 0;
    //
    ///* set input mode (non-canonical, no echo,...) */
    //newtio.c_lflag = 0;
    //
    //newtio.c_cc[VTIME]    = 0;   /* inter-character timer unused */
    //newtio.c_cc[VMIN]     = 1;   /* blocking read until 5 chars received */
    //
    //tcflush(fd, TCIFLUSH);
    //tcsetattr(fd,TCSANOW,&newtio);
    //revert_tc = 1;

    if( c_serial_new( &m_port, NULL ) < 0 ){
        fprintf( stderr, "ERROR: Unable to create new serial port\n" );
        exit(-1);
    }

    if( c_serial_set_port_name(m_port, port) < 0 ) {
        fprintf( stderr, "ERROR: can't set port name\n" );
    }

    c_serial_set_baud_rate(m_port, BAUDRATE);
    c_serial_set_data_bits(m_port, CSERIAL_BITS_8);
    c_serial_set_stop_bits(m_port, CSERIAL_STOP_BITS_1);
    c_serial_set_parity(m_port, CSERIAL_PARITY_NONE);
    c_serial_set_flow_control(m_port, CSERIAL_FLOW_NONE);

    c_serial_set_serial_line_change_flags( m_port, CSERIAL_LINE_FLAG_ALL );

    status = c_serial_open( m_port );
    if( status < 0 ){
        fprintf( stderr, "ERROR: Can't open serial port\n" );
        exit(-1);
    }

    printf("opened port \n\r");
}

int send_block(uint8_t *data, uint32_t dest_addr, uint32_t len)
{
    uint8_t send_buf[4];
    int count = 0;
    send_buf[0] = CMD_WRITE;

    if (cmd_send(send_buf, 1) != 0) 
    {
        printf("invalid response\n\r");
        return -1;
    }
    
    wordToBytes(send_buf, dest_addr);
    if (cmd_send(send_buf, 4) != 0) 
    {
        printf("invalid response\n\r");
        return -1;
    }

    wordToBytes(send_buf, len);
    if (cmd_send(send_buf, 4) != 0)
    {
        printf("invalid response\n\r");
        return -1;
    }

    if(cmd_send(data, len) != 0)
    {
        uint8_t err_buf[9];
        printf("Send error\n");
        cmd_recv(err_buf, 9);
        uint8_t exc = err_buf[0];
        uint32_t epc = err_buf[1];
        uint32_t mtval = err_buf[4];
        printf("err: %x %x", exc, epc);
        return -1;
    }

    return 0;
}

int main(int argc, const char **argv)
{   
    uint8_t tmp;
    uint32_t data_offset;

    if(process_args(argc, argv) != 0)
    {
        terminate(-1);
    }    
    init();

    file_data = malloc(st.st_size);
    if(fread(file_data, 1, st.st_size, fd_data) != st.st_size)
    {
        fputs ("File read error\n\r",stderr); 
        terminate(-1);
    }
    
    printf("Waiting for device\n\r");
    buf[0] = CMD_CHKRDY;
    if (cmd_send(buf, 1) != 0)
    { 
        printf("invalid response\n\r");
        terminate(-1);
    }
    
    printf("sending data: \n\r");
    for(data_offset = 0; data_offset < st.st_size; data_offset += BLOCK_SIZE)
    {
        uint32_t send_size = st.st_size - data_offset;
        if(send_size > BLOCK_SIZE) send_size = BLOCK_SIZE;

        tmp = send_block(&file_data[data_offset], base + data_offset, send_size);
        if(tmp != 0) terminate(tmp);
        if(st.st_size - data_offset < BLOCK_SIZE)
        {
            printf("Sent (%d) \n\r", st.st_size);
        }
        else 
        {
            printf("sent (%d of %d) \r", data_offset, st.st_size);
        }
        fflush(stdout);
    }

    if(run)
    {
        printf("Running program \n\r");
        buf[0] = CMD_JUMP;
        if (cmd_send(buf, 1) != 0) 
        {
            printf("invalid response\n");
            terminate(-1);
        }
    
        wordToBytes(buf, base);
        if (cmd_send(buf, 4) != 0) 
        {
            printf("invalid response\n");
            terminate(-1);
        }
    }

    terminate(0);
}
