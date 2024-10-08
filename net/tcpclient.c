/* tcpclient.c Martin Peach 20060508, working version 20060512 */
/* linux version 20060515 */
/* tcpclient.c is based on netclient: */
/* --------------------------  netclient  ------------------------------------- */
/*                                                                              */
/* Extended 'netsend', connects to 'netserver'.                                 */
/* Uses child thread to connect to server. Thus needs pd0.35-test17 or later.   */
/* Written by Olaf Matthes (olaf.matthes@gmx.de)                                */
/* Get source at http://www.akustische-kunst.org/puredata/maxlib/               */
/*                                                                              */
/* This program is free software; you can redistribute it and/or                */
/* modify it under the terms of the GNU General Public License                  */
/* as published by the Free Software Foundation; either version 2               */
/* of the License, or (at your option) any later version.                       */
/*                                                                              */
/* This program is distributed in the hope that it will be useful,              */
/* but WITHOUT ANY WARRANTY; without even the implied warranty of               */
/* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the                */
/* GNU General Public License for more details.                                 */
/*                                                                              */
/* You should have received a copy of the GNU General Public License            */
/* along with this program; if not, write to the Free Software                  */
/* Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.  */
/*                                                                              */
/* Based on PureData by Miller Puckette and others.                             */
/*                                                                              */
/* ---------------------------------------------------------------------------- */
//#define DEBUG

#include "m_pd.h"
#include "s_stuff.h"

#include <stdio.h>
#include <sys/types.h>
#include <string.h>
#include <pthread.h>
#ifndef _WIN32
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <sys/errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <stdio.h>
#include <unistd.h>
#define SOCKET_ERROR -1
#else
#include <winsock2.h>
#include <ws2tcpip.h> /* for socklen_t */
#endif

/* support older Pd versions without sys_open(), sys_fopen(), sys_fclose() */
#if PD_MAJOR_VERSION == 0 && PD_MINOR_VERSION < 44
#define sys_open open
#define sys_fopen fopen
#define sys_fclose fclose
#endif

#ifdef _MSC_VER
#define snprintf sprintf_s
#endif

#define DEFPOLLTIME 20  /* check for input every 20 ms */

static t_class *tcpclient_class;
static char objName[] = "tcpclient";
#define MAX_TCPCLIENT_SEND_BUF 65536L // longer than data in maximum UDP packet

/* each send is handled by a thread with a new parameter struct: */
/* these are stored under x->x_tsp[0..MAX_TCPCLIENT_BUFFERS-1] */
/* The buffer is preallocated for speed. */
typedef struct _tcpclient_sender_params
{
    char                x_sendbuf[MAX_TCPCLIENT_SEND_BUF]; /* possibly allocate this dynamically for space over speed */
    size_t              x_buf_len;
    int                 x_sendresult;
//    pthread_t           sendthreadid;
    int                 threadisvalid; /*  non-zero if sendthreadid is an active thread */
    int                 x_finished; /* non-zero if thread has terminated */
//    struct _tcpclient   *x_x;
} t_tcpclient_sender_params;
#define MAX_TCPCLIENT_BUFFERS 32
/* MAX_TCPCLIENT_BUFFERS is small to avoid wasting space. This is the maximum number of concurrent threads. */

