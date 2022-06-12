#include <runtime/rdma.hpp>


int RemoteRegion::initialize (char* local_add, char* port) {
    struct rdma_addrinfo hints, *res;
	struct ibv_qp_init_attr init_attr;
	struct ibv_qp_attr qp_attr;
	struct ibv_wc wc;
	int ret;
    void* temp;


    memset(&hints, 0, sizeof hints);
	hints.ai_flags = RAI_PASSIVE;
	hints.ai_port_space = RDMA_PS_TCP;
	ret = rdma_getaddrinfo(local_addr, port, &hints, &res);
	if (ret) {
		printf("rdma_getaddrinfo: %s\n", gai_strerror(ret));
		return ret;
	}

	memset(&init_attr, 0, sizeof init_attr);
	init_attr.cap.max_send_wr = init_attr.cap.max_recv_wr = 1;
	init_attr.cap.max_send_sge = init_attr.cap.max_recv_sge = 1;
	init_attr.cap.max_inline_data = 16;
	init_attr.sq_sig_all = 1;
	ret = rdma_create_ep(&listen_id, res, NULL, &init_attr);
	if (ret) {
		perror("rdma_create_ep");
		goto out_free_addrinfo;
	}
    printf("Server listening ...\n");
	ret = rdma_listen(listen_id, 0);
	if (ret) {
		perror("rdma_listen");
		goto out_destroy_listen_ep;
	}


    printf("Getting connection request ...\n");
	ret = rdma_get_request(listen_id, &id);
	if (ret) {
		perror("rdma_get_request");
		goto out_destroy_listen_ep;
	}

    print_buffers();

	memset(&qp_attr, 0, sizeof qp_attr);
	memset(&init_attr, 0, sizeof init_attr);
	ret = ibv_query_qp(id->qp, &qp_attr, IBV_QP_CAP,
			   &init_attr);
	if (ret) {
		perror("ibv_query_qp");
		goto out_destroy_accept_ep;
	}
	if (init_attr.cap.max_inline_data >= 16)
		send_flags = IBV_SEND_INLINE;
	else
		printf("rdma_server: device doesn't support IBV_SEND_INLINE, "
		       "using sge sends\n");

    printf("Registering mr ...\n");
	mr = rdma_reg_msgs(id, recv_msg, 16);
	if (!mr) {
		ret = -1;
		perror("rdma_reg_msgs for recv_msg");
		goto out_destroy_accept_ep;
	}
	if ((send_flags & IBV_SEND_INLINE) == 0) {
        printf("Registering send mr ...");
		send_mr = rdma_reg_msgs(id, send_msg, 16);
		if (!send_mr) {
			ret = -1;
			perror("rdma_reg_msgs for send_msg");
			goto out_dereg_recv;
		}
	}
    print_buffers();

    printf("Server accepting connection ...\n");
	ret = rdma_accept(id, NULL);
	if (ret) {
		perror("rdma_accept");
		goto out_dereg_send;
	}
    print_buffers();

    // receive remote mr rkey and addr of client
	printf("rdma_post_recv ...\n");
	ret = rdma_post_recv(id, NULL, recv_msg, 16, mr);
	if (ret) {
		perror("rdma_post_recv");
		goto out_dereg_send;
	}

    print_buffers();

	printf("rdma_get_recv_comp ...\n");
	while ((ret = rdma_get_recv_comp(id, &wc)) == 0);
	if (ret < 0) {
		perror("rdma_get_recv_comp");
		goto out_disconnect;
	}
    print_buffers();

	// extract information

	memcpy(&rkey, recv_msg, sizeof(rkey));
	print_a_buffer((uint8_t*)&rkey, sizeof(rkey), (char*)"rkey");

	memcpy(&remote_addr, recv_msg + sizeof(rkey), sizeof(remote_addr));

	print_a_buffer((uint8_t*)&remote_addr, sizeof(remote_addr), (char*)"remote addr");

    // register a local buffer
	strcpy((char*)rw_buff, "Dat");
	
	print_a_buffer(rw_buff, BUFFER_SIZE, (char*)"rw_buff");
	rw_mr = ibv_reg_mr(id->pd, rw_buff, BUFFER_SIZE, IBV_ACCESS_LOCAL_WRITE);


out_disconnect:
	rdma_disconnect(id);
out_dereg_send:
	if ((send_flags & IBV_SEND_INLINE) == 0)
		rdma_dereg_mr(send_mr);
out_dereg_recv:
	rdma_dereg_mr(mr);
out_destroy_accept_ep:
	rdma_destroy_ep(id);
out_destroy_listen_ep:
	rdma_destroy_ep(listen_id);
out_free_addrinfo:
	rdma_freeaddrinfo(res);
out:
    return ret;

}