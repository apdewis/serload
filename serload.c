
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <stdio.h>
#include <unistd.h>
#include <strings.h>
#include <stdlib.h>
#include <stdint.h>
#include <limits.h>
#include "argparse.h"

#include <libserialport.h>

#include "commands.h"
  
#define BAUDRATE B115200
#define _POSIX_SOURCE 1 /* POSIX compliant source */
#define DEFAULT_BASE 0x40000000
#define BLOCK_SIZE 512
#define BLOCK_ATTEMPTS 50

/* send() return codes */
#define SEND_OK    0
#define SEND_ERR  -1   /* transport failure */
#define SEND_NACK -2   /* far end NACKed: parity error, retry the block */

/* recv() return codes */
#define RECV_OK      0
#define RECV_ERR    -1   /* transport failure */
#define RECV_PARITY -2   /* parity error on an inbound byte, retry the fetch */

/* read_block()/verify_block() return codes */
#define READ_OK      0
#define READ_ERR    -1   /* transport failure or verify mismatch */
#define READ_PARITY -2   /* parity error reading the block back, retry the fetch */

uint32_t base = DEFAULT_BASE;
int serdev;
uint8_t buf[BLOCK_SIZE];
int fd, c, res;
FILE *fd_data;
struct termios oldtio,newtio;
const char *port = NULL;
const char *file = NULL;
uint8_t *run = NULL;
struct stat st;
uint8_t *file_data;
uint8_t revert_tc = 0;

struct sp_port *serial_port;
int check(enum sp_return result);
int8_t recv(uint8_t *rsp, uint32_t len);

static const char *const usage[] = 
{
    "serload [options]",
    NULL,
};

void terminate(int code)
{
    if(revert_tc) tcsetattr(fd,TCSANOW,&oldtio);
    if(file_data)
    {
        free(file_data);
    }
    exit(code);
}

int8_t send(uint8_t *buf, uint32_t len)
{
    uint8_t rsp = 0;
    int result;

    result = sp_blocking_write(serial_port, buf, len, UINT_MAX);
    if (result < 0) { check(result); return SEND_ERR; }
    if ((uint32_t)result != len) { printf("Short write\n"); return SEND_ERR; }

    /* Read the status byte through the de-framer; a parity error on the
     * status byte itself is unrecoverable here, so fail the block and retry. */
    if (recv(&rsp, 1) != RECV_OK) { return SEND_ERR; }

    if(rsp == ACK)
    {
        return SEND_OK;
    }
    else if(rsp == NACK)
    {
        return SEND_NACK;
    }
    else
    {
        return SEND_ERR;
    }
}

/* Read exactly len de-framed payload bytes into rsp, decoding the PARMRK
 * escaping set up in init(). Returns RECV_OK on success, RECV_PARITY if any
 * inbound byte had a parity error, RECV_ERR on a transport failure. */
