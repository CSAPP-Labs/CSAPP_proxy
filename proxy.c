/* Web Proxy CS-APP lab
 *
 * The project consists of three parts.
 * Part I: implementing a sequential web proxy
 * Part II: upgrade the proxy to deal with multiple concurrent 
 * connections
 * Part III: add caching to the proxy using a simple main memory 
 * cache of recently accessed web content
 * 
 * General discussion
 * 
 * Part I (implemented)
 * For full testing via browser, the browser preferences would need
 * to be set to accessing the internet via localhost <selected port>
 * for http style communications; https would access the internet
 * normally. The selected port is 3000.
 *
 * The proxy's general structure is that of a server. It sets up 
 * the string buffers, socket address structures, and I/O buffers
 * to prepare for communication. It opens and sets up a listening 
 * file descriptor and begins iterating for incoming connection 
 * requests. Thus, each iteration services a request from some 
 * client, and requests from separate clients could be interleaved
 * for the duration of the proxy's operation. Attempts by multiple
 * clients to access an Internet resource are not queued by the 
 * proxy. The accept() functionality pauses the proxy via syscall 
 * until a valid client tries to connect with the proxy, at which
 * point a connected file descriptor is returned for I/O. Once 
 * this is done, the proxy reads incoming lines from the client
 * via the Robust I/O (rio) services, setting up the rio buffer 
 * and extracting elements of a GET request. This information
 * is parsed such that the proxy knows which resources the client
 * is looking for, and so the proxy opens a connection with the 
 * target server. It first sends the GET request and conventional 
 * headers to the server as formatted by proxy, then forwards the
 * remaining headers read from the client, to the target server. 
 * The proxy then initializes a rio buffer with the server, 
 * reading its responses and writing them to the socket fd of the
 * client. Finally, the proxy closes both the client and the 
 * server fd and moves on to the next iteration, sequentially
 * servicing another connection request. 
 *
 *
 * Implementing POST and HEAD is optional.
 *
 * Part II: process-based concurrency (implemented)
 * A new process is spawned for each connection request accepted
 * by the proxy. This may result in a large number of child 
 * processes if there are many incoming connection requests.
 *
 * Part II: thread-based concurrency 
 * 
 * Part III
 * For testing, browser caching should be disabled. For firefox, 
 * type "about:config" in a new tab, search for 
 * network.http.use-cache and toggle from true to false.
 * 
 */

/* Recommended http sites for testing */
/*
 * http://csapp.cs.cmu.edu/3e/home.html
 * http://eu.httpbin.org/
 * http://neverssl.com/
 * http://www.testingmcafeesites.com/
 *
 * http://http://www.apimages.com/
 *
 */

#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
// static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *user_agent_hdr_alt = "Mozilla/5.0 (X11; Ubuntu; Linux x86_64; rv:84.0) Gecko/20100101 Firefox/84.0";
static const char *accept_header = "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8";
static const char *accept_encoding_header = "gzip, deflate";

/* HTTP functionality */
int readparse_request(int fd, char *targethost, char *path, char *port, char *method, 
	char *request_toserver, rio_t *rp);
void parse_url(char *url, char *host, char *abs_path, char *port);
void send_request(int server_connfd, char *request_toserver, char *targethost, rio_t *rio_client);
void forward_response(rio_t *rio_server, rio_t *rio_client, int server_connfd, int client_connfd);

/* informational */
void debug_status(rio_t *rp, int client_connfd);
void identify_client(const struct sockaddr *sa, socklen_t clientlen);

/* process concurrency */
void sigchld_handler(int sig);


