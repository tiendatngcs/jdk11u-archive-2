// #include <stdio.h>
// #include <stdlib.h>
// #include <errno.h>
// #include <netdb.h>
// #include <rdma/rdma_cma.h>
// #include <rdma/rdma_verbs.h>
#include "gc/shenandoah/remoteRegion.hpp"

// =================UtilFunctions============================= 

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

// =================RDMAConnect============================= 


RDMAConnection::RDMAConnection(ShenandoahHeap* heap, int num_connections) : 
	num_connections(num_connections),
	_heap(heap),
	_listen_id(NULL),
	rr_arr(),
	res(NULL){}


int RDMAConnection::establish_connections(char* local_addr, char* port){
    struct rdma_addrinfo hints;
	struct ibv_qp_init_attr init_attr;
    int ret;
    memset(&hints, 0, sizeof hints);
	hints.ai_flags = RAI_PASSIVE;
	hints.ai_port_space = RDMA_PS_TCP;
	ret = rdma_getaddrinfo(local_addr, port, &hints, &res);
	if (ret) {
		tty->print_cr("rdma_getaddrinfo: %s", gai_strerror(ret));
		// tty->print_cr("Fail at rdma_getaddrinfo");
		return ret;
	}

    memset(&init_attr, 0, sizeof init_attr);
	init_attr.cap.max_send_wr = init_attr.cap.max_recv_wr = 1;
	init_attr.cap.max_send_sge = init_attr.cap.max_recv_sge = 1;
	init_attr.cap.max_inline_data = 16;
	init_attr.sq_sig_all = 1;
	ret = rdma_create_ep(&_listen_id, res, NULL, &init_attr);
	if (ret) {
		perror("rdma_create_ep");
        return disconnect(out_free_addrinfo);
	}

    tty->print_cr("Server listening ...");
	ret = rdma_listen(_listen_id, 100);
	if (ret) {
		perror("rdma_listen");
		return disconnect(out_destroy_listen_ep);
	}

    rr_arr = (RemoteRegion**)malloc(sizeof(RemoteRegion*) * num_connections);

    for (int i= 0; i < num_connections; i ++) {
        rr_arr[i] = new RemoteRegion(this);
		rr_arr[i]->initialize();
    }

    return ret;
}

int RDMAConnection::disconnect (DISCONNECT_CODE code) {
    switch (code){
        case out_destroy_listen_ep:
            rdma_destroy_ep(*listen_id());
        case out_free_addrinfo:
            rdma_freeaddrinfo(res);
            break;
            

        default:
            return 0;
    }
    return 0;
}

// =======================================================================

RemoteRegion::RemoteRegion(RDMAConnection* connection) : 
	connection(connection),
	rkey(0),
	remote_addr(NULL),
	send_msg(),
	recv_msg(),
	rw_buff(),
	id(NULL),
	mr(NULL),
	send_mr(NULL),
	rw_mr(NULL),
	send_flags(0),
	wc(NULL) {
	
	initialize();
}