typedef struct _tcpclient
{
    t_object                    x_obj;
    t_clock                     *x_clock;
    t_clock                     *x_poll;
    //t_clock                     *x_sendclock;
    t_outlet                    *x_msgout;
    t_outlet                    *x_addrout;
    t_outlet                    *x_connectout;
    t_outlet                    *x_statusout;
    int                         x_dump; // 1 = hexdump received bytes
    int                         x_verbosity; // 1 = post connection state changes to main window
    int                         x_fd; // the socket
    int                         x_fdbuf; // the socket's buffer size
    char                        *x_hostname; // address we want to connect to as text
    int                         x_connectstate; // 0 = not connected, 1 = connected
    int                         x_port; // port we're connected to
    uint32_t                    x_addr; // address we're connected to as 32bit int
    uint32_t                    x_ourAddr; // address of our interface
    int                         x_ourPort; // port nunber of our interface
    t_atom                      x_addrbytes[5]; // address we're connected to as 4 bytes and one int
    t_atom                      x_msgoutbuf[MAX_TCPCLIENT_SEND_BUF]; // received data as float atoms
    unsigned char               x_msginbuf[MAX_TCPCLIENT_SEND_BUF]; // received data as bytes
    char                        *x_sendbuf; // pointer to data to send
    int                         x_blocked;
    /* multithread stuff */
    pthread_t                   x_threadid; /* id of connector child thread */
    pthread_t                   x_sendthreadid; /* id of sender child thread */
    pthread_attr_t              x_threadattr; /* attributes of connector child thread */
    pthread_attr_t              x_sendthreadattr; /* attributes of sender child thread */
    int                         x_nextbuffer; /* next unused send buffer */
    t_tcpclient_sender_params   x_tsp[MAX_TCPCLIENT_BUFFERS];
/* Thread params are used round-robin to avoid overwriting buffers when doing multiple sends */
} t_tcpclient;

static void tcpclient_verbosity(t_tcpclient *x, t_float verbosity);
static void tcpclient_dump(t_tcpclient *x, t_float dump);
static void tcp_client_hexdump(t_tcpclient *x, long len);
static void tcpclient_tick(t_tcpclient *x);
static void *tcpclient_child_connect(void *w);
static void tcpclient_connect(t_tcpclient *x, t_symbol *hostname, t_floatarg fportno);
static void tcpclient_disconnect(t_tcpclient *x);
static void tcpclient_send(t_tcpclient *x, t_symbol *s, int argc, t_atom *argv);
static int tcpclient_send_buf(t_tcpclient *x, char *buf, int buf_len);
static void *tcp_client_sender_loop(void *w);
static void tcpclient_sent(t_tcpclient *x);
static int tcpclient_get_socket_send_buf_size(t_tcpclient *x);
static int tcpclient_set_socket_send_buf_size(t_tcpclient *x, int size);
static void tcpclient_buf_size(t_tcpclient *x, t_symbol *s, int argc, t_atom *argv);
static void tcpclient_rcv(t_tcpclient *x);
static void tcpclient_poll(t_tcpclient *x);
static void *tcpclient_new(void);
static void tcpclient_free(t_tcpclient *x);
void tcpclient_setup(void);

static void tcpclient_dump(t_tcpclient *x, t_float dump)
{
    x->x_dump = (dump == 0)?0:1;
}

static void tcpclient_verbosity(t_tcpclient *x, t_float verbosity)
{
    x->x_verbosity = (verbosity == 0)?0:1; /* only two states so far */
}

static void tcp_client_hexdump(t_tcpclient *x, long len)
{
#define BYTES_PER_LINE 16
    char            hexStr[(3*BYTES_PER_LINE)+1];
    char            ascStr[BYTES_PER_LINE+1];
    long            i, j, k = 0L;
    unsigned char   *buf = x->x_msginbuf;

    if (x->x_verbosity) post("%s_hexdump %d:", objName, len);
    while (k < len)
    {
        for (i = j = 0; i < BYTES_PER_LINE; ++i, ++k, j+=3)
        {
            if (k < len)
            {
                snprintf(&hexStr[j], 4, "%02X ", buf[k]);
                snprintf(&ascStr[i], 2, "%c", ((buf[k] >= 32) && (buf[k] <= 126))? buf[k]: '.');
            }
            else
            { // the last line
                snprintf(&hexStr[j], 4, "   ");
                snprintf(&ascStr[i], 2, " ");
            }
        }
        post ("%s%s", hexStr, ascStr);
    }
}

