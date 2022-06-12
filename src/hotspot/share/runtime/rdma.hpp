#ifndef RDMA_H
#define RDMA_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include <netdb.h>
#include <rdma/rdma_cma.h>
#include <rdma/rdma_verbs.h>

#define BUFFER_SIZE 16
#define RW_BUFFER_SIZE 1 << 16 // 64 KB shoud be adquate, if not, do multiple trip


class RDMAConnection {

public:
    int get_from_remote (uint32_t rkey)
};

class RemoteRegion {

private:
    static const char *local_addr;
    static const char *port;

    static uint32_t rkey;  // rkey of remote memory to be used at local node
    static void* remote_addr;	// addr of remote memory to be used at local node	

    static uint8_t send_msg[BUFFER_SIZE];
    static uint8_t recv_msg[BUFFER_SIZE];
    static uint8_t rw_buff[RW_BUFFER_SIZE];

    static struct rdma_cm_id *listen_id, *id;
    static struct ibv_mr *mr, *send_mr, *rw_mr;
    static int send_flags;

public:

    int initialize(char* local_add, char* port);
    int rdma_send();
    int rdma_recv();

    int rdma_read();
    int rdma_write();

    static void print_a_buffer(uint8_t* buffer, int buffer_size, char* buffer_name) {
        printf("%s: ", buffer_name);
        for (int i = 0; i < buffer_size; i++) {
            printf("%02X ", buffer[i]);
        }
        printf("\n");
    }

    static void print_buffers() {
        print_a_buffer(send_msg, BUFFER_SIZE, (char*)"send_msg");
        print_a_buffer(recv_msg, BUFFER_SIZE, (char*)"recv_msg");
        printf("-----------\n");
    }


    static void get_wc_status(struct ibv_wc* wc) {
        printf("WC Status: %d\n", wc->status);
        printf("WC Opcode: %d\n", wc->opcode);
        printf("--------------\n");
    }

    uint32_t get_rkey () {
        return rkey;
    }

    void* get_remote_addr () {
        return remote_addr;
    }

};

#endif