int RemoteRegion::initialize () {
    struct rdma_addrinfo hints;
	struct ibv_qp_init_attr init_attr;
	struct ibv_qp_attr qp_attr;
	int ret;
    void* temp;


    tty->print_cr("Getting connection request ...");
	ret = rdma_get_request(*connection->listen_id(), &id);
	// ret = rdma_get_request(NULL, NULL);
	if (ret) {
		perror("rdma_get_request");
        return connection->disconnect(out_destroy_listen_ep);
		// goto out_destroy_listen_ep;
	}

    // print_buffers();

	memset(&qp_attr, 0, sizeof qp_attr);
	memset(&init_attr, 0, sizeof init_attr);
	ret = ibv_query_qp(id->qp, &qp_attr, IBV_QP_CAP,
			   &init_attr);
	if (ret) {
		perror("ibv_query_qp");
        return disconnect(out_destroy_accept_ep);
		// goto out_destroy_accept_ep;
	}
	if (init_attr.cap.max_inline_data >= 16)
		send_flags = IBV_SEND_INLINE;
	else
		tty->print_cr("rdma_server: device doesn't support IBV_SEND_INLINE, "
		       "using sge sends");

    tty->print_cr("Registering mr ...");
	// mr = rdma_reg_msgs(id, recv_msg, 16);
	mr = ibv_reg_mr(id->pd, recv_msg, 16, IBV_ACCESS_LOCAL_WRITE);
	if (!mr) {
		ret = -1;
		perror("rdma_reg_msgs for recv_msg");
        return disconnect(out_destroy_accept_ep);
		// goto out_destroy_accept_ep;
	}
	if ((send_flags & IBV_SEND_INLINE) == 0) {
        tty->print_cr("Registering send mr ...");
		// send_mr = rdma_reg_msgs(id, send_msg, 16);
		send_mr = ibv_reg_mr(id->pd, send_msg, 16, IBV_ACCESS_LOCAL_WRITE);
		if (!send_mr) {
			ret = -1;
			perror("rdma_reg_msgs for send_msg");
            return disconnect(out_dereg_recv);
			// goto out_dereg_recv;
		}
	}
    // print_buffers();

    // tty->print_cr("Server accepting connection ...");
	ret = rdma_accept(id, NULL);
	if (ret) {
		perror("rdma_accept");
        disconnect(out_dereg_send);
		// goto out_dereg_send;
	}
    print_buffers();

    // receive remote mr rkey and addr of client
	tty->print_cr("rdma_post_recv ...");
	ret = rdma_post_recv(id, NULL, recv_msg, 16, mr);
	if (ret) {
		perror("rdma_post_recv");
        disconnect(out_dereg_send);
		// goto out_dereg_send;
	}


	tty->print_cr("rdma_get_recv_comp ...");
	while ((ret = rdma_get_recv_comp(id, wc)) == 0);
	if (ret < 0) {
		perror("rdma_get_recv_comp");
        return disconnect(out_disconnect);
		// goto out_disconnect;
	}

	// // extract information

	memcpy(&rkey, recv_msg, sizeof(rkey));
	print_a_buffer((uint8_t*)&rkey, sizeof(rkey), (char*)"rkey");

	memcpy(&remote_addr, recv_msg + sizeof(rkey), sizeof(remote_addr));

	print_a_buffer((uint8_t*)&remote_addr, sizeof(remote_addr), (char*)"remote addr");

    // register a local buffer
	strcpy((char*)rw_buff, "Dat");
	
	print_a_buffer(rw_buff, BUFFER_SIZE, (char*)"rw_buff");
	// register remote region for rdma read and write
	rw_mr = ibv_reg_mr(id->pd, rw_buff, BUFFER_SIZE, IBV_ACCESS_LOCAL_WRITE);


    return ret;

}

int RemoteRegion::disconnect(DISCONNECT_CODE code) {
    switch (code){
        case out_disconnect:
            rdma_disconnect(id);
        case out_dereg_send:
            if ((send_flags & IBV_SEND_INLINE) == 0)
                ibv_dereg_mr(send_mr);
        case out_dereg_recv:
            ibv_dereg_mr(mr);
        case out_destroy_accept_ep:
            rdma_destroy_ep(id);
        case out_destroy_listen_ep:
            rdma_destroy_ep(*connection->listen_id());
        case out_free_addrinfo:
            connection->disconnect(out_free_addrinfo);

        default:
            return 0;
    }
}

int RemoteRegion::rdma_read(uint8_t* buffer, int length, int addr_offset) {
	int ret;
	
	tty->print_cr("RDMA read ...");

	memset(rw_buff, 0, sizeof(rw_buff));
	print_a_buffer(rw_buff, BUFFER_SIZE, (char*)"rw_buff");

	ret = rdma_post_read(id, NULL, rw_buff, BUFFER_SIZE, rw_mr, IBV_SEND_SIGNALED, (uint64_t)((uint8_t*)remote_addr+addr_offset), rkey);
	if (ret) {
		perror("Error rdma read");
		return disconnect(out_disconnect);
		// goto out_disconnect;
	}
	do {
		ret = ibv_poll_cq(id->send_cq, 1, wc);
	} while (ret == 0);

	print_a_buffer(rw_buff, BUFFER_SIZE, (char*)"rw_buff");

	get_wc_status(wc);

	return ret;	
}

int RemoteRegion::rdma_write(uint8_t* buffer, int length, int addr_offset) {
	int ret;

	tty->print_cr("RDMA write ...");

	memset(rw_buff, 0, sizeof(rw_buff));
	ret = rdma_post_write(id, NULL, rw_buff, BUFFER_SIZE, rw_mr, IBV_SEND_SIGNALED, (uint64_t)((uint8_t*)remote_addr+addr_offset), rkey);
	if (ret) {
		perror("Error rdma write");
		return disconnect(out_disconnect);
		// goto out_disconnect;
	}
	do {
		ret = ibv_poll_cq(id->send_cq, 1, wc);
	} while (ret == 0);

	
	print_a_buffer(rw_buff, BUFFER_SIZE, (char*)"rw_buff");

	get_wc_status(wc);

	return ret;	
}