int8_t recv(uint8_t *rsp, uint32_t len)
{
    uint32_t got = 0;
    int result;
    uint8_t b;

    while (got < len)
    {
        result = sp_blocking_read(serial_port, &b, 1, UINT_MAX);
        if (result < 0) { check(result); return RECV_ERR; }
        if (result != 1) { printf("Read failed\n"); return RECV_ERR; }

        if (b != 0xFF)
        {
            /* ordinary byte */
            rsp[got++] = b;
            continue;
        }

        /* 0xFF is the PARMRK escape lead-in; read the next byte to classify */
        result = sp_blocking_read(serial_port, &b, 1, UINT_MAX);
        if (result < 0) { check(result); return RECV_ERR; }
        if (result != 1) { printf("Read failed\n"); return RECV_ERR; }

        if (b == 0xFF)
        {
            /* \377 \377 -> a literal 0xFF data byte */
            rsp[got++] = 0xFF;
        }
        else if (b == 0x00)
        {
            /* \377 \0 <byte> -> parity error on <byte>; consume it and bail */
            result = sp_blocking_read(serial_port, &b, 1, UINT_MAX);
            if (result < 0) { check(result); return RECV_ERR; }
            return RECV_PARITY;
        }
        else
        {
            /* not a framing sequence we expect */
            printf("Unexpected framing byte 0x%02x\n\r", b);
            return RECV_ERR;
        }
    }

    return RECV_OK;
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
    //open specified data file
    fd_data = fopen(file, "rb");
    if (fd_data == NULL) {perror("FILE"); exit(-1); }
    if (stat(file, &st) != 0) {perror("ST"); exit(-1); }

    check(sp_get_port_by_name(port, &serial_port));

    printf("Opening port.\n");
    check(sp_open(serial_port, SP_MODE_READ_WRITE));

    printf("Setting port to 115200 8E1, no flow control.\n");
    check(sp_set_baudrate(serial_port, 115200));
    check(sp_set_bits(serial_port, 8));
    check(sp_set_parity(serial_port, SP_PARITY_EVEN));
    check(sp_set_stopbits(serial_port, 1));
    check(sp_set_flowcontrol(serial_port, SP_FLOWCONTROL_NONE));

    /* Mark inbound parity errors so recv() can detect them. With INPCK on,
     * IGNPAR off and PARMRK on, a byte received with a parity error is
     * delivered as the three-byte sequence \377 \0 <byte>; a genuine \377
     * in the data is doubled to \377 \377. libserialport doesn't expose
     * PARMRK, so set it directly on the underlying fd. */
    check(sp_get_port_handle(serial_port, &fd));
    if (tcgetattr(fd, &newtio) != 0) { perror("tcgetattr"); exit(-1); }
    newtio.c_iflag |= (INPCK | PARMRK);
    newtio.c_iflag &= ~IGNPAR;
    if (tcsetattr(fd, TCSANOW, &newtio) != 0) { perror("tcsetattr"); exit(-1); }

    printf("Flushing port buffers.\n");
    check(sp_flush(serial_port, SP_BUF_BOTH));
}

int send_block(uint8_t *data, uint32_t dest_addr, uint32_t len)
{
    uint8_t send_buf[4];
    int8_t r;
    send_buf[0] = CMD_WRITE;

    r = send(send_buf, 1);
    if (r == SEND_NACK) return SEND_NACK;
    if (r != SEND_OK)
    {
        printf("invalid response\n\r");
        return SEND_ERR;
    }

    wordToBytes(send_buf, dest_addr);
    r = send(send_buf, 4);
    if (r == SEND_NACK) return SEND_NACK;
    if (r != SEND_OK)
    {
        printf("invalid response\n\r");
        return SEND_ERR;
    }

    wordToBytes(send_buf, len);
    r = send(send_buf, 4);
    if (r == SEND_NACK) return SEND_NACK;
    if (r != SEND_OK)
    {
        printf("invalid response\n\r");
        return SEND_ERR;
    }

    r = send(data, len);
    if (r == SEND_NACK) return SEND_NACK;
    if (r != SEND_OK)
    {
        printf("Send error\n");
        return SEND_ERR;
    }

    return SEND_OK;
}

int read_block(uint8_t *dest, uint32_t src_addr, uint32_t len)
{
    uint8_t send_buf[4];
    int8_t r;
    send_buf[0] = CMD_READ;

    if (send(send_buf, 1) != SEND_OK)
    {
        printf("invalid response\n\r");
        return READ_ERR;
    }

    wordToBytes(send_buf, src_addr);
    if (send(send_buf, 4) != SEND_OK)
    {
        printf("invalid response\n\r");
        return READ_ERR;
    }

    wordToBytes(send_buf, len);
    if (send(send_buf, 4) != SEND_OK)
    {
        printf("invalid response\n\r");
        return READ_ERR;
    }

    r = recv(dest, len);
    if (r == RECV_PARITY) return READ_PARITY;
    if (r != RECV_OK)
    {
        printf("Read error\n");
        return READ_ERR;
    }

    return READ_OK;
}

