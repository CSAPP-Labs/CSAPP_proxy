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
 * normally. 
 *
 * The proxy's general structure is that of a server. It sets up 
 * the string buffers, socket address structures, and I/O buffers
 * to prepare for communication. It opens and sets up a listening 
 * file descriptor and begins iterating for incoming client 
 * connections. The accept() functionality blocks via syscall 
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
 * servicing another client. 
 *
 * Issue: need to detect entity-body; need to distinguish text
 * from binary data; need to copy such data (like images/videos)
 * from the server to the client.
 *
 * Implementing POST and HEAD is optional.
 *
 * Part II
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
 */

#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
// static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
// static const char *user_agent_hdr_alt = "User-Agent: Mozilla/5.0 (X11; Ubuntu; Linux x86_64; rv:84.0) Gecko/20100101 Firefox/84.0\r\n";
static char user_agent_hdr_alt[MAXLINE];

int readparse_request(int fd, char *targethost, char *path, char *request_toserver, char *port, char *method, rio_t *rp);
void parse_url(char *url, char *host, char *abs_path, char *port);
void forward_requesthdrs(rio_t *rp, int fd);
void send_request(int server_connfd, char *request_toserver, char *targethost);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);
void send_r_and_h(int server_connfd, char *request_toserver, char *targethost, rio_t *rio_client); 

/* $begin main */
int main(int argc, char **argv)
{

    int listenfd, client_connfd, server_connfd, rio_cnt;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    rio_t rio_client, rio_toserver; /* or initiate new rio struct in each iteration? */
    char hostname[MAXLINE], port[MAXLINE], targethost[MAXLINE], path[MAXLINE], 
    			request_toserver[MAXLINE], server_buf[MAXLINE], server_port[6],
    			request_method[64];

    /* initialize user agent without \r\n and header title of "User-Agent" */
	strcpy(user_agent_hdr_alt, "Mozilla/5.0 (X11; Ubuntu; Linux x86_64; rv:84.0) Gecko/20100101 Firefox/84.0");

	/* ignore SIGPIPE signals by flagging it with SIG_IGN */
	Signal(SIGPIPE, SIG_IGN);

	/* Check command line args */
    if (argc != 2) {
		fprintf(stderr, "usage: %s <port>\n", argv[0]);
		exit(1);
    }

    /* sequential proxy: waits for contact by client, services a request, closes connection */
    listenfd = Open_listenfd(argv[1]); /* port passed to proxy at cmdline argv[1]; exit if error */
    while (1) {
    	/* accept incoming connections */
    	clientlen = sizeof(clientaddr);
    	if ((client_connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen)) < 0) {
    		printf("Couldn't connect to client.\n"); 
    		continue;
    	}

    	/* obtain client info; not necessary for basic proxy tasks */
        Getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE, 
                    port, MAXLINE, 0);
        printf("PROXY: Accepted connection from client (%s, %s)\n", hostname, port);

        /* read request; extract the host/path/port requested by client 
         * set up the client-facing I/O buffer from rio package 
         * ignore requests which are not implemented.
         */
    	if  (readparse_request(client_connfd, targethost, path, request_toserver, 
        			server_port, request_method, &rio_client) < 0) 
    		continue;

        /* proxy performs a client role: connect to the server */
		if ((server_connfd = Open_clientfd(targethost, server_port)) < 0)
			continue;

		/* send method+resource+version request + mandatory headers, 
		 * then forward request headers from client line by line */
		// send_request(server_connfd, request_toserver, targethost);
		// forward_requesthdrs(&rio_client, server_connfd); 

	    /* send request, check and modify mandatory headers then send all headers to server */ 
	    send_r_and_h(server_connfd, request_toserver, targethost, &rio_client);

        /* set up rio buffer to read server responses */
        Rio_readinitb(&rio_toserver, server_connfd); /* proxy can only start reading server responses here */
        if ( (rio_cnt = Rio_readlineb(&rio_toserver, server_buf, MAXLINE)) != 0 ) { /* status code from server */
	        printf("Server response status: \n");
	        printf("%s\r\n\r\n",server_buf);
	        Rio_writen(client_connfd, server_buf, strlen(server_buf));
    	}

    	/* also need to copy binaries, images and videos from server to client. 
    	 * need to decide based on URL which resources need to be served, what
    	 * the appropriate filetype is, when the server is done sending headers,
    	 * and when it started sending a stream of binary bytes; to be copied
    	 * via mmap()
    	 */

    	/* write server responses to client */
        while ( (rio_cnt = Rio_readlineb(&rio_toserver, server_buf, MAXLINE)) != 0 ) {
        	// printf("%s", server_buf);
        	Rio_writen(client_connfd, server_buf, strlen(server_buf));

        	/* should detect HEAD method and send only the response headers to client*/
        	// if (strstr(request_method, "HEAD") && ((strstr(server_buf, "\r\n\r\n"))) )
        	// 	break;
        }

        /* close socket with server, then close socket with client */
        Close(server_connfd);
    	Close(client_connfd);
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
int readparse_request(int fd, char *targethost, char *path, char *request_toserver, char *port, char *request_method, rio_t *rp)
{
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];

    /* clear buffers */
    strcpy(request_toserver, ""); strcpy(path, ""); strcpy(targethost, ""); strcpy(port, ""); strcpy(request_method, "");

    /* Read request line and headers */
    Rio_readinitb(rp, fd);
    if (!Rio_readlineb(rp, buf, MAXLINE))  
        return 0;
    sscanf(buf, "%s %s %s", method, uri, version);  
    printf("PROXY: Request of method [%s] received from client:\n%s", method, buf);    
    if (/*strcasecmp(method, "GET") != 0*/ !strstr(method,"GET")) {   
        clienterror(fd, method, "501", "Not Implemented",
                    "Method not implemented by Web Proxy");
        printf("PROXY: Request of method [%s] not implemented; ignored.\n", method);    
        return -1;
    }                                                   

    parse_url(uri, targethost, path, port);
    strcpy(version, "HTTP/1.0");     /* proxy version always sends HTTP/1.0 */
    strcpy(request_method, method);

    /* build the request line */
    sprintf(request_toserver, "%s %s %s", method, path, version); /* may need terminating with "\r\n" */
    return 0;
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
	if (strchr(scheme, 's')) { /* https */
		printf("PROXY: ERROR: can't serve https requests. \n");
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
    // strcpy(abs_path, "");
    strcat(abs_path, suffix); // printf("h: %s| pt: %s | ph: %s\n", host, port, abs_path);
    return;
}
/* $end parse_url */

