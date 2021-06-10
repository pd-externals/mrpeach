/* udpsrvr.c 20210607. Martin Peach, from udpsndrcv */
/* A UDP socket that listens on a port and can connect to any address */
/* udpsndrcv.c 20140221.  Dennis Engdahl did it based on: 20060424. Martin Peach did it based on x_net.c. x_net.c header follows: */
/* Copyright (c) 1997-1999 Miller Puckette.
* For information on usage and redistribution, and for a DISCLAIMER OF ALL
* WARRANTIES, see the file, "LICENSE.txt," in this distribution.  */

/* network */

#include "m_pd.h"
#include "s_stuff.h"

#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <sys/ioctl.h> // for SIOCGIFCONF
#include <net/if.h> // for SIOCGIFCONF
#include <arpa/inet.h>
#include <errno.h>
#include <unistd.h>
#endif // _WIN32

/* support older Pd versions without sys_open(), sys_fopen(), sys_fclose() */
#if PD_MAJOR_VERSION == 0 && PD_MINOR_VERSION < 44
#define sys_open open
#define sys_fopen fopen
#define sys_fclose fclose
#endif

static t_class *udpsrvr_class;

#define MAX_UDP_RECEIVE 65536L // longer than data in maximum UDP packet

typedef struct _udpsrvr
{
    t_object            x_obj;
    int                 x_fd; /* the socket */
    int                 x_portno; /* the port we listen on */
    t_outlet            *x_msgout;
    t_outlet            *x_addrout;
    long                x_total_received;
    t_float             x_verbose; 
    struct sockaddr_in  x_remote;
    t_atom              x_addrbytes[5];
    t_atom              x_msgoutbuf[MAX_UDP_RECEIVE];
    char                x_msginbuf[MAX_UDP_RECEIVE];
    char                x_addr_name[256]; // a multicast address or 0
} t_udpsrvr;

void udpsrvr_setup(void);
static void udpsrvr_free(t_udpsrvr *x);
static void udpsrvr_send(t_udpsrvr *x, t_symbol *s, int argc, t_atom *argv);
static void udpsrvr_listen(t_udpsrvr *x, t_floatarg fListeningPort);
//static void udpsrvr_to(t_udpsrvr *x, t_symbol *hostname, t_floatarg fToPort);
static void udpsrvr_to(t_udpsrvr *x, t_symbol *s, int argc, t_atom *argv);
static void udpsrvr_sock_err(t_udpsrvr *x, char *err_string);
static void *udpsrvr_new(void);
static void udpsrvr_status(t_udpsrvr *x);
static void udpsrvr_read(t_udpsrvr *x, int sockfd);
static void udpsrvr_verbosity(t_udpsrvr *x, t_floatarg v);

static void *udpsrvr_new(void)
{
    t_udpsrvr *x;
    int       i;

    x = (t_udpsrvr *)pd_new(udpsrvr_class); /* if something fails we return 0 instead of x. Is this OK? */
    if (NULL == x) return x;
    x->x_addr_name[0] = '\0';
    /* convert the bytes in the buffer to floats in a list */
    for (i = 0; i < MAX_UDP_RECEIVE; ++i)
    {
        x->x_msgoutbuf[i].a_type = A_FLOAT;
        x->x_msgoutbuf[i].a_w.w_float = 0;
    }
    for (i = 0; i < 5; ++i)
    {
        x->x_addrbytes[i].a_type = A_FLOAT;
        x->x_addrbytes[i].a_w.w_float = 0;
    }
    //outlet_new(&x->x_obj, &s_float);
    x->x_msgout = outlet_new(&x->x_obj, &s_anything);
    x->x_addrout = outlet_new(&x->x_obj, &s_anything);

    x->x_fd = -1;
    x->x_portno = 0;
    return (x);
}