/* tcpclient_tick: this is called when x_clock times out (on connect) */
static void tcpclient_tick(t_tcpclient *x)
{
    t_atom output_atom[5];
    int i, result;

    SETFLOAT(&output_atom[0], (x->x_ourAddr & 0xFF000000)>>24); // address of our interface in bytes
    SETFLOAT(&output_atom[1], (x->x_ourAddr & 0x0FF0000)>>16);
    SETFLOAT(&output_atom[2], (x->x_ourAddr & 0x0FF00)>>8);
    SETFLOAT(&output_atom[3], (x->x_ourAddr & 0x0FF));
    SETFLOAT(&output_atom[4], x->x_ourPort); // port nunber of our interface
    outlet_anything( x->x_statusout, gensym("ourIP"), 5, output_atom);
    outlet_float(x->x_connectout, 1);
    /* start the sender loop */
    for (i = 0; i < MAX_TCPCLIENT_BUFFERS; ++i)
    {
        x->x_tsp[i].threadisvalid = 0;
        x->x_tsp[i].x_buf_len = 0;
    }
    if((result = pthread_create(&x->x_sendthreadid, &x->x_sendthreadattr, tcp_client_sender_loop, x)) < 0)
    {
        pd_error(x, "tcpclient_tick: could not create new thread (%d)", result);
        /* mow what? */
    }

}

/* tcpclient_child_connect is in its own thread */
static void *tcpclient_child_connect(void *w)
{
    t_tcpclient         *x = (t_tcpclient*) w;
    struct sockaddr_in  server, addr;
    struct hostent      *hp;
    int                 sockfd, intarg;
    socklen_t           addrlen = sizeof (addr);

    /* create a socket */
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
#ifdef DEBUG
    sys_lock(); post("%s: send socket %d\n", objName, sockfd); sys_unlock();
#endif
    if (sockfd < 0)
    {
        sys_lock(); sys_sockerror("tcpclient: socket"); sys_unlock();
        return (x);
    }
    /* connect socket using hostname provided in command line */
    server.sin_family = AF_INET;

    /* ask OS to allow another Pd to reopen this port after we close it. */
    intarg = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (char *)&intarg, sizeof(intarg)) < 0)
    {
        sys_lock(); post("tcpreceive: setsockopt (SO_REUSEADDR) failed"); sys_unlock();
    }
    /* Stream (TCP) sockets are set NODELAY */
    intarg = 1;
    if (setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, (char *)&intarg, sizeof(intarg)) < 0)
    {
        sys_lock(); post("setsockopt (TCP_NODELAY) failed\n"); sys_unlock();
    }

    hp = gethostbyname(x->x_hostname);
    if (hp == 0)
    {
        sys_lock(); sys_sockerror("tcpclient: bad host?\n"); sys_unlock();
        return (x);
    }
    memcpy((char *)&server.sin_addr, (char *)hp->h_addr, hp->h_length);

    /* assign client port number */
    server.sin_port = htons((u_short)x->x_port);

    if (x->x_verbosity)
    {
      sys_lock(); post("%s: connecting socket %d to port %d", objName, sockfd, x->x_port); sys_unlock();
    }
    /* try to connect */
    if (connect(sockfd, (struct sockaddr *) &server, sizeof (server)) < 0)
    {
        sys_lock();
        sys_sockerror("tcpclient: connecting stream socket");
        sys_closesocket(sockfd);
        sys_unlock();
        return (x);
    }
    x->x_fd = sockfd;
    x->x_addr = ntohl(*(uint32_t *)hp->h_addr);
    /* outlet_float is not threadsafe ! */
    // outlet_float(x->x_obj.ob_outlet, 1);
    x->x_connectstate = 1;
    x->x_blocked = 0;
    /* Find our address */
    if (getsockname(sockfd, (struct sockaddr *)&addr, (socklen_t *)&addrlen))
    {
        sys_lock(); sys_sockerror("tcpclient: getting socket name"); sys_unlock();
    }
    else
    { /* output the address this socket is bound to */
        //printf("addrlen %u\n", addrlen);
        //printf("port %u\n", addr.sin_port);
        //printf("IP address %s\n", inet_ntoa(addr.sin_addr));
        x->x_ourAddr = ntohl(addr.sin_addr.s_addr);
        x->x_ourPort = ntohs(addr.sin_port);
    }
    /* use callback instead to set outlet */
    sys_lock(); clock_delay(x->x_clock, 0); sys_unlock();
    return (x);
}

