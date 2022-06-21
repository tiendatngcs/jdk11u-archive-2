#ifndef RemoteMem_H
#define RemoteMem_H

#include <errno.h>
#include <netdb.h>
// namespace rdma_conn {
// }

#include <rdma/rdma_cma.h>
#include <infiniband/verbs.h>

#include "gc/shenandoah/shenandoahHeap.hpp"

#define BUFFER_SIZE 16
#define RW_BUFFER_SIZE 1 << 16 // 64KB shoud be adquate, if not, do multiple trips

#define MR_SIZE 1 << 32 // 4GB

class RemoteMem;
// class RemoteRegion;
class ShenandoahHeap;

enum DISCONNECT_CODE {
    out_destroy_listen_ep,
    out_free_addrinfo,
    out_destroy_accept_ep,
    out_dereg_recv,
    out_dereg_send,
    out_disconnect,

};

enum COMM_CODE {
    BEGIN,
	REG_MR,
	EXIT,
    TEST = 10
};


// =================UtilFunctions============================= 


static void print_a_buffer(uint8_t* buffer, int buffer_size, char* buffer_name) {
	tty->print_cr("%s: ", buffer_name);
	for (int i = 0; i < buffer_size; i++) {
		tty->print("%02X ", buffer[i]);
	}
	tty->print("\n");
}

static void get_wc_status(struct ibv_wc* wc) {
	tty->print_cr("WC Status: %d", wc->status);
	tty->print_cr("WC Opcode: %d", wc->opcode);
	tty->print_cr("--------------");
}

static int rdma_post_read(struct rdma_cm_id *id, void *context, void *addr,
	       size_t length, struct ibv_mr *mr, int flags,
	       uint64_t remote_addr, uint32_t rkey)
{
	struct ibv_sge sge;

	sge.addr = (uint64_t) (uintptr_t) addr;
	sge.length = (uint32_t) length;
	sge.lkey = mr->lkey;


	struct ibv_send_wr wr, *bad;

	wr.wr_id = (uintptr_t) context;
	wr.next = NULL;
	wr.sg_list = &sge;
	wr.num_sge = 1;
	wr.opcode = IBV_WR_RDMA_READ;
	wr.send_flags = flags;
	wr.wr.rdma.remote_addr = remote_addr;
	wr.wr.rdma.rkey = rkey;

	return ibv_post_send(id->qp, &wr, &bad);
}

static int rdma_post_write(struct rdma_cm_id *id, void *context, void *addr,
		size_t length, struct ibv_mr *mr, int flags,
		uint64_t remote_addr, uint32_t rkey)
{
	struct ibv_sge sge;

	sge.addr = (uint64_t) (uintptr_t) addr;
	sge.length = (uint32_t) length;
	sge.lkey = mr ? mr->lkey : 0;

	// return rdma_post_writev(id, context, &sge, 1, flags, remote_addr, rkey);

	struct ibv_send_wr wr, *bad;

	wr.wr_id = (uintptr_t) context;
	wr.next = NULL;
	wr.sg_list = &sge;
	wr.num_sge = 1;
	wr.opcode = IBV_WR_RDMA_WRITE;
	wr.send_flags = flags;
	wr.wr.rdma.remote_addr = remote_addr;
	wr.wr.rdma.rkey = rkey;

	return ibv_post_send(id->qp, &wr, &bad);
}

static int rdma_post_send(struct rdma_cm_id *id, void *context, void *addr,
	       size_t length, struct ibv_mr *mr, int flags)
{
	struct ibv_sge sge;

	sge.addr = (uint64_t) (uintptr_t) addr;
	sge.length = (uint32_t) length;
	sge.lkey = mr ? mr->lkey : 0;

	// return rdma_post_sendv(id, context, &sge, 1, flags);

	struct ibv_send_wr wr, *bad;

	wr.wr_id = (uintptr_t) context;
	wr.next = NULL;
	wr.sg_list = &sge;
	wr.num_sge = 1;
	wr.opcode = IBV_WR_SEND;
	wr.send_flags = flags;

	return ibv_post_send(id->qp, &wr, &bad);
}

static int rdma_post_recv(struct rdma_cm_id *id, void *context, void *addr,
	       size_t length, struct ibv_mr *mr)
{
	struct ibv_sge sge;

	assert((addr >= mr->addr) &&
		(((uint8_t *) addr + length) <= ((uint8_t *) mr->addr + mr->length)), "Fail");
	sge.addr = (uint64_t) (uintptr_t) addr;
	sge.length = (uint32_t) length;
	sge.lkey = mr->lkey;

	// return rdma_post_recvv(id, context, &sge, 1);

	struct ibv_recv_wr wr, *bad;

	wr.wr_id = (uintptr_t) context;
	wr.next = NULL;
	wr.sg_list = &sge;
	wr.num_sge = 1;

	if (id->srq)
		return ibv_post_srq_recv(id->srq, &wr, &bad);
	else
		return ibv_post_recv(id->qp, &wr, &bad);
}

static int rdma_get_recv_comp(struct rdma_cm_id *id, struct ibv_wc *wc)
{
	struct ibv_cq *cq;
	void *context;
	int ret;

	do {
		ret = ibv_poll_cq(id->recv_cq, 1, wc);
		if (ret)
			break;

		ret = ibv_req_notify_cq(id->recv_cq, 0);
		if (ret)
			return ret;

		ret = ibv_poll_cq(id->recv_cq, 1, wc);
		if (ret)
			break;

		ret = ibv_get_cq_event(id->recv_cq_channel, &cq, &context);
		if (ret)
			return ret;

		assert(cq == id->recv_cq && context == id, "Fail");
		ibv_ack_cq_events(id->recv_cq, 1);
	} while (1);
	return ret;
}