static void udpsrvr_read(t_udpsrvr *x, int sockfd)
{
    int                 i, read = 0;
    struct sockaddr_in  from;
    socklen_t           fromlen = sizeof(from);
    t_atom              output_atom;
    long                addr;
    unsigned short      port;

    read = recvfrom(sockfd, x->x_msginbuf, MAX_UDP_RECEIVE, 0, (struct sockaddr *)&from, &fromlen);
#ifdef DEBUG
    post("udpsrvr_read: read %lu x->x_fd = %d",
        read, x->x_fd);
#endif
    /* get the sender's ip */
    addr = ntohl(from.sin_addr.s_addr);
    port = ntohs(from.sin_port);

    x->x_addrbytes[0].a_w.w_float = (addr & 0xFF000000)>>24;
    x->x_addrbytes[1].a_w.w_float = (addr & 0x0FF0000)>>16;
    x->x_addrbytes[2].a_w.w_float = (addr & 0x0FF00)>>8;
    x->x_addrbytes[3].a_w.w_float = (addr & 0x0FF);
    x->x_addrbytes[4].a_w.w_float = port;
    outlet_anything(x->x_addrout, gensym("from"), 5L, x->x_addrbytes);

    if (read < 0)
    {
        udpsrvr_sock_err(x, "udpsrvr_read");
        sys_closesocket(x->x_fd);
        sys_rmpollfn(x->x_fd);
        x->x_fd = -1;
        x->x_portno = 0;
        return;
    }
    if (read > 0)
    {
        for (i = 0; i < read; ++i)
        {
            /* convert the bytes in the buffer to floats in a list */
            x->x_msgoutbuf[i].a_w.w_float = (float)(unsigned char)x->x_msginbuf[i];
        }
        x->x_total_received += read;
        SETFLOAT(&output_atom, read);
        outlet_anything(x->x_addrout, gensym("received"), 1, &output_atom);
        /* send the list out the outlet */
        if (read > 1) outlet_list(x->x_msgout, &s_list, read, x->x_msgoutbuf);
        else outlet_float(x->x_msgout, x->x_msgoutbuf[0].a_w.w_float);
    }
}

static void udpsrvr_listen(t_udpsrvr *x, t_floatarg fListeningPort)
{
    struct sockaddr_in  server;
    int                 sockfd;
    int                 portno = fListeningPort;
    t_atom              output_atom;

    if ((x->x_fd >= 0) && (portno == x->x_portno)) return; // don't change a thing
    if ((x->x_fd >= 0) && (portno != x->x_portno))
    { // reassign the listening port: close the socket and start again
        sys_closesocket(x->x_fd);
        sys_rmpollfn(x->x_fd);
        x->x_fd = -1;
        x->x_portno = 0;
    }
    if (x->x_fd < 0)
    {
        /* First time only, create a UDP socket to listen on */
        sockfd = socket(AF_INET, SOCK_DGRAM, 0);
        if (x->x_verbose) post("udpsrvr_listen: new socket %d", sockfd);
        if (sockfd < 0)
        {
            udpsrvr_sock_err(x, "udpsrvr socket");
            return;
        }
        x->x_fd = sockfd;
        sys_addpollfn(x->x_fd, (t_fdpollfn)udpsrvr_read, x);
    }
    else sockfd = x->x_fd; // use existing socket
    /* assign servers port number */
    server.sin_family = AF_INET;
    server.sin_port = htons((u_short)portno);

    /* bind the socket to INADDR_ANY and port as specified */
    server.sin_addr.s_addr = INADDR_ANY; // receive from anywhere
    if (bind(sockfd, (struct sockaddr *) &server, sizeof (server)) < 0)
    {
        udpsrvr_sock_err(x, "udpsrvr: bind");
        sys_closesocket(sockfd);
        sys_rmpollfn(x->x_fd);
        x->x_fd = -1;
        x->x_portno = 0;
        return;
    }
    x->x_portno = portno;
    if (x->x_verbose) post("udpsrvr listening on port %d", x->x_portno);
    x->x_total_received = 0L;
    SETFLOAT(&output_atom, x->x_portno);
    outlet_anything(x->x_addrout, gensym("listening"), 1, &output_atom);
    return;
}

