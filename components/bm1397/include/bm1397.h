#ifndef BM1397_H_
#define BM1397_H_

#include "driver/gpio.h"

#define BM1397_RST_PIN  GPIO_NUM_1


#define TYPE_JOB 0x20
#define TYPE_CMD 0x40

#define GROUP_SINGLE 0x00
#define GROUP_ALL 0x10

#define CMD_JOB 0x01

#define CMD_SETADDRESS 0x00
#define CMD_WRITE 0x01
#define CMD_READ 0x02
#define CMD_INACTIVE 0x03

#define RESPONSE_CMD 0x00
#define RESPONSE_JOB 0x80
#define CRC5_MASK 0x1F

static const u_int64_t BM1397_FREQUENCY = CONFIG_BM1397_FREQUENCY;
static const u_int64_t BM1397_CORE_COUNT = 672;
static const u_int64_t BM1397_HASHRATE_S = BM1397_FREQUENCY * BM1397_CORE_COUNT * 1000000;
//2^32
static const u_int64_t NONCE_SPACE = 4294967296;
static const double  BM1397_FULLSCAN_MS = ((double)NONCE_SPACE / (double)BM1397_HASHRATE_S) * 1000; 



typedef enum {
  JOB_PACKET = 0, 
  CMD_PACKET = 1,
} packet_type_t;

typedef enum {
  JOB_RESP = 0, 
  CMD_RESP = 1,
} response_type_t;

struct __attribute__((__packed__)) job_packet {
  uint8_t job_id;
  uint8_t num_midstates;
  uint8_t starting_nonce[4];
  uint8_t nbits[4];
  uint8_t ntime[4];
  uint8_t merkle4[4];
  uint8_t midstate[32];
};

struct __attribute__((__packed__)) nonce_response {
    uint8_t preamble[2];
    uint32_t nonce;
    uint8_t midstate_num;
    uint8_t job_id;
    uint8_t crc;
};


void send_read_address(void);
void send_init(void);
void send_work(struct job_packet *job);
void reset_BM1397(void);
void init_BM1397(void);
void set_job_difficulty_mask(int);
unsigned char reverseBits(unsigned char num);
int largestPowerOfTwo(int num);
void set_bm1397_max_baud(void);

#endif /* BM1397_H_ */