/* 
 * Compile Command:
 * g++ remote_node.cc -o remote_node.exe -libverbs -lrdmacm -pthread
 * 
 * 
 * Run remote code first, then local code
 * 
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include <netdb.h>
#include <rdma/rdma_cma.h>
#include <rdma/rdma_verbs.h>


static const char *local_addr;
static const char *port ;
static int rm_size;

static struct rdma_cm_id *listen_id, *id;
static struct ibv_mr *mr, *send_mr, *rdma_mr, *local_mr;
static int send_flags;

#define BUFFER_SIZE 16
static uint8_t send_msg[BUFFER_SIZE];
static uint8_t recv_msg[BUFFER_SIZE];

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

static int remote_node_run()
{
    struct rdma_addrinfo hints, *res;
	struct ibv_qp_init_attr init_attr;
	struct ibv_qp_attr qp_attr;
	struct ibv_wc wc;
	int ret;
    void* temp;

    static uint8_t* rdma_buff = (uint8_t*)malloc(rm_size * sizeof(uint8_t));

    print_buffers();

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
        printf("Registering mr ...");
		send_mr = rdma_reg_msgs(id, send_msg, 16);
		if (!send_mr) {
			ret = -1;
			perror("rdma_reg_msgs for send_msg");
			goto out_dereg_recv;
		}
	}
    print_buffers();

    printf("rdma_post_recv ...\n");
	ret = rdma_post_recv(id, NULL, recv_msg, 16, mr);
	if (ret) {
		perror("rdma_post_recv");
		goto out_dereg_send;
	}

    print_buffers();

    
    printf("Server accepting connection ...\n");
	ret = rdma_accept(id, NULL);
	if (ret) {
		perror("rdma_accept");
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

    printf("rdma_post_send ...\n");
	ret = rdma_post_send(id, NULL, send_msg, 16, send_mr, send_flags);
	if (ret) {
		perror("rdma_post_send");
		goto out_disconnect;
	}
    print_buffers();

    printf("rdma_get_send_comp ...\n");
	while ((ret = rdma_get_send_comp(id, &wc)) == 0);
	if (ret < 0)
		perror("rdma_get_send_comp");
	else
		ret = 0;

    print_buffers();

    // ------------------------------------
    // new code here

    // Create new mr for RDMA read and write to this server
	rdma_mr = ibv_reg_mr(id->pd, rdma_buff, rm_size, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ);
	
	// copy mr remote key and addr to send buffer to send to server
	print_a_buffer((uint8_t*)&rdma_mr->rkey, sizeof(rdma_mr->rkey), (char*)"rkey");
	print_a_buffer((uint8_t*)&rdma_mr->addr, sizeof(rdma_mr->addr), (char*)"addr");
	temp = memccpy(send_msg, &rdma_mr->rkey, '\0', sizeof(rdma_mr->rkey));
	temp = memcpy(temp, &rdma_mr->addr, sizeof(rdma_mr->addr));

	print_buffers();

	// now send buffer to server
	ret = rdma_post_send(id, NULL, send_msg, 16, send_mr, send_flags);
	if (ret) {
		perror("rdma_post_send");
		goto out_dereg_rdma_mr;
	}

	while ((ret = rdma_get_send_comp(id, &wc)) == 0);
	if (ret < 0) {
		perror("rdma_get_send_comp");
		goto out_dereg_rdma_mr;
	}

	// Post a recv and wait for signal from server to exit
	ret = rdma_post_recv(id, NULL, recv_msg, 16, mr);
	if (ret) {
		perror("rdma_post_recv");
		goto out_dereg_send;
	}

	while ((ret = rdma_get_recv_comp(id, &wc)) == 0);
	if (ret < 0)
		perror("rdma_get_recv_comp");
	else
		ret = 0;

out_dereg_rdma_mr:
	rdma_dereg_mr(rdma_mr);
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

int main (int argc, char** argv) {
    int ret, op;
    int is_server = 0;

    while ((op = getopt(argc, argv, "a:p:s:")) != -1) {
		switch (op) {
		// case 's':
		// 	is_server = 1;
		// 	break;
        case 'a':
            local_addr = optarg;
            break;
		case 'p':
			port = optarg;
			break;
		case 's':
			rm_size = 1<<atoi(optarg);
			break;
		default:
			printf("usage: %s\n", argv[0]);
			printf("\t[-a server_addr]\n");
			printf("\t[-p port_number]\n");
			printf("\t[-s size_of_remote_mem_shift]\n");
			exit(1);
		}
	}

    printf("Address of local node %s\n", local_addr);
    printf("Port of local node %s\n", port);
    printf("Size of Remote Memory %d\n", rm_size);

    // if (is_server) {
    //     printf("rdma_server: start\n");
    //     ret = server_run();
    //     printf("rdma_server: end %d\n", ret);
    // } else {
    //     printf("rdma_client: start\n");
    //     ret = client_run();
    //     printf("rdma_client: end %d\n", ret);
        
    // }

    remote_node_run();
}