static void tcpclient_connect(t_tcpclient *x, t_symbol *hostname, t_floatarg fportno)
{
    /* if we are not already connected, as Ivica Ico Bukvic 5/5/10 <ico@bukvic.net> noted */
    if (0 != x->x_connectstate)
    {
        pd_error(x, "%s_connect: already connected to %s:%d on socket %d", objName, x->x_hostname, x->x_port, x->x_fd);
        return;
    }
    /* we get hostname and port and pass them on
       to the child thread that establishes the connection */
    x->x_hostname = hostname->s_name;
    x->x_port = fportno;
    /* start child thread */
    if(pthread_create(&x->x_threadid, &x->x_threadattr, tcpclient_child_connect, x) < 0)
        post("%s: could not create new thread", objName);
}

static void tcpclient_disconnect(t_tcpclient *x)
{
    int i, result;
    void *status;

    if (x->x_fd >= 0)
    {
        for (i = 0; i < MAX_TCPCLIENT_BUFFERS;++i)
        { /* wait for sender thread to finish sending */
            while (x->x_tsp[i].threadisvalid != 0);
        }
        x->x_connectstate = 0; // sender thread looks for this
        result = pthread_join(x->x_sendthreadid, &status);
        //post("tcpclient_disconnect pthread_join result = %d, status = %ld", result, (long)status);
        sys_closesocket(x->x_fd);
        x->x_fd = -1;
        x->x_ourAddr = 0L;
        x->x_ourPort = 0;
        outlet_float(x->x_connectout, 0);
        if (x->x_verbosity) post("%s: disconnected", objName);
    }
    else post("%s: not connected", objName);
}

static void tcpclient_send(t_tcpclient *x, t_symbol *s, int argc, t_atom *argv)
{
#define BYTE_BUF_LEN 65536 // arbitrary maximum similar to max IP packet size
    static char     byte_buf[BYTE_BUF_LEN];
    int             i, j, d;
    unsigned char   c;
    float           f, e;
    size_t          sent = 0;
    char            fpath[FILENAME_MAX];
    FILE            *fptr;

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
                pd_error(x, "%s_send: item %d (%f) is not an integer", objName, i, f);
                return;
            }
            if ((d < 0) || (d > 255))
            {
                pd_error(x, "%s: item %d (%f) is not between 0 and 255", objName, i, f);
                return;
            }
            c = (unsigned char)d;
            byte_buf[j++] = c;
            if (j >= BYTE_BUF_LEN)
            {
                sent += tcpclient_send_buf(x, byte_buf, j);
                j = 0;
            }
        }
        else if (argv[i].a_type == A_SYMBOL)
        {
            atom_string(&argv[i], fpath, FILENAME_MAX);
            fptr = sys_fopen(fpath, "rb");
            if (fptr == NULL)
            {
                post("%s_send: unable to open \"%s\"", objName, fpath);
                return;
            }
            rewind(fptr);
            while ((d = fgetc(fptr)) != EOF)
            {
                c = (char)(d & 0x0FF);
                byte_buf[j++] = c;
                if (j >= BYTE_BUF_LEN)
                {
                    sent += tcpclient_send_buf(x, byte_buf, j);
                    j = 0;
                }
            }
            sys_fclose(fptr);
            fptr = NULL;
            if (x->x_verbosity) post("%s_send: read \"%s\" length %d byte%s", objName, fpath, j, ((d==1)?"":"s"));
        }
        else
        {
            pd_error(x, "%s_send: item %d is not a float or a file name", objName, i);
            return;
        }
    }
    if (j > 0)
    sent += tcpclient_send_buf(x, byte_buf, j);
}

static int tcpclient_send_buf(t_tcpclient *x, char *buf, int buf_len)
{
    t_tcpclient_sender_params   *tsp = &x->x_tsp[x->x_nextbuffer];
    int                         i, max;

    if (x->x_blocked) return 0;
    if (x->x_fd < 0)
    {
        pd_error(x, "%s: not connected", objName);
        x->x_blocked++;
        return 0;
    }
    max = (buf_len > MAX_TCPCLIENT_SEND_BUF)? MAX_TCPCLIENT_SEND_BUF: buf_len;
    while(0 != tsp->threadisvalid); /* wait for thread to clear */
    for (i = 0; i < max; ++i) tsp->x_sendbuf[i] = buf[i];/* copy to our buffer so it doesn't get overwritten */
    tsp->x_buf_len = i;
    tsp->threadisvalid = 1; // tcp_client_sender_loop is looking for this flag
    x->x_nextbuffer = (1+x->x_nextbuffer)%MAX_TCPCLIENT_BUFFERS;
    return max;
}

