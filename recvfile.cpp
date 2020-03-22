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
    int curr_seq = -1;
    int last_seq = -1;
    bool finish = false;
    ofstream outFile;
    int file_len;

    bool first_recv_meta = true;

    int send_count = 0;
    int recv_count = 0;

    vector<receiver_window_node *> window;
    for (int i = 0; i < WINDOW_SIZE; i++) {
        receiver_window_node *node = (receiver_window_node *) malloc(sizeof(int) + sizeof(bool) + PACKET_DATA_LEN);
        node->isReceived = false;
        node->data = (char *) malloc(PACKET_DATA_LEN);
        window.push_back(node);
    }

    // Metadata stop and wait with timeout

    while (true) {
        memset(buff, 0, BUFFER_SIZE);
        int recv_len = recvfrom(server_sock, buff, BUFFER_SIZE, MSG_DONTWAIT, (struct sockaddr *) &client_sin,
                                &client_len);
        if (recv_len <= 0) continue;    // Failed receiving data

//        if (!get_random()) continue;

        int seq_num = *((int *) buff);

        cout << "PACKET " << seq_num  <<" RECEIVED" << endl;
        if (seq_num == META_DATA_FLAG) {    // Meta Data
            cout << "Meta data received" << endl;

            file_len = *(int *) (buff + sizeof(int));
            int file_path_len = *(int *) (buff + 2 * sizeof(int));

            if (!check_meta_checksum(buff, file_path_len)) {  // OR PUT IT IN FRONT AND USE MAX_META_LEN???
                cout << "[recv data] / IGNORED (corrupted packet)" << endl;
                continue;
            }

            char file_path[MAX_FILE_PATH_LEN];
            memcpy(file_path, buff + 3 * sizeof(int) + sizeof(unsigned short), MAX_FILE_PATH_LEN);
            cout << "File path is:" << file_path << " File length is: " << file_path_len << " Created file is: "
                 << file_path << ".recv" << endl;

            // create an ACK packet
            *(int *) ack_buff = META_DATA_FLAG;    // Ack num
            *(bool *) (ack_buff + sizeof(int)) = true; // is_meta
            *(unsigned short *) (ack_buff + sizeof(int) + sizeof(bool)) = get_checksum(ack_buff,
                                                                                       sizeof(int));   // Checksum

            cout << "META DATA! SEND META ACK IS: " << *(int *) ack_buff << " AND ACK CHECKSUM: "
                 << *(unsigned short *) (ack_buff + sizeof(int) + sizeof(bool)) << endl;

            sendto(server_sock, &ack_buff, ACK_BUFF_LEN, 0, (struct sockaddr *) &client_sin, client_len);

            // Read
            if (first_recv_meta) {
                outFile.open(strcat(file_path, ".recv"), ios::out | ios::binary);
                first_recv_meta = false;
            }
            curr_seq = 0;
        } else {
            data_packet *received_packet = (data_packet *) buff;
            int packet_len = received_packet->packet_len;    // *((int *) buff+sizeof(int));
            bool is_last_packet = received_packet->is_last_packet;

            // TODO checksum
            unsigned short received_checksum = received_packet->checksum;
            if (!check_checksum(buff)) {
                cout << "[recv data] / IGNORED (corrupted packet)" << endl;
                continue;
            }

            if (is_last_packet) {
                last_seq = seq_num;
            }

            // Check if this is out of the range of window, ignore and do not send ack back!
            cout << "window的左边界" <<curr_seq << " 右边界 " << (curr_seq + WINDOW_SIZE) % (MAX_SEQ_LEN) << endl;
            if (!inWindow(seq_num, curr_seq - 1)) {
                cout << "[recv data] / IGNORED (out-of-window)" << endl;

                // create an ACK packet
                *(int *) ack_buff = seq_num;    // Ack num
                *(bool *) (ack_buff + sizeof(int)) = false; // is_meta
                *(unsigned short *) (ack_buff + sizeof(int) + sizeof(bool)) = get_checksum(ack_buff, sizeof(int));   // Checksum

                if (is_last_packet) {
                    *(bool *) (ack_buff + sizeof(int) + sizeof(bool) + sizeof(unsigned short)) = true;
                } else {
                    *(bool *) (ack_buff + sizeof(int) + sizeof(bool) + sizeof(unsigned short)) = false;
                }

                cout << "OUT-OF-WINDOW, NOW SEND ACK IS: " << *(int *) ack_buff << " AND ACK CHECKSUM: "
                     << *(unsigned short *) (ack_buff + sizeof(int) + sizeof(bool)) << endl;
                sendto(server_sock, &ack_buff, ACK_BUFF_LEN, 0, (struct sockaddr *) &client_sin, client_len);
                // IGNORE OUT OF BOUND

                continue;
            } else {
                // If the packet is received? (duplicate) Do not send ack back!
                receiver_window_node *packet_in_window = window[seq_num % (WINDOW_SIZE)];
                if (packet_in_window->isReceived) {
                    cout << "[recv data] / IGNORED (duplicate)" << endl;
                    continue;
                }

                // Copy data into the node
                packet_in_window->isReceived = true;
                packet_in_window->seq_num = seq_num;

                memset(packet_in_window->data, 0, PACKET_DATA_LEN * sizeof(char));

                memcpy(packet_in_window->data, (buff + PACKET_HEADER_LEN), PACKET_DATA_LEN);
//                cout << "RECEIVED SEQ_NUM: " << received_packet->seq_num << " PACKET LEN: "
//                     << received_packet->packet_len << endl;
                // Update window
                if (seq_num == curr_seq) {  // If matches, write that to file and move window
                    cout << "[recv data] / ACCEPTED (in-order)" << endl;
                    // write back and move
                    int curr_idx = curr_seq % (WINDOW_SIZE);
                    receiver_window_node *currNode = window[curr_idx];
                    while (currNode->isReceived) {
                        outFile.write(currNode->data, packet_len);
                        cout <<"Write file, recv_count is "<< recv_count + 1  <<endl;
                        recv_count += 1;
                        if (curr_seq == last_seq) {
                            finish = true;
                            break;
                        }
                        curr_seq = (curr_seq + 1) % (MAX_SEQ_LEN);

                        currNode->isReceived = false;
                        curr_idx = (curr_idx + 1) % (WINDOW_SIZE);
                        currNode = window[curr_idx];
                    }
                } else {    // If fall in window, store it.
                    cout << "[recv data] / ACCEPTED (out-of-order)" << endl;
                }

                // Create ack packet
                *(int *) ack_buff = seq_num;    // Ack num
                *(bool *) (ack_buff + sizeof(int)) = false; // is_meta
                *(unsigned short *) (ack_buff + sizeof(int) + sizeof(bool)) = get_checksum(ack_buff,
                                                                                           sizeof(int));   // Checksum
                 if (is_last_packet) {
                    *(bool *) (ack_buff + sizeof(int) + sizeof(bool) + sizeof(unsigned short)) = true;
                } else {
                    *(bool *) (ack_buff + sizeof(int) + sizeof(bool) + sizeof(unsigned short)) = false;
                }
                cout << "SEND ACK IS: " << *(int *) ack_buff << " isLast? "
                     << *(bool *) (ack_buff + sizeof(int) + sizeof(bool) + sizeof(unsigned short)) << endl;

//                std::this_thread::sleep_for(std::chrono::milliseconds(300));

                sendto(server_sock, ack_buff, ACK_BUFF_LEN, 0, (struct sockaddr *) &client_sin, client_len);
                send_count += 1;
                if (finish) {
                    cout << "[complete!]" << endl;
                    break;
                }
            }
        }
    }

    cout << "Send " << send_count << " ack packet and receive: " << recv_count<<" data packet" <<endl;


    outFile.close();
    close(server_sock);
    for (int i = 0; i < WINDOW_SIZE; i++) {
        receiver_window_node *node = window[i];
        free(node->data);
        free(node);
    }
    return 0;
}