/*
 * forward_requesthdrs - read HTTP request headers
 */
/* $begin forward_requesthdrs */
void forward_requesthdrs(rio_t *rp, int fd) 
{
    char buf[MAXLINE];
    printf("Forwarding the following request headers from client: \n");
    do
    {
        Rio_readlineb(rp, buf, MAXLINE);
        Rio_writen(fd, buf, strlen(buf));
        printf("%s", buf);

        /* potential intercession for non-GET requests would go here */

    } while (strcmp(buf, "\r\n")); /* should detect the end of the request as defined by client */

    return;
}
/* $end forward_requesthdrs */

/*
 * send_request - sends request and conventional request headers to server
 * line by line
 */
/* $begin send_request */
void send_request(int server_connfd, char *request_toserver, char *targethost) 
{
	char buf[MAXLINE];

	sprintf(buf, "%s\r\n", request_toserver); printf("%s", buf);
    Rio_writen(server_connfd, buf, strlen(buf));

    printf("Sending the following request headers by proxy: \n");
    /* correction needed: find the host header that the browser uses, and use that one */
    sprintf(buf, "Host: %s\r\n", targethost); printf("%s", buf);
    Rio_writen(server_connfd, buf, strlen(buf));
    sprintf(buf, "User-Agent: %s\r\n", user_agent_hdr_alt); printf("%s", buf);
    Rio_writen(server_connfd, buf, strlen(buf));
    sprintf(buf, "Connection: %s\r\n", "close"); printf("%s", buf);
    Rio_writen(server_connfd, buf, strlen(buf));
    sprintf(buf, "Proxy-Connection: %s\r\n", "close"); printf("%s", buf);
    Rio_writen(server_connfd, buf, strlen(buf)); 

    return;
}
/* $end send_request */


