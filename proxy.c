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
 * until a valid client tries to connect with the proxy. Once 
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
 * Issues: the proxy currently drafts its own mandatory request 
 * headers of Host, User-Agent, Connection, Proxy-Connection, and
 * then forwards all of the client's request headers. These are 
 * useful for telnet testing; all that a server receives is from
 * the cmdline, so the proxy supplies the mandatory headers. But
 * for browser requests, the browser as client usually supplies
 * the headers Host, User-Agent. So the proxy could simply 
 * forward all headers, override Connection to specify "close",
 * append Proxy-Connection, and continue forwarding.
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
static const char *user_agent_hdr_alt = "Mozilla/5.0 (X11; Ubuntu; Linux x86_64; rv:84.0) Gecko/20100101 Firefox/84.0\r\n";

void readparse_request(int fd, char *targethost, char *path, char *request_toserver, char *port, rio_t *rp);
void parse_url(char *url, char *host, char *abs_path, char *port);
void forward_requesthdrs(rio_t *rp, int fd);
void send_request(int server_connfd, char *request_toserver, char *targethost);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);

/* $begin main */
int main(int argc, char **argv)
{
/*
	Part I: implementing a sequential web proxy

	server functionality
	set up proxy to accept incoming connections
	read/parse requests, GET HTTP/1.0

	client functionality
	forward requests to web servers
	read the servers' responses

	server functionality
	forward those responses to corresponding clients

*/
    int listenfd, client_connfd, server_connfd;
    char hostname[MAXLINE], port[MAXLINE], targethost[MAXLINE], path[MAXLINE], 
    			request_toserver[MAXLINE], server_buf[MAXLINE], server_port[6];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    int rio_cnt;
    rio_t rio_client, rio_toserver; /* should a new rio structure be initiated in each iteration? */

	/* Check command line args */
    if (argc != 2) {
		fprintf(stderr, "usage: %s <port>\n", argv[0]);
		exit(1);
    }

    /* sequential proxy: handles one client at a time */
    listenfd = Open_listenfd(argv[1]); /* port passed to proxy at cmdline argv[1]*/
    while (1) {
    	/* accept incoming connections */
    	clientlen = sizeof(clientaddr);
    	client_connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen); 

    	/* obtain client info; not necessary for basic proxy tasks? */
        Getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE, 
                    port, MAXLINE, 0);
        printf("PROXY: Accepted connection from client (%s, %s)\n", hostname, port);

        /* read/parse GET requests, 
         * extract the host and path which the client requests 
         * set up the client-facing I/O buffer from rio package */
        readparse_request(client_connfd, targethost, path, request_toserver, 
        			server_port, &rio_client);

        /* proxy performs a client role: connect to the server */
		server_connfd = Open_clientfd(targethost, server_port);

		/* send to server request as GET <resource> HTTP/1.0 + request headers */
		/* may need to deal with I/O error signals by installing sighandlers that ignore them */
		send_request(server_connfd, request_toserver, targethost);

        /* read request headers from client (browser etc) and forward to server */
	    forward_requesthdrs(&rio_client, server_connfd); 

        /* set up rio buffer to read server responses */
        Rio_readinitb(&rio_toserver, server_connfd); /* proxy can only start reading server responses here */
        while ( (rio_cnt = Rio_readlineb(&rio_toserver, server_buf, MAXLINE)) != 0 ) {
        	// printf("%s\n",server_buf);

        	/* write server responses to client */
        	Rio_writen(client_connfd, server_buf, strlen(server_buf));
        }

        /* close socket with server, then close socket with client */
        Close(server_connfd);
    	Close(client_connfd);
    }

	return 0;
}
/* $end main */


/*
 * readparse_request - read and parse requests received from client 
 * Expected requests are according to rfc1945. Scheme is http, 
 * version HTTP/1.0. 
 */
/* $begin readparse_request */
void readparse_request(int fd, char *targethost, char *path, char *request_toserver, char *port, rio_t *rp)
{
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];

    /* Read request line and headers */
    Rio_readinitb(rp, fd);
    if (!Rio_readlineb(rp, buf, MAXLINE))  
        return;
    printf("PROXY: Request (headers?) received from client:\n");
    printf("%s", buf);
    sscanf(buf, "%s %s %s", method, uri, version);       
    if (strcasecmp(method, "GET")) {   
        clienterror(fd, method, "501", "Not Implemented",
                    "Tiny does not implement this method");
        return;
    }                                                   

    /* parse URI into targethost and path */
    parse_url(uri, targethost, path, port);

    /* override version such that the proxy always sends HTTP/1.0 */
    strcpy(version, "HTTP/1.0");

    /* build the request line */
    strcpy(request_toserver, "");
    sprintf(request_toserver, "%s %s %s", method, path, version); /* may need terminating "\r\n" */
    return;
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
    strcpy(abs_path, "");
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
    printf("Sending the following request headers by proxy: \n");

	sprintf(buf, "%s\r\n", request_toserver); printf("%s\n", buf);
    Rio_writen(server_connfd, buf, strlen(buf));

    /* correction needed: find the host header that the browser uses, and use that one */
    sprintf(buf, "Host: %s\r\n", targethost); printf("%s\n", buf);
    Rio_writen(server_connfd, buf, strlen(buf));

    sprintf(buf, "User-Agent: %s\r\n", user_agent_hdr_alt); printf("%s\n", buf);
    Rio_writen(server_connfd, buf, strlen(buf));
    sprintf(buf, "Connection: %s\r\n", "close"); printf("%s\n", buf);
    Rio_writen(server_connfd, buf, strlen(buf));
    sprintf(buf, "Proxy-Connection: %s\r\n", "close"); printf("%s\n", buf);
    Rio_writen(server_connfd, buf, strlen(buf)); 

    return;
}
/* $end send_request */


/*
 * clienterror - returns an error message to the client
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
    sprintf(buf, "<html><title>Tiny Error</title>");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<body bgcolor=""ffffff"">\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "%s: %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<p>%s: %s\r\n", longmsg, cause);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<hr><em>The Tiny Web server</em>\r\n");
    Rio_writen(fd, buf, strlen(buf));
}
/* $end clienterror */

