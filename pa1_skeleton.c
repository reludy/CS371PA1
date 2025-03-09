/*
# Copyright 2025 University of Kentucky
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#	  http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# SPDX-License-Identifier: Apache-2.0
*/

/* 
Please specify the group members here

# Student #1: Abhinav Jha (Individual work)

*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <pthread.h>
#include <fcntl.h>
#include <errno.h>

#define MAX_EVENTS 64
#define MESSAGE_SIZE 16
#define DEFAULT_CLIENT_THREADS 4

char *server_ip = "127.0.0.1";
int server_port = 12345;
int num_client_threads = DEFAULT_CLIENT_THREADS;
int num_requests = 1000000;

/*
 * This structure is used to store per-thread data in the client
 */
typedef struct {
	int epoll_fd;		/* File descriptor for the epoll instance, used for monitoring events on the socket. */
	int socket_fd;	   /* File descriptor for the client socket connected to the server. */
	long long total_rtt; /* Accumulated Round-Trip Time (RTT) for all messages sent and received (in microseconds). */
	long total_messages; /* Total number of messages sent and received. */
	float request_rate;  /* Computed request rate (requests per second) based on RTT and total messages. */
} client_thread_data_t;

/* Helper function to set socket to non-blocking mode (not mission critical)*/
static int set_nonblocking(int fd) {
	int flags;
	if ((flags = fcntl(fd, F_GETFL, 0)) < 0) {
		perror("fcntl(F_GETFL)");
		return -1;
	}
	if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
		perror("fcntl(F_SETFL)");
		return -1;
	}
	return 0;
}

/*
 * This function runs in a separate client thread to handle communication with the server
 */
void *client_thread_func(void *arg) {
	client_thread_data_t *data = (client_thread_data_t *)arg;
	struct epoll_event event, events[MAX_EVENTS];
	char send_buf[MESSAGE_SIZE] = "ABCDEFGHIJKMLNOP"; /* Send 16-Bytes message every time */
	char recv_buf[MESSAGE_SIZE];
	struct timeval start, end;

	// Hint 1: register the "connected" client_thread's socket in the its epoll instance
	event.events = EPOLLIN;
	event.data.fd = data->socket_fd;
	if (epoll_ctl(data->epoll_fd, EPOLL_CTL_ADD, data->socket_fd, &event) < 0) {
		perror("epoll_ctl: client socket");
		pthread_exit(NULL);
	}

	/* TODO:
	 * It sends messages to the server, waits for a response using epoll,
	 * and measures the round-trip time (RTT) of this request-response.
	 */
	for (int i = 0; i < num_requests; i++) {
		// Hint 2: use gettimeofday() and "struct timeval start, end" to record timestamp, which can be used to calculated RTT.
		gettimeofday(&start, NULL);
		// Send a 16-byte message to the server
		ssize_t sent = send(data->socket_fd, send_buf, MESSAGE_SIZE, 0);
		if (sent < 0) {
			perror("send");
			break;
		}

		// Wait for the server's response using epoll
		while (1) {
			int n = epoll_wait(data->epoll_fd, events, MAX_EVENTS, -1);
			if (n < 0 && errno != EINTR) {
				perror("epoll_wait");
				break;
			}
			if (n < 0) continue;  // keep trying if interrupted by signal

			for (int j = 0; j < n; j++) {
				if (events[j].data.fd == data->socket_fd && (events[j].events & EPOLLIN)) {
					// Try to read the echoed message
					ssize_t recvd = recv(data->socket_fd, recv_buf, MESSAGE_SIZE, 0);
					if (recvd > 0) {
						// Record time after receiving the echo
						gettimeofday(&end, NULL);

						// RTT in microseconds
						long long start_us = (long long)start.tv_sec * 1000000LL + start.tv_usec;
						long long end_us   = (long long)end.tv_sec   * 1000000LL + end.tv_usec;
						long long rtt  	= end_us - start_us; 

						data->total_rtt	+= rtt;
						data->total_messages++;
					}
					// Break and send next request, or finish.
					goto next_request;
				}
			}
		}
next_request:;
	}
 
	/* TODO:
	 * The function exits after sending and receiving a predefined number of messages (num_requests). 
	 * It calculates the request rate based on total messages and RTT
	 */
	 
	// approx total time = data->total_rtt (in us)
	// request_rate = total_messages / (total_time_in_seconds)
	// total_time_in_seconds = data->total_rtt / 1e6

	if (data->total_messages > 0) {
		double total_time_s = (double)(data->total_rtt) / 1000000.0;
		if (total_time_s > 0) {
			data->request_rate = data->total_messages / total_time_s;
		}
	}

	// Close the socket and epoll
	close(data->socket_fd);
	close(data->epoll_fd);

	return NULL;
}