/*
 * send_r_and_h - sends request + conventional headers to server, then
 * forwards client-side requests, in two blocks.
 * Maintains client preference for the "Host" value. 
 * Does not preserve the ordering of the headers sent.
 *
 * RFC2616: the order in which header fields with differing field names are
 * received is not significant. Good practice: send general headers, then 
 * request/response headers, then entity headers.
 * However, the order of headers matters when there are multiple headers
 * with the same name.
 */
/* $begin send_r_and_h */
void send_r_and_h(int server_connfd, char *request_toserver, char *targethost, rio_t *rio_client) 
{
	char buf[MAXLINE], buf_client[MAXLINE], proxy_toserver[MAXLINE], client_toserver[MAXLINE], 
				overriden_host[MAXLINE], overriden_agent[MAXLINE];

	strcpy(client_toserver, "");

    /* send method request built by proxy, to server */
	sprintf(buf, "%s\r\n", request_toserver); printf("%s", buf);
    Rio_writen(server_connfd, buf, strlen(buf));

    /* obtain Host and User-Agent info from client; ignore Connection and 
     * Proxy-Connection from client and write "close". build+send all headers
     */
    do
    {
        Rio_readlineb(rio_client, buf_client, MAXLINE);
    	if (strstr(buf_client, "Host:")) {
    		sscanf(buf_client, "Host: %s", overriden_host);
    		strcpy(targethost, overriden_host);
    	} else if (strstr(buf_client, "User-Agent:")) { /* disabled */
    		// sscanf(buf_client, "User-Agent: %s", overriden_agent);
    		// strcpy(user_agent_hdr_alt, overriden_agent);
    	} else if (strstr(buf_client, "Connection:") || strstr(buf_client, "Proxy-")) {
    		continue;
    	} else {
    		/* build the content to be sent to server from client unaltered */
    		sprintf(client_toserver, "%s%s", client_toserver, buf_client);
    	}

        /* potential intercession for non-GET requests would go here */

    } while (strcmp(buf_client, "\r\n")); 
    
    strcat(client_toserver, "\r\n");

    /* override mandatory proxy headers where necessary */
    sprintf(proxy_toserver, "Host: %s\r\n", targethost);
    sprintf(proxy_toserver, "%sUser-Agent: %s\r\n", proxy_toserver, user_agent_hdr_alt); 
    sprintf(proxy_toserver, "%sConnection: close\r\n", proxy_toserver); 
    sprintf(proxy_toserver, "%sProxy-Connection: close\r\n", proxy_toserver); 
    
    printf("Request headers built by proxy, to server: \n");
    printf("%s", proxy_toserver);
    printf("Request headers forwarded from client, to server: \n");
    printf("%s", client_toserver);

    /* send mandatory headers by proxy, forward the rest from client. */   
    Rio_writen(server_connfd, proxy_toserver, strlen(proxy_toserver));
    Rio_writen(server_connfd, client_toserver, strlen(client_toserver)); /* terminate with \r\n\r\n */ 

    return;
}
/* $end send_r_and_h */

/*
 * clienterror - returns an error message to the CLIENT
 */
/* $begin clienterror */
void clienterror(int fd, char *cause, char *errnum, 
		 char *shortmsg, char *longmsg) 
{
    char buf[MAXLINE];

    /* Print the HTTP response headers */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n\r\n");
    Rio_writen(fd, buf, strlen(buf));

    /* Print the HTTP response body */
    sprintf(buf, "<html><title>Proxy Error</title>");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<body bgcolor=""ffffff"">\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "%s: %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<p>%s: %s\r\n", longmsg, cause);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<hr><em>The Web Proxy</em>\r\n");
    Rio_writen(fd, buf, strlen(buf));
}
/* $end clienterror */

