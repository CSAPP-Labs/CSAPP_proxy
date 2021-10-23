#ifndef __PROXY_H__
#define __PROXY_H__

#include "csapp.h"

/* Header strings */
static const char *user_agent_hdr_alt = "Mozilla/5.0 (X11; Ubuntu; Linux x86_64; rv:84.0) Gecko/20100101 Firefox/84.0";
static const char *accept_header = "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8";
static const char *accept_encoding_header = "gzip, deflate";

/* HTTP functionality */
int readparse_request(int fd, char *targethost, char *path, char *port, char *method, 
	char *request_toserver, rio_t *rp, char *url_ptr);
void parse_url(char *url, char *host, char *abs_path, char *port);
void send_request(int server_connfd, char *request_toserver, char *targethost, 
	rio_t *rio_client);
void forward_response(rio_t *rio_server, rio_t *rio_client, int server_connfd, 
	int client_connfd, char *url_ptr);

/* debugging */
void debug_status(rio_t *rp, int client_connfd);
void identify_client(const struct sockaddr *sa, socklen_t clientlen);

/* thread-based concurrency functionality */
void *thread(void *vargp);


#endif /* __PROXY_H__ */