/*
 * This function orchestrates multiple client threads to send requests to a server,
 * collect performance data of each threads, and compute aggregated metrics of all threads.
 */
void run_client() {
	pthread_t threads[num_client_threads];
	client_thread_data_t thread_data[num_client_threads];
	struct sockaddr_in server_addr;
		
	memset(&server_addr,0,sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(server_port);
	server_addr.sin_addr.s_addr = inet_addr(server_ip);

	/* TODO:
	 * Create sockets and epoll instances for client threads
	 * and connect these sockets of client threads to the server
	 */
	for (int i = 0; i < num_client_threads; i++) {
		// Create socket
		int sock_fd = socket(AF_INET,SOCK_STREAM,0);
		if (sock_fd < 0) {
			perror("socket");
			exit(EXIT_FAILURE);
		}

		// Connect to the server
		if (connect(sock_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
			perror("connect");
			close(sock_fd);
			exit(EXIT_FAILURE);
		}

		// Make socket non-blocking
		set_nonblocking(sock_fd);

		// Create epoll instance for this thread
		int ep_fd = epoll_create1(0);
		if (ep_fd < 0) {
			perror("epoll_create1");
			close(sock_fd);
			exit(EXIT_FAILURE);
		}

		// Initialize thread data
		thread_data[i].epoll_fd = ep_fd;
		thread_data[i].socket_fd = sock_fd;
		thread_data[i].total_rtt = 0LL;
		thread_data[i].total_messages = 0;
		thread_data[i].request_rate = 0.0f;
	}

	// Launch client threads
	for (int i = 0; i < num_client_threads; i++) {
		pthread_create(&threads[i], NULL, client_thread_func, &thread_data[i]);
	}

	/* TODO:
	 * Wait for client threads to complete and aggregate metrics of all client threads
	 */
	long long total_rtt_all = 0;
	long long total_messages_all = 0;
	float total_request_rate_all = 0.0f;

	for (int i = 0; i < num_client_threads; i++) {
		pthread_join(threads[i], NULL);
		total_rtt_all += thread_data[i].total_rtt;
		total_messages_all += thread_data[i].total_messages;
		total_request_rate_all += thread_data[i].request_rate;
	}

	long long avg_rtt_us = 0;
	if (total_messages_all > 0) {
		avg_rtt_us = total_rtt_all / total_messages_all;
	}

	printf("Average RTT: %lld us\n", avg_rtt_us);
	printf("Total Request Rate: %f messages/s\n", total_request_rate_all);
}

void run_server() {
	
	/* TODO:
	 * Server creates listening socket and epoll instance.
	 * Server registers the listening socket to epoll
	 */ 
	 
	int listen_fd, epoll_fd;
	struct sockaddr_in server_addr;
	struct epoll_event event, events[MAX_EVENTS];

	// Create listening socket
	listen_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (listen_fd < 0) {
		perror("socket");
		exit(EXIT_FAILURE);
	}

	int opt = 1;
	setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family  = AF_INET;
	server_addr.sin_port = htons(server_port);
	server_addr.sin_addr.s_addr = inet_addr(server_ip);

	// Bind and listen
	if (bind(listen_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
		perror("bind");
		close(listen_fd);
		exit(EXIT_FAILURE);
	}

	if (listen(listen_fd, SOMAXCONN) < 0) {
		perror("listen");
		close(listen_fd);
		exit(EXIT_FAILURE);
	}

	// Create epoll
	epoll_fd = epoll_create1(0);
	if (epoll_fd < 0) {
		perror("epoll_create1");
		close(listen_fd);
		exit(EXIT_FAILURE);
	}

	// Make listening socket non-blocking
	set_nonblocking(listen_fd);

	// Register the listening socket in epoll
	event.events  = EPOLLIN;
	event.data.fd = listen_fd;
	if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_fd, &event) < 0) {
		perror("epoll_ctl: listen_fd");
		close(listen_fd);
		close(epoll_fd);
		exit(EXIT_FAILURE);
	}

	printf("Server listening on %s:%d\n", server_ip, server_port);

	/* Server's run-to-completion event loop */
	while (1) {
		/* TODO:
		 * Server uses epoll to handle connection establishment with clients
		 * or receive the message from clients and echo the message back
		 */		
		int n = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
		if (n < 0 && errno != EINTR) {
			perror("epoll_wait");
			break;
		}
		if (n < 0) continue; // retry if interrupted by signal

		for (int i = 0; i < n; i++) {
			int fd = events[i].data.fd;

			// Check if it's the listening socket and accept new client
			if (fd == listen_fd) {
				// Accept as many connections as possible
				while (1) {
					struct sockaddr_in client_addr;
					socklen_t client_len = sizeof(client_addr);
					int conn_fd = accept(listen_fd, (struct sockaddr*)&client_addr, &client_len);
					if (conn_fd < 0) {
						if (errno == EAGAIN || errno == EWOULDBLOCK) {
							// If no more connections to accept
							break;
						} else {
							perror("accept");
						}
						continue; // Retry accepting other connections (unsure if this is the correct implementation)
					}

					// Make connection socket non-blocking and add it to epoll
					set_nonblocking(conn_fd);
					struct epoll_event conn_event;
					conn_event.events  = EPOLLIN;
					conn_event.data.fd = conn_fd;
					if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, conn_fd, &conn_event) < 0) {
						perror("epoll_ctl: conn_fd");
						close(conn_fd);
					}
				}
			}
			// Else, it's client socket data
			else {
				if (events[i].events & EPOLLIN) {
					char buffer[MESSAGE_SIZE];
					ssize_t recvd = recv(fd, buffer, MESSAGE_SIZE, 0);

					if (recvd < 0) {
						if (errno != EAGAIN && errno != EWOULDBLOCK) {
							perror("recv");
							close(fd);
						}
					} else if (recvd == 0) {
						// Client disconnected
						close(fd);
					} else {
						// Echo back the same data
						ssize_t sent = send(fd, buffer, recvd, 0);
						if (sent < 0) {
							perror("send");
							close(fd);
						}
					}
				}
			}
		}
	}

	close(listen_fd);
	close(epoll_fd);
}

int main(int argc, char *argv[]) {
	if (argc > 1 && strcmp(argv[1], "server") == 0) {
		if (argc > 2) server_ip = argv[2];
		if (argc > 3) server_port = atoi(argv[3]);

		run_server();
	} else if (argc > 1 && strcmp(argv[1], "client") == 0) {
		if (argc > 2) server_ip = argv[2];
		if (argc > 3) server_port = atoi(argv[3]);
		if (argc > 4) num_client_threads = atoi(argv[4]);
		if (argc > 5) num_requests = atoi(argv[5]);

		run_client();
	} else {
		printf("Usage: %s <server|client> [server_ip server_port num_client_threads num_requests]\n", argv[0]);
	}

	return 0;
}