//static void udpsrvr_to(t_udpsrvr *x, t_symbol *hostname, t_floatarg fToPort)
static void udpsrvr_to(t_udpsrvr *x, t_symbol *s, int argc, t_atom *argv)
{ // set up the remote address for sendto
    struct hostent  *hp;
    int             toPort, ip[4];
    int             len, i;
    char            addrBuf[64];
    
    if (x->x_fd < 0)
    {
        pd_error(x, "udpsrvr: no socket: listen first");
        return;
    }

    if ((argc >= 2) && (argv[0].a_type == A_SYMBOL) && (argv[1].a_type == A_FLOAT))
    {
        /* assign client port number */
        toPort = argv[1].a_w.w_float;
        if (toPort == 0) toPort = x->x_portno;
        x->x_remote.sin_family = AF_INET;
        /* adddress socket using hostname symbol */
        hp = gethostbyname(argv[0].a_w.w_symbol->s_name);
        if (hp == 0)
        {
            post("udpsrvr: bad host?");
            return;
        }
        // the chars in hp->h_addr are the 4 bytes of the ipv4 address in network order
        if (x->x_verbose)
            post
            (
                "udpsrvr: gethostbyname returned %d %d %d %d",
                *((char *)hp->h_addr),
                *((char *)hp->h_addr+1),
                *((char *)hp->h_addr+2),
                *((char *)hp->h_addr+3)
            );
        memcpy((char *)&x->x_remote.sin_addr, (char *)hp->h_addr, hp->h_length);
    }
    else if 
    (
        (argc >= 5)
        && (argv[0].a_type == A_FLOAT)
        && (argv[1].a_type == A_FLOAT)
        && (argv[2].a_type == A_FLOAT)
        && (argv[3].a_type == A_FLOAT)
        && (argv[4].a_type == A_FLOAT)
    )
    {
        /* adddress socket using 4 floats */
        for (i = 0; i < 4; ++i )
        {
            ip[i] = (int)argv[i].a_w.w_float;
            if ((ip[i] < 0) || (ip[i] > 255))
            {
                post("udpsrvr: ip element %d out of range [0-255]:%d", i, ip[i]);
                return;
            } 
            addrBuf[i] = ip[i];
        }
        /* assign client port number */
        toPort = argv[1].a_w.w_float;
        if (toPort == 0) toPort = x->x_portno;
        x->x_remote.sin_family = AF_INET;
        // the chars in addrBuf are the 4 bytes of the ipv4 address in network order
        if (x->x_verbose)
            post
            (
                "udpsrvr: numeric host %d %d %d %d",
                *((char *)addrBuf),
                *((char *)addrBuf+1),
                *((char *)addrBuf+2),
                *((char *)addrBuf+3)
            );
        memcpy((char *)&x->x_remote.sin_addr, addrBuf, 4);
    }
    else return;

    x->x_remote.sin_port = htons((u_short)toPort);

    if (x->x_verbose) post("udpsrvr: will send to port %d from %d", toPort, x->x_portno);
    return;
}

static void udpsrvr_sock_err(t_udpsrvr *x, char *err_string)
{
/* prints the last error from errno or WSAGetLastError() */
#ifdef _WIN32
    void            *lpMsgBuf;
    unsigned long   errornumber = WSAGetLastError();
    int             len = 0, i;
    char            *cp;

    if ((len = FormatMessageA((FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS),
        NULL, errornumber, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&lpMsgBuf, 0, NULL)))
    {
        cp = (char *)lpMsgBuf;
        for(i = 0; i < len; ++i)
        {
            if (cp[i] < 0x20)
            { /* end string at first weird character */
                cp[i] = 0;
                break;
            }
        }
        pd_error(x, "%s: %s (%ld)", err_string, (char *)lpMsgBuf, errornumber);
        LocalFree(lpMsgBuf);
    }
#else
    pd_error(x, "%s: %s (%d)", err_string, strerror(errno), errno);
#endif
}

static void udpsrvr_status(t_udpsrvr *x)
{
    t_atom output_atom;

    SETFLOAT(&output_atom, x->x_total_received);
    outlet_anything(x->x_addrout, gensym("total"), 1, &output_atom);
}