/* tcp_client_sender_loop runs in its own thread, checking for things to send */
static void *tcp_client_sender_loop(void *w)
{
    t_tcpclient *xx = (t_tcpclient *)w;
    t_tcpclient_sender_params *tsp;
    int i;

    while (0 != xx->x_connectstate)
    {
        for (i = 0; i < MAX_TCPCLIENT_BUFFERS; ++i)
        {
            tsp = &xx->x_tsp[i];
            if ((1 == tsp->threadisvalid) && (tsp->x_buf_len > 0))
            {
                tsp->x_sendresult = send(xx->x_fd, tsp->x_sendbuf, tsp->x_buf_len, 0);
                tsp->x_finished = 1; // poll clock calls tcpclient_sent      
                tsp->threadisvalid = 0; /* this thread is over */
            }
        }
    } // loop while connected
    pthread_exit((void *)w);
}

static void tcpclient_sent(t_tcpclient *x)
{
    t_atom  output_atom;
    int i;
    /* scan sender thread list for completed sends */
    for (i = 0; i < MAX_TCPCLIENT_BUFFERS; ++i)
    {
        if (x->x_tsp[i].x_finished)
        {
            if (x->x_tsp[i].x_sendresult < 0)
            {
                sys_sockerror("tcpclient: send");
                post("%s_sent: could not send data ", objName);
                x->x_blocked++;
                SETFLOAT(&output_atom, x->x_tsp[i].x_sendresult);
                outlet_anything( x->x_statusout, gensym("blocked"), 1, &output_atom);
            }
            else if (x->x_tsp[i].x_sendresult == 0) /* does this ever happen? */
            { /* assume the message is queued and will be sent real soon now */
                SETFLOAT(&output_atom, x->x_tsp[i].x_buf_len);
                outlet_anything( x->x_statusout, gensym("queued"), 1, &output_atom);
            }
            else
            {
                SETFLOAT(&output_atom, x->x_tsp[i].x_sendresult);
                //SETFLOAT(&output_atom, i); // see which buffer was sent
                outlet_anything( x->x_statusout, gensym("sent"), 1, &output_atom);
            }
            x->x_tsp[i].x_finished = 0; 
        }
    }
}

/* Return the send buffer size of socket, also output it on status outlet */
static int tcpclient_get_socket_send_buf_size(t_tcpclient *x)
{
    int                 optVal = 0;
    socklen_t           optLen = sizeof(int);
    t_atom              output_atom;
#ifdef _WIN32
    if (getsockopt(x->x_fd, SOL_SOCKET, SO_SNDBUF, (char*)&optVal, &optLen) == SOCKET_ERROR)
        post("%_get_socket_send_buf_size: getsockopt returned %d\n", objName, WSAGetLastError());
#else
    if (getsockopt(x->x_fd, SOL_SOCKET, SO_SNDBUF, (char*)&optVal, &optLen) == -1)
        post("%_get_socket_send_buf_size: getsockopt returned %d\n", objName, errno);
#endif
    SETFLOAT(&output_atom, optVal);
    outlet_anything( x->x_statusout, gensym("buf"), 1, &output_atom);
    return  optVal;
}