/* $begin main */
int main(int argc, char **argv)
{
	pid_t pid;
    int listenfd, client_connfd, server_connfd, rio_cnt, content_length;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    rio_t rio_client, rio_server; 
    char targethost[MAXLINE], path[MAXLINE], 
    			request_toserver[MAXLINE], server_buf[MAXLINE], server_port[8],
    			request_method[64];

	/* Check command line args */
    if (argc != 2) {
		fprintf(stderr, "usage: %s <port>\n", argv[0]);
		exit(1);
    }

	/* ignore SIGPIPE signals */
	Signal(SIGPIPE, SIG_IGN);

	/* reap finished child processes doing work for clients without waiting */
	Signal(SIGCHLD, sigchld_handler);	

    /* concurrent process-based proxy: spawns a child process for each valid client connection */
    listenfd = Open_listenfd(argv[1]); /* exit if cmdline port invalid */
    while (1) {

    	/* accept incoming connections */
    	clientlen = sizeof(clientaddr);
    	if ((client_connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen)) < 0) {
    		printf("Couldn't connect to client.\n"); 
    		continue;
    	}

    	/* spawn a child process for each valid connection request; terminate when done */
    	if ( (pid = Fork()) == 0) {
	    	/* debugging, obtain client info; not necessary for basic proxy tasks */
	    	identify_client((SA *) &clientaddr, clientlen);

	        /* set up the client-facing I/O buffer from rio package; extract the host/path/port requested by client */
	    	if  (readparse_request(client_connfd, targethost, path, 
	        			server_port, request_method, request_toserver, &rio_client) < 0) 
	    		continue; /* move on to next request if unsuccessful */

	        /* proxy performs a client role: connect to the server (targethost) */
			if ((server_connfd = Open_clientfd(targethost, server_port)) < 0)
				continue; /* move on to next request if unsuccessful */

		    /* send request, check and modify mandatory headers then send all headers to server */  
		    send_request(server_connfd, request_toserver, targethost, &rio_client);

		    /* set up server-facing I/O buffer; write server response to client */
		    forward_response(&rio_server, &rio_client, server_connfd, client_connfd);

	        Close(server_connfd);
	    	Close(client_connfd);
	    	exit(0);
	    }
	    // if (pid != 0) {printf("Process of pid [%d] spawned.\n", pid);} /* for examination of pid */
	    Close(client_connfd); /* causes non-terminating "close of bad descriptor" error */

    }

    Close(listenfd);

	return 0;
}
/* $end main */


/*
 * readparse_request - read and parse requests received from client 
 * Expected requests are according to rfc1945. Scheme is http, 
 * version HTTP/1.0. 
 */
/* $begin readparse_request */
int readparse_request(int fd, char *targethost, char *path, char *port, char *request_method, char *request_toserver, rio_t *rp)
{
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];

    /* clear caller buffers */
    strcpy(path, ""); strcpy(targethost, ""); strcpy(port, ""); strcpy(request_method, "");

    /* Read request line and headers */
    Rio_readinitb(rp, fd);
    if (!Rio_readlineb(rp, buf, MAXLINE))  
        return 0; /* nothing to read */

    sscanf(buf, "%s %s %s", method, uri, version);  
    printf("PROXY: Request of method [%s] received from client:\n%s", method, buf);    
    if (/*strcasecmp(method, "GET") != 0*/ !strstr(method,"GET")) {   
        printf("PROXY: Request of method [%s] not implemented; ignored.\n", method);    
        return -1;
    }                                                   
    parse_url(uri, targethost, path, port);
    strcpy(request_method, method);

    strcpy(request_toserver, ""); 
	sprintf(request_toserver, "%s %s HTTP/1.0\r\n", request_method, path);

    return 0; /* request extracted */
}
/* $end readparse_request */

/*
 * parse_url - parse URI into targethost and path
 * Expected URL format: 
 * http_URL       = "http:" "//" host [ ":" port ] [ abs_path ]       
 */
/* $begin parse_url */
void parse_url(char *url, char *host, char *abs_path, char *port) 
{
	/* authority may contain host:port ; suffix may contain path?query#fragment */
    char scheme[10], authority[MAXLINE], suffix[MAXLINE];
    char hostbuf[MAXLINE], portbuf[6];

	/* extract fields from URL */ 
    sscanf(url, "%[^:]%*[:/]%[^/]%s", scheme, authority, suffix); // printf("sc: %s| aut: %s | suf: %s\n", scheme, authority, suffix);

    /* check if valid request */
	if (!strstr(scheme, "http")) {
		printf("PROXY: ERROR: no http prefix. \n");
		exit(0);
		return;
	}

	/* extract port if specified; otherwise default TCP port=80 */
    if (strchr(authority, ':') != NULL) {	
	    sscanf(authority, "%[^:]:%s", hostbuf, portbuf);
    	strcpy(host, hostbuf);
		strcpy(port, portbuf);
    } else {
    	strcpy(host, authority);
    	strcpy(port, "80");
    }

    /* clear path, then formulate path */
    strcat(abs_path, suffix); 
    return;
}
/* $end parse_url */

/*
 * send_request - sends request + proxy headers + client headers
 * RFC2616: ordering of headers only matters if multiple headers of same name
 */