int verify_block(uint8_t *expected, uint32_t src_addr, uint32_t len)
{
    uint32_t i;
    uint32_t mismatches = 0;
    uint32_t fetch;
    int8_t r = READ_ERR;

    /* Fetch the block back for comparison. A parity error on the inbound
     * data means the read-back itself was corrupted (not the written data),
     * so flush and re-fetch rather than rewriting the block. */
    for (fetch = 1; fetch <= BLOCK_ATTEMPTS; fetch++)
    {
        r = read_block(buf, src_addr, len);
        if (r != READ_PARITY) break;

        printf("Read-back at 0x%08x: parity error (fetch %d of %d), flushing and refetching\n\r",
               src_addr, fetch, BLOCK_ATTEMPTS);
        check(sp_flush(serial_port, SP_BUF_BOTH));
    }

    if (r != READ_OK)
    {
        return -1;
    }

    for (i = 0; i < len; i++)
    {
        if (buf[i] != expected[i])
        {
            printf("Verify mismatch at 0x%08x: wrote 0x%02x, read 0x%02x\n\r",
                   src_addr + i, expected[i], buf[i]);
            mismatches++;
        }
    }

    if (mismatches != 0)
    {
        printf("%u byte(s) differ\n\r", mismatches);
        return -1;
    }

    return 0;
}

int main(int argc, const char **argv)
{
    int8_t tmp;
    uint32_t data_offset;

    if(process_args(argc, argv) != 0)
    {
        terminate(-1);
    }    
    init();

    printf("file size: %d \n", st.st_size);
    file_data = malloc(st.st_size);
    if(fread(file_data, 1, st.st_size, fd_data) != st.st_size)
    {
        fputs ("File read error\n\r",stderr); 
        terminate(-1);
    }
    
    printf("Waiting for device\n\r");
    buf[0] = CMD_CHKRDY;
    if (send(buf, 1) != 0)
    { 
        printf("invalid response\n\r");
        terminate(-1);
    }
    
    printf("sending data: \n\r");
    for(data_offset = 0; data_offset < st.st_size; data_offset += BLOCK_SIZE)
    {
        uint32_t attempt;
        uint32_t send_size = st.st_size - data_offset;
        if(send_size > BLOCK_SIZE) send_size = BLOCK_SIZE;

        for(attempt = 1; attempt <= BLOCK_ATTEMPTS; attempt++)
        {
            tmp = send_block(&file_data[data_offset], base + data_offset, send_size);
            if(tmp == SEND_NACK)
            {
                /* Far end reported a parity error mid-transfer. Flush both
                 * queues to discard the partially-received block and any
                 * pending status bytes, then retry. */
                printf("Block at 0x%08x: parity error (attempt %d of %d), flushing and retrying\n\r",
                       base + data_offset, attempt, BLOCK_ATTEMPTS);
                check(sp_flush(serial_port, SP_BUF_BOTH));
                continue;
            }
            if(tmp == SEND_OK)
            {
                tmp = verify_block(&file_data[data_offset], base + data_offset, send_size);
            }
            if(tmp == SEND_OK) break;

            printf("Block at 0x%08x failed (attempt %d of %d)\n\r",
                   base + data_offset, attempt, BLOCK_ATTEMPTS);
        }
        if(tmp != 0)
        {
            printf("Block failed after %d attempts, giving up\n\r", BLOCK_ATTEMPTS);
            terminate(-1);
        }

        if(st.st_size - data_offset < BLOCK_SIZE)
        {
            printf("Sent and verified (%d) \n\r", st.st_size);
        }
        else
        {
            printf("sent and verified (%d of %d) \r", data_offset, st.st_size);
        }
        fflush(stdout);
    }

    if(run)
    {
        printf("Running program \n\r");
        buf[0] = CMD_JUMP;
        if (send(buf, 1) != 0) 
        {
            printf("invalid response\n");
            terminate(-1);
        }
    
        wordToBytes(buf, base);
        if (send(buf, 4) != 0) 
        {
            printf("invalid response\n");
            terminate(-1);
        }
    }

    terminate(0);
}

/* Helper function for error handling. */
int check(enum sp_return result)
{
        /* For this example we'll just exit on any error by calling abort(). */
        char *error_message;

        switch (result) {
        case SP_ERR_ARG:
                printf("Error: Invalid argument.\n");
                abort();
        case SP_ERR_FAIL:
                error_message = sp_last_error_message();
                printf("Error: Failed: %s\n", error_message);
                sp_free_error_message(error_message);
                abort();
        case SP_ERR_SUPP:
                printf("Error: Not supported.\n");
                abort();
        case SP_ERR_MEM:
                printf("Error: Couldn't allocate memory.\n");
                abort();
        case SP_OK:
        default:
                return result;
        }
}
