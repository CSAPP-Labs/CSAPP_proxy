/* 
 * io_wrappers.c - non-terminating wrappers for Robust I/O functions from csapp.c
 */
/* $begin io_wrappers.c */
// #include "csapp.h"

ssize_t rio_writen_w(int fd, void *usrbuf, size_t n);
static ssize_t rio_read_w(rio_t *rp, char *usrbuf, size_t n);
ssize_t rio_readnb_w(rio_t *rp, void *usrbuf, size_t n);
ssize_t rio_readlineb_w(rio_t *rp, void *usrbuf, size_t maxlen);
void Rio_writen_w(int fd, void *usrbuf, size_t n);
ssize_t Rio_readnb_w(rio_t *rp, void *usrbuf, size_t n);
ssize_t Rio_readlineb_w(rio_t *rp, void *usrbuf, size_t maxlen);

