#ifndef RemoteRegion_H
#define RemoteRegion_H

#include <errno.h>
#include <netdb.h>
// namespace rdma_conn {
// }

#include <rdma/rdma_cma.h>
#include <infiniband/verbs.h>

#include "gc/shenandoah/shenandoahHeap.hpp"

#define BUFFER_SIZE 16
#define RW_BUFFER_SIZE 1 << 16 // 64 KB shoud be adquate, if not, do multiple trip

class RDMAConnection;
class RemoteRegion;
class ShenandoahHeap;

enum DISCONNECT_CODE {
    out_destroy_listen_ep,
    out_free_addrinfo,
    out_destroy_accept_ep,
    out_dereg_recv,
    out_dereg_send,
    out_disconnect,

};


class RemoteRegion : public CHeapObj<mtGC> {

private:
    RDMAConnection* connection;

    uint32_t rkey;  // rkey of remote memory to be used at local node
    void* remote_addr;	// addr of remote memory to be used at local node	

    uint8_t send_msg[BUFFER_SIZE];
    uint8_t recv_msg[BUFFER_SIZE];
    uint8_t rw_buff[RW_BUFFER_SIZE];

    struct rdma_cm_id *id;
    struct ibv_mr *mr, *send_mr, *rw_mr;
    int send_flags;
    struct ibv_wc* wc;

public:
    RemoteRegion(RDMAConnection* connection);
    int initialize();
    int disconnect(DISCONNECT_CODE code);
    int rdma_send();
    int rdma_recv();

    int rdma_read(uint8_t* buffer, int length, int addr_offset);
    int rdma_write(uint8_t* buffer, int length, int addr_offset);


    void print_a_buffer(uint8_t* buffer, int buffer_size, char* buffer_name) {
        tty->print_cr("%s: ", buffer_name);
        for (int i = 0; i < buffer_size; i++) {
            tty->print("%02X ", buffer[i]);
        }
        tty->print("\n");
    }

    void print_buffers() {
        print_a_buffer(send_msg, BUFFER_SIZE, (char*)"send_msg");
        print_a_buffer(recv_msg, BUFFER_SIZE, (char*)"recv_msg");
        tty->print_cr("-----------");
    }


    void get_wc_status(struct ibv_wc* wc) {
        tty->print_cr("WC Status: %d", wc->status);
        tty->print_cr("WC Opcode: %d", wc->opcode);
        tty->print_cr("--------------");
    }

    uint32_t get_rkey () {
        return rkey;
    }

    void* get_remote_addr () {
        return remote_addr;
    }

};


class RDMAConnection : public CHeapObj<mtGC> {

private:

    ShenandoahHeap* _heap;
    struct rdma_cm_id* _listen_id;
    int num_connections;
    RemoteRegion** rr_arr;

    struct rdma_addrinfo *res;

public:
    RDMAConnection(ShenandoahHeap* heap, int num_connections);
    int establish_connections(char* local_addr, char* port);
    int get_from_remote (uint32_t rkey);

    int disconnect(DISCONNECT_CODE code);



    struct rdma_cm_id ** listen_id() {
        return &_listen_id;
    }
};

#endif