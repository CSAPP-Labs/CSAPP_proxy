/* 
 * io_wrappers.c - non-terminating wrappers for Robust I/O functions from csapp.c
 */
/* $begin io_wrappers.c */
#include "csapp.h"
#include "io_wrappers.h"

/****************************************
 * The Rio_w package - Robust I/O functions
 ****************************************/

/*
 * rio_writen_w - Robustly write n bytes (unbuffered)
 */
/* $begin rio_writen_w */
ssize_t rio_writen_w(int fd, void *usrbuf, size_t n) 
{
    size_t nleft = n;
    ssize_t nwritten;
    char *bufp = usrbuf;

    while (nleft > 0) {
	if ((nwritten = write(fd, bufp, nleft)) <= 0) {
	    if (errno == EINTR)  /* Interrupted by sig handler return */
		  nwritten = 0;    /* and call write() again */
        else if (errno == EPIPE) { /* EDIT: announce EPIPE error at the proxy */
            // printf("EPIPE error.\n");
            return -1; /* EDIT: crash out */
        } else
		  return -1;       /* errno set by write() */
	}
	nleft -= nwritten;
	bufp += nwritten;
    }
    return n;
}
/* $end rio_writen_w */


/* 
 * rio_read_w - bypasses ECONNRESET
 */
/* $begin rio_read_w */
static ssize_t rio_read_w(rio_t *rp, char *usrbuf, size_t n)
{
    int cnt;

    while (rp->rio_cnt <= 0) {  /* Refill if buf is empty */
	rp->rio_cnt = read(rp->rio_fd, rp->rio_buf, 
			   sizeof(rp->rio_buf));
	if (rp->rio_cnt < 0) {
        if (errno == ECONNRESET) { /* EDIT: treat prematurely closed socket as EOF */
            return 0;
        }
	    if (errno != EINTR) /* Interrupted by sig handler return */
		return -1;
	}
	else if (rp->rio_cnt == 0)  /* EOF */
	    return 0;
	else 
	    rp->rio_bufptr = rp->rio_buf; /* Reset buffer ptr */
    }

    /* Copy min(n, rp->rio_cnt) bytes from internal buf to user buf */
    cnt = n;          
    if (rp->rio_cnt < n)   
	cnt = rp->rio_cnt;
    memcpy(usrbuf, rp->rio_bufptr, cnt);
    rp->rio_bufptr += cnt;
    rp->rio_cnt -= cnt;
    return cnt;
}
/* $end rio_read_w */


/*
 * rio_readnb_w - Robustly read n bytes (buffered)
 */
/* $begin rio_readnb_w */
ssize_t rio_readnb_w(rio_t *rp, void *usrbuf, size_t n) 
{
    size_t nleft = n;
    ssize_t nread;
    char *bufp = usrbuf;
    
    while (nleft > 0) {
	if ((nread = rio_read_w(rp, bufp, nleft)) < 0) 
            return -1;          /* errno set by read() */ 
	else if (nread == 0)
	    break;              /* EOF */
	nleft -= nread;
	bufp += nread;
    }
    return (n - nleft);         /* return >= 0 */
}
/* $end rio_readnb_w */

/* 
 * rio_readlineb_w - Robustly read a text line (buffered)
 */
/* $begin rio_readlineb_w */
ssize_t rio_readlineb_w(rio_t *rp, void *usrbuf, size_t maxlen) 
{
    int n, rc;
    char c, *bufp = usrbuf;

    for (n = 1; n < maxlen; n++) { 
        if ((rc = rio_read_w(rp, &c, 1)) == 1) {
	    *bufp++ = c;
	    if (c == '\n') {
                n++;
     		break;
            }
	} else if (rc == 0) {
	    if (n == 1)
		return 0; /* EOF, no data read */
	    else
		break;    /* EOF, some data was read */
	} else
	    return -1;	  /* Error */
    }
    *bufp = 0;
    return n-1;
}
/* $end rio_readlineb_w */

/**********************************
 * Wrappers for robust I/O routines
 **********************************/


void Rio_writen_w(int fd, void *usrbuf, size_t n) 
{
    if (rio_writen_w(fd, usrbuf, n) != n)
	unix_error("Rio_writen error");
}



ssize_t Rio_readnb_w(rio_t *rp, void *usrbuf, size_t n) 
{
    ssize_t rc;

    if ((rc = rio_readnb_w(rp, usrbuf, n)) < 0)
	unix_error("Rio_readnb error");
    return rc;
}

ssize_t Rio_readlineb_w(rio_t *rp, void *usrbuf, size_t maxlen) 
{
    ssize_t rc;

    if ((rc = rio_readlineb_w(rp, usrbuf, maxlen)) < 0)
	unix_error("Rio_readlineb error");
    return rc;
} 

