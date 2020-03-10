//
// Created by Hanyi Wang on 3/8/20.
//

#ifndef PROJECT2PRE_RECEIVERCONTROLLER_H
#define PROJECT2PRE_RECEIVERCONTROLLER_H


#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <string>
#include <fstream>

using namespace std;

#define BUFFER_SIZE 32768
#define WINDOW_SIZE 8
#define ACK_NOT_RECEIVED 0
#define ACK_RECEIVED 1
#define MAX_SEQ_LEN 2*WINDOW_SIZE
#define ACK_PACKET_LEN 24

#define PACKET_DATA_LEN 1024
#define PACKET_HEADER_LEN 4
#define CRC_POS 0
#define SEQ_POS 2

// ACK packet configuration
#define ACK_DATA_LEN 18
#define ACK_HEADER_LEN 6
#define ACK_CRC_POS 0
#define ACK_SEQ_POS 2
#define ACK_ACK_POS 4

#define TIMEOUT 5000

struct ack_packet {
    u_short crc;
    u_short seq_num;
    u_short ack;
    uint32_t send_sec;
    uint32_t send_usec;
    char * padding;
};

//struct send_packet {
//    uint16_t packet_len;
//    uint16_t seq_num;
//    time_t send_time;
//    time_t recv_time;
//    int type;
//    char * data;
//};

struct meta_data {
    uint16_t file_size;
    string file_dir;
    string file_name;
};

struct window_node {
    bool isReceived;
    u_short seq_num;
    char * data;
};



void update_window(bool *finish) {

}

class receiverController {
public:
    receiverController();

    ack_packet * get_ack_packet();

private:
    ack_packet * meta_packet_ack;

};

#endif //PROJECT2PRE_RECEIVERCONTROLLER_H