static void udpsrvr_send(t_udpsrvr *x, t_symbol *s, int argc, t_atom *argv)
{
#define BYTE_BUF_LEN 65536 // arbitrary maximum similar to max IP packet size
    static char    byte_buf[BYTE_BUF_LEN];
    int            d;
    int            i, j;
    unsigned char  c;
    float          f, e;
    char           *bp;
    int            length, sent;
    int            result;
    static double  lastwarntime;
    static double  pleasewarn;
    double         timebefore;
    double         timeafter;
    int            late;
    char           fpath[FILENAME_MAX];
    FILE           *fptr;

#ifdef DEBUG
    post("s: %s", s->s_name);
    post("argc: %d", argc);
#endif
    for (i = j = 0; i < argc; ++i)
    {
        if (argv[i].a_type == A_FLOAT)
        {
            f = argv[i].a_w.w_float;
            d = (int)f;
            e = f - d;
            if (e != 0)
            {
                pd_error(x, "udpsrvr_send: item %d (%f) is not an integer", i, f);
                return;
            }
            c = (unsigned char)d;
            if (c != d)
            {
                pd_error(x, "udpsrvr_send: item %d (%f) is not between 0 and 255", i, f);
                return;
            }
#ifdef DEBUG
            post("udpsrvr_send: argv[%d]: %d", i, c);
#endif
            byte_buf[j++] = c;
        }
        else if (argv[i].a_type == A_SYMBOL)
        {

            atom_string(&argv[i], fpath, FILENAME_MAX);
#ifdef DEBUG
            post ("udpsrvr fname: %s", fpath);
#endif
            fptr = sys_fopen(fpath, "rb");
            if (fptr == NULL)
            {
                post("udpsrvr: unable to open \"%s\"", fpath);
                return;
            }
            rewind(fptr);
            while ((d = fgetc(fptr)) != EOF)
            {
#ifdef DEBUG
                post("udpsrvr: d is %d", d);
#endif
                byte_buf[j++] = (char)(d & 0x0FF);
#ifdef DEBUG
                post("udpsrvr: byte_buf[%d] = %d", j-1, byte_buf[j-1]);
#endif
                if (j >= BYTE_BUF_LEN)
                {
                    post ("udpsrvr: file too long, truncating at %lu", BYTE_BUF_LEN);
                    break;
                }
            }
            fclose(fptr);
            fptr = NULL;
            post("udpsrvr: read \"%s\" length %d byte%s", fpath, j, ((d==1)?"":"s"));
        }
        else
        {
            pd_error(x, "udpsrvr_send: item %d is not a float or a file name", i);
            return;
        }
    }

    length = j;
    if ((x->x_fd >= 0) && (length > 0))
    {
        for (bp = byte_buf, sent = 0; sent < length;)
        {
            timebefore = sys_getrealtime();
            //result = send(x->x_fd, byte_buf, length-sent, 0);
            result = sendto(x->x_fd, byte_buf, length-sent, 0, (struct sockaddr *)&x->x_remote, sizeof(x->x_remote));
            timeafter = sys_getrealtime();
            late = (timeafter - timebefore > 0.005);
            if (late || pleasewarn)
            {
                if (timeafter > lastwarntime + 2)
                {
                    post("udpsrvr blocked %d msec",
                        (int)(1000 * ((timeafter - timebefore) + pleasewarn)));
                    pleasewarn = 0;
                    lastwarntime = timeafter;
                }
                else if (late) pleasewarn += timeafter - timebefore;
            }
            if (result <= 0)
            {
                udpsrvr_sock_err(x, "udpsrvr send");
                break;
            }
            else
            {
                sent += result;
                bp += result;
            }
        }
    }
    else pd_error(x, "udpsrvr: not connected");
}

static void udpsrvr_verbosity(t_udpsrvr *x, t_floatarg v)
{
    x->x_verbose = (v == 0)?0:1;
}


static void udpsrvr_free(t_udpsrvr *x)
{
    if (x->x_fd >= 0)
    {
        sys_closesocket(x->x_fd);
        sys_rmpollfn(x->x_fd);
        x->x_fd = -1;
    }
}

void udpsrvr_setup(void)
{
    udpsrvr_class = class_new(gensym("udpsrvr"), (t_newmethod)udpsrvr_new, (t_method)udpsrvr_free, sizeof(t_udpsrvr), 0, 0);
    class_addmethod(udpsrvr_class, (t_method)udpsrvr_listen, gensym("listen"), A_FLOAT, 0);
//    class_addmethod(udpsrvr_class, (t_method)udpsrvr_to, gensym("to"), A_SYMBOL, A_FLOAT, 0);
    class_addmethod(udpsrvr_class, (t_method)udpsrvr_to, gensym("to"), A_GIMME, 0);
    class_addmethod(udpsrvr_class, (t_method)udpsrvr_send, gensym("send"), A_GIMME, 0);
    class_addmethod(udpsrvr_class, (t_method)udpsrvr_status,
        gensym("status"), 0);
    class_addlist(udpsrvr_class, (t_method)udpsrvr_send);
    class_addmethod(udpsrvr_class, (t_method)udpsrvr_verbosity, gensym("verbose"), A_FLOAT, 0);
}

/* end udpsrvr.c*/