/* $begin send_request */
void send_request(int server_connfd, char *request_toserver, char *targethost, rio_t *rio_client) 
{
	char buf[MAXLINE], buf_client[MAXLINE], proxy_toserver[MAXLINE], client_toserver[MAXLINE];
	printf("Request sent by proxy, to client:\n%s\n", request_toserver);

    /* override client headers with proxy preference; overtake the rest */
    do
    {
        Rio_readlineb(rio_client, buf_client, MAXLINE);
    	if (strstr(buf_client, "Host:")) {
    		sscanf(buf_client, "Host: %s", targethost);
    	} else if (strstr(buf_client, "Connection:") || strstr(buf_client, "Proxy-") || 
    		strstr(buf_client, "Accept:") || strstr(buf_client, "Accept-En")) {
    		continue;
    	} else { /* build the content to be sent to server from client unaltered */
    		sprintf(client_toserver, "%s%s", client_toserver, buf_client);
    	}

        /* potential intercession for non-GET requests would go here */

    } while (strcmp(buf_client, "\r\n")); 
    
    /* build proxy headers */
    sprintf(proxy_toserver, "Host: %s\r\n", targethost);
    sprintf(proxy_toserver, "%sUser-Agent: %s\r\n", proxy_toserver, user_agent_hdr_alt); 
    sprintf(proxy_toserver, "%sAccept: %s\r\n", proxy_toserver, accept_header); 
    sprintf(proxy_toserver, "%sAccept-Encoding: %s\r\n", proxy_toserver, accept_encoding_header); 
    sprintf(proxy_toserver, "%sConnection: close\r\n", proxy_toserver); 
    sprintf(proxy_toserver, "%sProxy-Connection: close\r\n\r\n", proxy_toserver); /* terminate with \r\n\r\n */
    
    /* for debugging */
    printf("Request headers built by proxy, to server:\n%s", proxy_toserver);
    printf("Request headers forwarded from client, to server:\n%sEnd of headers.\n\n", client_toserver);

    /* send mandatory headers by proxy, then forward the rest from client. */   
    Rio_writen(server_connfd, request_toserver, strlen(request_toserver)); /* request */
    Rio_writen(server_connfd, proxy_toserver, strlen(proxy_toserver));
    Rio_writen(server_connfd, client_toserver, strlen(client_toserver));

    return;
}
/* $end send_request */

/*
 * forward_response - forward server's response to client
 */
/* $begin forward_response */
void forward_response(rio_t *rio_server, rio_t *rio_client, int server_connfd, int client_connfd)
{
	int rio_cnt;
	char server_buf[MAXLINE];

    /* set up rio buffer to read server responses */
    Rio_readinitb(rio_server, server_connfd); 
	// debug_status(rio_server, client_connfd); /* prints out server response headers and the binary data too */

	/* write server response to client */
    while ( (rio_cnt = Rio_readnb(rio_server, server_buf, MAXLINE)) != 0 ) {
    	Rio_writen(client_connfd, server_buf, rio_cnt); /* write text to client from server buffer */

    	/* determine end of headers, extract content type/length, service non-GET requests */
    	// if (!(strcmp(server_buf, "\r\n")) ) {}        	}
    }

    return;
}
/* $end forward_response */


/* debugging helpers */

void debug_status(rio_t *rp, int client_connfd)
{
	char server_buf[MAXLINE];
	int rio_cnt;

    if ( (rio_cnt = Rio_readnb(rp, server_buf, MAXLINE)) != 0 ) { /* status code from server */
        printf("Server response status (first response header): \n");
        printf("%s\r\n",server_buf);
        Rio_writen(client_connfd, server_buf, rio_cnt);
	}

}

void identify_client(const struct sockaddr *sa, socklen_t clientlen) 
{
	char hostname[MAXLINE], port[8];

    Getnameinfo(sa, clientlen, hostname, MAXLINE, 
            port, MAXLINE, 0);
    printf("PROXY: Accepted connection from client (%s, %s)\n", hostname, port);

    return;
}


/* process-based concurrency helpers */

/*
 * sigchild handler: reap client-servicing children inside a sigchild 
 * handler instead of explicitly waiting for them to terminate
 */
/* $begin handler */
void sigchld_handler(int sig)
{
    pid_t pid;
    int olderrno = errno;

    while ((pid = waitpid(-1, NULL, WNOHANG))>0)
        ;

    errno = olderrno;
}
/* $end handler */