/* Set the send buffer size of socket, returns actual size */
static int tcpclient_set_socket_send_buf_size(t_tcpclient *x, int size)
{
    int optVal = size;
    int optLen = sizeof(int);
#ifdef _WIN32
    if (setsockopt(x->x_fd, SOL_SOCKET, SO_SNDBUF, (char*)&optVal, optLen) == SOCKET_ERROR)
    {
        post("%s_set_socket_send_buf_size: setsockopt returned %d\n", objName, WSAGetLastError());
#else
    if (setsockopt(x->x_fd, SOL_SOCKET, SO_SNDBUF, (char*)&optVal, optLen) == -1)
    {
        post("%s_set_socket_send_buf_size: setsockopt returned %d\n", objName, errno);
#endif
        return 0;
    }
    else return (tcpclient_get_socket_send_buf_size(x));
}

/* Get/set the send buffer size of client socket */
static void tcpclient_buf_size(t_tcpclient *x, t_symbol *s, int argc, t_atom *argv)
{
    float   buf_size = 0;

    if(x->x_connectstate == 0)
    {
        post("%s_buf_size: no clients connected", objName);
        return;
    }
    /* get size of buffer (first element in list) */
    if (argc > 0)
    {
        if (argv[0].a_type != A_FLOAT)
        {
            post("%s_buf_size: specify buffer size with a float", objName);
            return;
        }
        buf_size = atom_getfloatarg(0, argc, argv);
        x->x_fdbuf = tcpclient_set_socket_send_buf_size(x, (int)buf_size);
        if (x->x_verbosity) post("%s_buf_size: set to %d", objName, x->x_fdbuf);
        return;
    }
    x->x_fdbuf = tcpclient_get_socket_send_buf_size(x);
    return;
}

static void tcpclient_rcv(t_tcpclient *x)
{
    int             sockfd = x->x_fd;
    int             ret;
    int             i;
    fd_set          readset;
    fd_set          exceptset;
    struct timeval  ztout;

    if(x->x_connectstate)
    {
        /* check if we can read/write from/to the socket */
        FD_ZERO(&readset);
        FD_ZERO(&exceptset);
        FD_SET(x->x_fd, &readset );
        FD_SET(x->x_fd, &exceptset );

        ztout.tv_sec = 0;
        ztout.tv_usec = 0;

        ret = select(sockfd+1, &readset, NULL, &exceptset, &ztout);
        if(ret < 0)
        {
            pd_error(x, "%s: unable to read from socket", objName);
            sys_closesocket(sockfd);
            return;
        }
        if(FD_ISSET(sockfd, &readset) || FD_ISSET(sockfd, &exceptset))
        {
            /* read from server */
            ret = recv(sockfd, x->x_msginbuf, MAX_TCPCLIENT_SEND_BUF, 0);
            if(ret > 0)
            {
#ifdef DEBUG
                x->x_msginbuf[ret] = 0;
                post("%s: received %d bytes ", objName, ret);
#endif
                if (x->x_dump)tcp_client_hexdump(x, ret);
                for (i = 0; i < ret; ++i)
                {
                    /* convert the bytes in the buffer to floats in a list */
                    x->x_msgoutbuf[i].a_w.w_float = (float)x->x_msginbuf[i];
                }
                /* find sender's ip address and output it */
                x->x_addrbytes[0].a_w.w_float = (x->x_addr & 0xFF000000)>>24;
                x->x_addrbytes[1].a_w.w_float = (x->x_addr & 0x0FF0000)>>16;
                x->x_addrbytes[2].a_w.w_float = (x->x_addr & 0x0FF00)>>8;
                x->x_addrbytes[3].a_w.w_float = (x->x_addr & 0x0FF);
                x->x_addrbytes[4].a_w.w_float = (x->x_port);
                outlet_list(x->x_addrout, &s_list, 5L, x->x_addrbytes);
                /* send the list out the outlet */
                if (ret > 1) outlet_list(x->x_msgout, &s_list, ret, x->x_msgoutbuf);
                else outlet_float(x->x_msgout, x->x_msgoutbuf[0].a_w.w_float);
            }
            else
            {
                if (ret < 0)
                {
                    sys_sockerror("tcpclient: recv");
                    tcpclient_disconnect(x);
                }
                else
                {
                    if (x->x_verbosity) post("%s: connection closed for socket %d\n", objName, sockfd);
                    tcpclient_disconnect(x);
                }
            }
        }
    }
    //else post("%s: not connected", objName);
}

/* tcpclient_poll is the x->x_poll clock callback */
/* Handle the outlets in Pd spacetime */
static void tcpclient_poll(t_tcpclient *x)
{
    tcpclient_rcv(x);	/* try to read in case we're connected */
    tcpclient_sent(x); /* sere if any sends are complete */
    clock_delay(x->x_poll, DEFPOLLTIME);	/* see you later */
}

static void *tcpclient_new(void)
{
    int i;

    t_tcpclient *x = (t_tcpclient *)pd_new(tcpclient_class);
    x->x_msgout = outlet_new(&x->x_obj, &s_anything);	/* received data */
    x->x_addrout = outlet_new(&x->x_obj, &s_list);
    x->x_connectout = outlet_new(&x->x_obj, &s_float);	/* connection state */
    x->x_statusout = outlet_new(&x->x_obj, &s_anything);/* last outlet for everything else */
    //x->x_sendclock = clock_new(x, (t_method)tcpclient_sent);
    x->x_clock = clock_new(x, (t_method)tcpclient_tick);
    x->x_poll = clock_new(x, (t_method)tcpclient_poll);
    x->x_verbosity = 0; /* by default, don't post status changes to main window */
    x->x_fd = -1;
    /* convert the bytes in the buffer to floats in a list */
    for (i = 0; i < MAX_TCPCLIENT_SEND_BUF; ++i)
    {
        x->x_msgoutbuf[i].a_type = A_FLOAT;
        x->x_msgoutbuf[i].a_w.w_float = 0;
    }
    for (i = 0; i < 5; ++i)
    {
        x->x_addrbytes[i].a_type = A_FLOAT;
        x->x_addrbytes[i].a_w.w_float = 0;
    }
    x->x_addr = 0L;
    x->x_blocked = 1;
    x->x_ourAddr = 0L;
    x->x_ourPort = 0;
    x->x_connectstate = 0;
    x->x_nextbuffer = 0;
    /* prepare child threads */
    if(pthread_attr_init(&x->x_threadattr) < 0)
        post("%s: warning: could not prepare child thread", objName);
    if(pthread_attr_setdetachstate(&x->x_threadattr, PTHREAD_CREATE_DETACHED) < 0)
        post("%s: warning: could not prepare child thread", objName);
    if(pthread_attr_init(&x->x_sendthreadattr) < 0)
        post("%s: warning: could not prepare child thread", objName);
    if(pthread_attr_setdetachstate(&x->x_sendthreadattr, PTHREAD_CREATE_JOINABLE) < 0)
        post("%s: warning: could not prepare child thread", objName);
    clock_delay(x->x_poll, 0);	/* start polling the input */
    return (x);
}

static void tcpclient_free(t_tcpclient *x)
{
    if (x->x_verbosity) post("tcpclient_free...");
    tcpclient_disconnect(x);
    clock_free(x->x_poll);
    clock_free(x->x_clock);
    if (x->x_verbosity) post("...tcpclient_free");
}

#ifndef BUILD_DATE
# define BUILD_DATE __DATE__ " " __TIME__
#endif
void tcpclient_setup(void)
{
    char    aboutStr[MAXPDSTRING];

    snprintf(aboutStr, MAXPDSTRING, "%s: (GPL) 20200530 Martin Peach, compiled for pd-%d.%d on %s",
             objName, PD_MAJOR_VERSION, PD_MINOR_VERSION, BUILD_DATE );

#if PD_MAJOR_VERSION==0 && PD_MINOR_VERSION<43
    post(aboutStr);
#else
    logpost(NULL, 3, "%s", aboutStr);
#endif
    tcpclient_class = class_new(gensym(objName), (t_newmethod)tcpclient_new,
        (t_method)tcpclient_free, sizeof(t_tcpclient), 0, 0);
    class_addmethod(tcpclient_class, (t_method)tcpclient_connect, gensym("connect")
        , A_SYMBOL, A_FLOAT, 0);
    class_addmethod(tcpclient_class, (t_method)tcpclient_disconnect, gensym("disconnect"), 0);
    class_addmethod(tcpclient_class, (t_method)tcpclient_send, gensym("send"), A_GIMME, 0);
    class_addmethod(tcpclient_class, (t_method)tcpclient_buf_size, gensym("buf"), A_GIMME, 0);
    class_addmethod(tcpclient_class, (t_method)tcpclient_verbosity, gensym("verbosity"), A_FLOAT, 0);
    class_addmethod(tcpclient_class, (t_method)tcpclient_dump, gensym("dump"), A_FLOAT, 0);
    class_addlist(tcpclient_class, (t_method)tcpclient_send);
}

/* end of tcpclient.c */