static int rdma_get_send_comp(struct rdma_cm_id *id, struct ibv_wc *wc)
{
	struct ibv_cq *cq;
	void *context;
	int ret;

	do {
		ret = ibv_poll_cq(id->send_cq, 1, wc);
		if (ret)
			break;

		ret = ibv_req_notify_cq(id->send_cq, 0);
		if (ret)
			return ret;

		ret = ibv_poll_cq(id->send_cq, 1, wc);
		if (ret)
			break;

		ret = ibv_get_cq_event(id->send_cq_channel, &cq, &context);
		if (ret)
			return ret;

		assert(cq == id->send_cq && context == id, "Fail");
		ibv_ack_cq_events(id->send_cq, 1);
	} while (1);
	return ret;
}


// class RemoteRegion : public CHeapObj<mtGC> {

// private:
//     RemoteMem* connection;

//     uint32_t rkey;  // rkey of remote memory to be used at local node
//     void* remote_addr;	// addr of remote memory to be used at local node	

//     uint8_t rw_buff[RW_BUFFER_SIZE];

//     struct ibv_mr *rw_mr;
//     int send_flags;
//     struct ibv_wc wc;

	

// public:
//     RemoteRegion(RemoteMem* connection, uint32_t rkey, void* remote_addr);
//     int initialize();
//     int disconnect(DISCONNECT_CODE code);
//     // int rdma_send(uint8_t* buffer, int length);
//     // int rdma_recv(uint8_t* buffer, int length);

//     int rdma_read(uint8_t* buffer, int length, int addr_offset);
//     int rdma_write(uint8_t* buffer, int length, int addr_offset);

// 	int get_new_region();


//     void print_buffers() {
//         print_a_buffer(send_msg, BUFFER_SIZE, (char*)"send_msg");
//         print_a_buffer(recv_msg, BUFFER_SIZE, (char*)"recv_msg");
//         tty->print_cr("-----------");
//     }




//     uint32_t get_rkey () {
//         return rkey;
//     }

//     void* get_remote_addr () {
//         return remote_addr;
//     }

// };

class MemoryRegion : public CHeapObj<mtGC> {
private:
	uint32_t _rkey;
	void* _addr;
	size_t _allocation_offset; // offset from mr->addr where an allocation can happen
	size_t _used; 

public:

private:

public:
	MemoryRegion(uint32_t rkey, void* start_addr);
	size_t allocation_offset() 	{ return _allocation_offset; }
	size_t used() 						{ return _used; }
	size_t free() 						{ return (size_t)MR_SIZE - _used; }
	uint32_t rkey() 					{ return _rkey; }
	void* start_addr() 					{ return _addr; }
	void* allocation_pointer()			{ return (uint8_t*)_addr + _allocation_offset; }

	void increment_used(int i)			{_used += i;}
};


class RDMAServer : public CHeapObj<mtGC> {
private:
	RemoteMem* connection;
	MemoryRegion** mr_arr;
	// RemoteRegion* rr_arr[100]; //allocate 100 regions each server
	int next_empty_idx;
	int send_flags;
	struct ibv_wc wc;

    uint8_t send_msg[BUFFER_SIZE];
    uint8_t recv_msg[BUFFER_SIZE];
	uint8_t rdma_buff[RW_BUFFER_SIZE];
    struct ibv_mr *recv_mr, *send_mr, *rdma_mr;


	// struct ibv_mr * mr []


public:
	int idx;
	// attributes for communication
	struct rdma_cm_id *cm_id;

private:
	MemoryRegion* get_current_mr(size_t length);



public:
	RDMAServer(int idx, RemoteMem* connection);
	int send_comm_code(COMM_CODE code);
	int reg_new_mr();
	int register_comm_mr();
	int register_rdma_mr();
	

	int rdma_write(uint8_t* buffer, size_t length);
	int rdma_read(uint8_t* buffer, int length, MemoryRegion* mr, int addr_offset);

	int rdma_send(char* buff);
	int rdma_recv(char* buff);

	int disconnect(DISCONNECT_CODE code);
};


class RemoteMem : public CHeapObj<mtGC> {

/* 

Responsible for control path 

Each mem region is set to be 4GB, each expansion would register extra 4GB at remote nodes

A pointer to a remote location is made up of
[       32 bit rkey     |       32 bit mr offset        ]

allocation is bump-pointing in remote mem region



*/


private:

    ShenandoahHeap* _heap;
    struct rdma_cm_id* _listen_id;

    int num_connections;
    // RemoteRegion** rr_arr;
	RDMAServer** server_arr;

    struct rdma_addrinfo *res;

    // void* current_head;  // Head of allocation
    int current_rr_idx;
    uint32_t current_offset;

public:

    RemoteMem(ShenandoahHeap* heap, int num_connections);
    int establish_connections(char* local_addr, char* port);
    bool expand_remote_region(int rm_idx, int expansion_size);


    void perform_some_tests();

    // Read write to remote mem (Use mapping to get remote addr)
    int allocate(int size);

    int disconnect(DISCONNECT_CODE code);



    struct rdma_cm_id ** listen_id() { return &_listen_id;}

private:

	// int send_msg(int node_idx, uint8_t* msg);
	// int recv_msg(uint8_t* msg);






    // uint32_t get_current_offset() {
    //     return current_offset;
    // }

    // uint32_t get_current_rkey() {
    //     return rr_arr[current_rr_idx]->get_rkey();
    // }

    // uint64_t get_current_addr() {
    //     return (uint64_t)get_current_rkey() << 32 | (uint64_t)get_current_offset();
    // }
};

#endif