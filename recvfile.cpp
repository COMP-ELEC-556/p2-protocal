//
// Created by Hanyi Wang on 3/6/20.
//

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string>
#include <vector>
#include <fcntl.h>
#include "iostream"
#include "utils.h"

//#include <chrono>
//#include <thread>

using namespace std;

int main(int argc, char **argv) {
    if (argc != 3) {
        perror("You have to input 3 arguments! \n");
        exit(0);
    }

    // Get parameters 1
    char *command_to_check = argv[1];
    if (strcmp(command_to_check, "-p") != 0) {
        perror("Invalid command 1, it should be -p! \n");
        exit(0);
    }

    // Get parameters 2
    char *recv_port_str = argv[2];
    if (!recv_port_str) {
        perror("Invalid recv_port input! \n");
        exit(0);
    }
    int recv_port = atoi(recv_port_str);
    if (recv_port < 18000 || recv_port > 18200) {
        perror("Receiver port number should be within 18000 and 18200! \n");
        exit(0);
    }

    int server_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (server_sock < 0) {
        perror("Unable to create socket! \n");
        exit(1);
    }

    struct sockaddr_in server_sin, client_sin;
    socklen_t client_len = sizeof(client_sin);

    memset(&server_sin, 0, sizeof(server_sin));
    server_sin.sin_family = AF_INET;
    server_sin.sin_addr.s_addr = htonl(INADDR_ANY);
    server_sin.sin_port = htons(recv_port);

    if (::bind(server_sock, (struct sockaddr *) &server_sin, sizeof(server_sin)) <
        0) { // make clear that it's in the global namespace
        perror("binding socket to address error!! \n");
        exit(1);
    }
    fcntl(server_sock, F_SETFL, O_NONBLOCK);

    char buff[BUFFER_SIZE];
    char ack_buff[ACK_BUFF_LEN];
    ofstream outFile;

    bool finish = false;
    int file_len;
    int last_seq = -1;

    int curr_seq = -1;

//    int acc_ack_seq = -1;
    int acc_recv_seq = -1;

    bool first_recv_meta = true;

    int send_count = 0;
    int recv_count = 0;

    vector<receiver_window_node *> window;
    for (int i = 0; i < WINDOW_SIZE; i++) {
        receiver_window_node *node = (receiver_window_node *) malloc(sizeof(int) + sizeof(bool) + PACKET_DATA_LEN);
        node->isReceived = false;
        node->packet_len = 0;
        node->data = (char *) malloc(PACKET_DATA_LEN);
        window.push_back(node);
    }

    // Metadata stop and wait with timeout
    bool meta_OK = false;

    while (true) {
        memset(buff, 0, BUFFER_SIZE);
        int recv_len = recvfrom(server_sock, buff, BUFFER_SIZE, MSG_DONTWAIT, (struct sockaddr *) &client_sin,
                                &client_len);
        if (recv_len <= 0) continue;    // Failed receiving data

        int seq_num = *((int *) buff);
        int acc_seq_num = *(int *) (buff + sizeof(int));
        int packet_len = *(int *) (buff + 2*sizeof(int));

//        cout << "PACKET " << seq_num << " RECEIVED" << endl;
        if (seq_num == META_DATA_FLAG) {    // Meta Data

            if (meta_OK) continue;

            cout << "Meta data received" << endl;

            file_len = *(int *) (buff + sizeof(int));
            int file_path_len = *(int *) (buff + 2 * sizeof(int));

            if (!check_meta_checksum(buff, file_path_len)) {  // OR PUT IT IN FRONT AND USE MAX_META_LEN???
                cout << "[recv meta data] / IGNORED (corrupted packet)" << endl;
                continue;
            }

            char file_path[MAX_FILE_PATH_LEN];
            memcpy(file_path, buff + 3 * sizeof(int) + sizeof(unsigned short), MAX_FILE_PATH_LEN);


            // create an ACK packet
            *(int *) ack_buff = META_DATA_FLAG;    // Ack num
            *(int *) (ack_buff + sizeof(int))  =  META_DATA_FLAG;
            *(bool *) (ack_buff + 2 * sizeof(int)) = true; // is_meta
            *(unsigned short *) (ack_buff + 2 * sizeof(int) + sizeof(bool)) = get_checksum(ack_buff + sizeof(int),
                                                                                       sizeof(int));   // Checksum

            cout << "META DATA! SEND META ACK IS: " << *(int *) ack_buff << " AND ACC ACK: "
                 << *(unsigned short *) (ack_buff + sizeof(int)) << endl;

            sendto(server_sock, &ack_buff, ACK_BUFF_LEN, 0, (struct sockaddr *) &client_sin, client_len);

            // Read
            if (first_recv_meta) {
                outFile.open(strcat(file_path, ".recv"), ios::out | ios::binary);
                first_recv_meta = false;
                cout << "File path is:" << file_path << " File length is: " << file_path_len << " Created file is: "
                     << file_path << ".recv" << endl;
            }
            curr_seq = 0;
        } else if (seq_num == END_DATA_FLAG) {
            meta_OK = true;
            if (!check_ack_checksum(buff)) {
                cout << "end flag got, but corrupted" << endl;
            } else {
                cout << "end flag received !!!!!!!!!!!! " << endl;
                break;
            }
        } else {
            meta_OK = true;
            data_packet *received_packet = (data_packet *) buff;
            int packet_len = received_packet->packet_len;    // *((int *) buff+sizeof(int));
            bool is_last_packet = received_packet->is_last_packet;

            // TODO checksum
            if (!check_checksum(buff)) {
                cout << "[recv data] / IGNORED (corrupted packet)" << endl;
                continue;
            }

            if (is_last_packet) {
                last_seq = seq_num;
//                last_seq = acc_seq_num;
            }

            // Check if this is out of the range of window, ignore and do not send ack back!
            if (!inWindow(seq_num, curr_seq - 1)) {
//                cout << "[recv data] / IGNORED (out-of-window)" << endl;
                // create an ACK packet
                *(int *) ack_buff = seq_num;    // Ack num
                *(int *) (ack_buff + sizeof(int)) = acc_seq_num;
                *(bool *) (ack_buff + 2 * sizeof(int)) = false; // is_meta
                *(unsigned short *) (ack_buff + 2 * sizeof(int) + sizeof(bool)) = get_checksum(ack_buff + sizeof(int),
                                                                                           sizeof(int));   // Checksum

                *(bool *) (ack_buff + 2*sizeof(int) + sizeof(bool) + sizeof(unsigned short)) = is_last_packet;

//                cout << "OUT-OF-WINDOW, NOW SEND ACK IS: " << *(int *) ack_buff << " AND ACK CHECKSUM: "
//                     << *(unsigned short *) (ack_buff + sizeof(int) + sizeof(bool)) << endl;
                sendto(server_sock, &ack_buff, ACK_BUFF_LEN, 0, (struct sockaddr *) &client_sin, client_len);
                continue;
            } else {
                // If the packet is received? (duplicate) Do not send ack back!
                receiver_window_node *packet_in_window = window[seq_num % (WINDOW_SIZE)];
                if (packet_in_window->isReceived) {

                    *(int *) ack_buff = seq_num;    // Ack num
                    *(int *) (ack_buff + sizeof(int)) = acc_seq_num;
                    *(bool *) (ack_buff + 2  * sizeof(int)) = false; // is_meta
                    *(unsigned short *) (ack_buff + 2 * sizeof(int) + sizeof(bool)) = get_checksum(ack_buff + sizeof(int),
                                                                                               sizeof(int));   // Checksum
                    *(bool *) (ack_buff + 2 * sizeof(int) + sizeof(bool) + sizeof(unsigned short)) = is_last_packet;

                    cout << "DUPLICATE, SO RESEND ACK IS: " << *(int *) ack_buff << " cumulative ack: " << acc_seq_num << " isLast? "
                         << *(bool *) (ack_buff + 2*sizeof(int) + sizeof(bool) + sizeof(unsigned short)) << endl;

                    sendto(server_sock, ack_buff, ACK_BUFF_LEN, 0, (struct sockaddr *) &client_sin, client_len);
                    continue;
                }

                if (acc_seq_num <= acc_recv_seq) {
                    cout << "delay, we ignore it"<< endl;
                    continue;
                }

                // Copy data into the node
                packet_in_window->isReceived = true;
                packet_in_window->packet_len = packet_len;
                packet_in_window->seq_num = seq_num;

                memset(packet_in_window->data, 0, PACKET_DATA_LEN * sizeof(char));

                memcpy(packet_in_window->data, (buff + PACKET_HEADER_LEN), PACKET_DATA_LEN);
                // Update window
                if (seq_num == curr_seq) {  // If matches, write that to file and move window


//                    cout << "[recv data] / ACCEPTED (in-order)" << endl;
                    // write back and move
                    int curr_idx = curr_seq % (WINDOW_SIZE);
                    receiver_window_node *currNode = window[curr_idx];
                    while (currNode->isReceived) {
                        outFile.write(currNode->data, currNode->packet_len);
                        cout <<"Write! seq num: "<< currNode->seq_num  <<" Is it the last?" << is_last_packet << " and WE received a packet with length: " << currNode->packet_len <<endl;
                        recv_count += 1;
                        if (curr_seq == last_seq) {
                            finish = true;
                            break;
                        }
                        curr_seq = (curr_seq + 1) % (MAX_SEQ_LEN);

                        acc_recv_seq += 1;

                        currNode->isReceived = false;
                        curr_idx = (curr_idx + 1) % (WINDOW_SIZE);
                        currNode = window[curr_idx];
                    }
                } else {    // If fall in window, store it.
//                    cout << "[recv data] / ACCEPTED (out-of-order)" << endl;
                }

                cout << "Curr seq is: " << seq_num << "window的左边界" << curr_seq << " 右边界 " << (curr_seq + WINDOW_SIZE) % (MAX_SEQ_LEN) << endl;

                // Create ack packet
                *(int *) ack_buff = seq_num;    // Ack num
                *(int *) (ack_buff + sizeof(int)) = acc_seq_num;
                *(bool *) (ack_buff + 2*sizeof(int)) = false; // is_meta
                *(unsigned short *) (ack_buff + 2*sizeof(int) + sizeof(bool)) = get_checksum(ack_buff + sizeof(int),
                                                                                           sizeof(int));   // Checksum
                *(bool *) (ack_buff + 2*sizeof(int) + sizeof(bool) + sizeof(unsigned short)) = is_last_packet;
//                cout << "SEND ACK IS: " << *(int *) ack_buff << " isLast? "
//                     << *(bool *) (ack_buff + sizeof(int) + sizeof(bool) + sizeof(unsigned short)) << endl;

                sendto(server_sock, ack_buff, ACK_BUFF_LEN, 0, (struct sockaddr *) &client_sin, client_len);
                send_count += 1;
                if (finish) {
                    cout << "[complete!]" << endl;
                    // TODO 加一个timeout，来解决没收到end-FLA，保证退出

//                    break;
                }
            }
        }
    }

    cout << "Send " << send_count << " ack packet and receive: " << recv_count << " data packet" << endl;


    outFile.close();
    close(server_sock);
    for (int i = 0; i < WINDOW_SIZE; i++) {
        receiver_window_node *node = window[i];
        free(node->data);
        free(node);
    }
    return 0;
}