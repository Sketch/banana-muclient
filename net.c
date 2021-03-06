/* net.c
 *
 * Network library for connecting to mu*s.
 */

#include "banana.h"

#include "telnet.h"
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <signal.h>
#include <iconv.h>

#define MAX_EVENTS 20

static int epfd;
static pthread_mutex_t sockmutex;
static pthread_key_t pt_key;

struct new_conn_info {
  // Host is strdup'd, so free.
  char *host;
  char *port;
  World *world;
};

inline void
net_lock() {
  noisy_lock(&sockmutex, "sock");
}

inline void
net_unlock() {
  noisy_unlock(&sockmutex, "sock");
}

void
net_sigusr1(int sig _unused_) {
  int *fd;
  struct epoll_event epvt;
  fd = pthread_getspecific(pt_key);
  if (fd) {
    epoll_ctl(epfd, EPOLL_CTL_DEL, *fd, &epvt);
    close(*fd);
  }
}

static void *
get_in_addr(struct sockaddr *sa)
{
  if (sa->sa_family == AF_INET) {
    return &(((struct sockaddr_in*)sa)->sin_addr);
  }

  return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

// TODO: Figure out locking between threads? Or trust the other threads
// to kill this one when appropriate?
// Thank you, Beej, for your awesome code and tutorial.
static void *
thread_connect(void *arg) {
  struct new_conn_info *nci = (struct new_conn_info *) arg;
  char host[200];
  char port[20];
  World *world;
  struct epoll_event epvt;

  int sockfd;
  int rv;
  struct addrinfo hints, *servinfo, *p;

  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  // Copy over from nci.
  snprintf(host, 200, "%s", nci->host);
  snprintf(port, 20, "%s", nci->port);
  world = nci->world;

  // Free nci
  free(nci->host);
  free(nci->port);
  free(nci);

  rv = getaddrinfo(host, port, &hints, &servinfo);

  if (rv != 0) {
    messageEvent(world->user, world, EVENT_WORLD_CONNFAIL, "cause",
                 "Connect failed: '%s'.", gai_strerror(rv));
    world->netstatus = WORLD_DISCONNECTED;
    return NULL;
  }

  // Constantly check if we stop connecting. Heh. Should only happen
  // on a close or disconnect, so no events.
  messageEvent(world->user, world, EVENT_WORLD_CONNECTING, "status",
               "Resolved host.");

  // loop through all the results and connect to the first we can
  net_lock();
  for(p = servinfo; p != NULL; p = p->ai_next) {
    // Make a socket for the given type.
    if ((sockfd = socket(p->ai_family, p->ai_socktype,
            p->ai_protocol)) == -1) {
      // perror("client: socket");
      continue;
    }

    // Connect it.
    if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
      close(sockfd);
      // perror("client: connect");
      continue;
    }

    break;
  }
  if (p && sockfd >= 0) {
    pthread_setspecific(pt_key, &sockfd);
  }
  net_unlock();

  if (p == NULL) {
    messageEvent(world->user, world, EVENT_WORLD_CONNFAIL, "cause",
                 "Failed to connect.");
    world->netstatus = WORLD_DISCONNECTED;
    return NULL;
  }

  // Copy the ipaddr over.
  inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr),
      world->ipaddr, 49);

  // Set the sockfd opts we need: Nonblock 
  if (1) {
    struct linger linger;
    int flags;
   
    // NONBLOCKING
    flags = fcntl(sockfd, F_GETFL, 0);
    fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);

    // LINGERING
    linger.l_onoff = 1;
    linger.l_linger = 5;
    setsockopt(sockfd, SOL_SOCKET, SO_LINGER, &linger, sizeof(linger));
  }

  net_lock();

  world->fd = sockfd;
  // Now to add this to epoll.
  epvt.data.ptr = world;
  epvt.events = EPOLLIN | EPOLLERR;

  epoll_ctl(epfd, EPOLL_CTL_ADD, sockfd, &epvt);

  world->connectTime = time(NULL);

  llog(world->logger, "-- WORLD CONNECTED to '%s' port '%s' --", host, port);
  slog("Connected world '%s' for user '%s'", world->name, world->user->name);

  net_unlock();

  world->netstatus = WORLD_CONNECTED;
  messageEvent(world->user, world, EVENT_WORLD_CONNECT, "ipaddr",
               "%s", world->ipaddr);

  // Clear this thread from world.
  memset(&world->connthread, 0, sizeof(pthread_t));

  // And we're done =).
  return NULL;
}

void
net_connect(World *world, char *host, char *port) {
  struct new_conn_info *nci;

  // First, ensure it's not already connecting.
  if (world->netstatus != WORLD_DISCONNECTED) {
    messageEvent(world->user, world, EVENT_WORLD_CONNFAIL, "cause",
                 "Socket for '%s' already exists.", world->name);
  } else {
    nci = malloc(sizeof(struct new_conn_info));
    nci->port = strdup(port);
    nci->host = strdup(host);
    nci->world = world;
    world->netstatus = WORLD_CONNECTING;

    // And spawn a new thread to do the connection.
    pthread_create(&world->connthread, NULL, thread_connect, (void *) nci);
    messageEvent(world->user, world, EVENT_WORLD_CONNECTING, "status",
                 "Beginning connect.");
  }
}

// #define DEBUG_NET
#ifdef DEBUG_NET
static int writing = -1;
static void
write_dbg(int fd, unsigned char *text, int len) {
  int i;
  char c;
  // Debug prints.
  for (i = 0; i < len; i++) {
    c = text[i] & 0xFF;
    if (writing != 1) { printf("\n* "); writing = 1; }
    printf("%.2X ", c & 0xff);
  }
  send(fd, text, len, MSG_DONTWAIT);
}

int
recv_debug(int fd, unsigned char *ptr, int size, int flags) {
  int ret = recv(fd, ptr, size, flags);
  int i;
  char c;
  for (i = 0; i < ret; i++) {
    c = ptr[i] & 0xFF;
    if (writing != 0) { printf("\n"); writing = 0; }
    printf("%.2X ", c & 0xff);
  }
  return ret;
}
#define recv_raw(f, p, c, fl) recv_debug(f, (unsigned char *) p, c, fl)
#define write_raw(f,t,l) write_dbg(f,(unsigned char *) t,l)
#else
#define recv_raw(f, p, c, fl) recv(f, p, c, fl)
#define write_raw(f,t,l) send(f,(unsigned char *) t,l, MSG_DONTWAIT)
#endif

int write_stoppers[0x100];

void
net_send(World *world, char *text) {
  char buff[BUFFER_LEN];
  int i, j;
  noisy_lock(&world->user->mutex, world->user->name);
  if (world->fd >= 0) {
    j = 0;
    for (i = 0; text[i]; i++) {
      if (write_stoppers[(unsigned char) *text]) {
        if (text[i] == (char) _IAC) {
          buff[j++] = text[i];
          buff[j++] = text[i];
        } else if (text[i] == '\n') {
          buff[j++] = '\r';
          buff[j++] = '\n';
          write_raw(world->fd, buff, j);
          j = 0;
        }
      } else {
        buff[j++] = text[i];
      }
      if (j >= (BUFFER_LEN - 8)) {
        // Send what we have so far.
        write_raw(world->fd, buff, j);
        j = 0;
      }
    }
    buff[j++] = '\r';
    buff[j++] = '\n';
    if (j > 0) {
      write_raw(world->fd, buff, j);
      j = 0;
    }
  } else {
    messageEvent(world->user, world, EVENT_WORLD_ERROR, "cause",
                 "Not connected.");
  }
#ifdef DEBUG_NET
  printf("\n");
  writing = -1;
#endif
  noisy_unlock(&world->user->mutex, world->user->name);
}

void
net_disconnect(World *world) {
  int sockfd = world->fd;
  int status = world->netstatus;
  struct epoll_event epvt;
  pthread_t connthread = world->connthread;
  world->fd = -1;
  world->netstatus = WORLD_DISCONNECTED;
  world->connthread = 0;

  // 
  if (status == WORLD_CONNECTING && connthread) {
    pthread_kill(connthread, SIGUSR1);
  }
  if (sockfd >= 0) {
    epoll_ctl(epfd, EPOLL_CTL_DEL, sockfd, &epvt);
    close(sockfd);
  }
}

int
net_init() {
  // Stuff for the new_connection-spawned threads.
  signal(SIGUSR1, net_sigusr1);
  pthread_key_create(&pt_key, NULL);

  memset(write_stoppers, 0, sizeof(write_stoppers));
  write_stoppers['\r'] = 1;
  write_stoppers['\n'] = 1;
  write_stoppers[_IAC] = 1;

  pthread_mutex_init(&sockmutex, &pthread_recursive_attr);
  epfd = epoll_create(10);
  if (epfd < 0) {
    perror("epoll_create");
    return 1;
  }
  return 0;
}

static void
writebuf(char *buff _unused_, char **bp, char *what, int len) {
  memcpy(*bp, what, len);
  *bp += len;
}

static void
writebuf_escaped(char *buff _unused_, char **bp, char *what, int len) {
  int i;
  for (i = 0; i < len; i++) {
    *(*bp)++ = what[i];
    if (what[i] == (char) _IAC) {
      *(*bp)++ = what[i];
    }
  }
}

void
send_charsets(World *w) {
  char buff[100], *bp = buff;
  char *charsets = ";UTF-8;ISO-8859-1;ISO-8859-2;US-ASCII;CP437";
  writebuf(buff, &bp, IAC SB CHARSET CHARSET_REQUEST, 4);
  writebuf_escaped(buff, &bp, charsets, strlen(charsets));
  writebuf(buff, &bp, IAC SE, 2);
  write_raw(w->fd, buff, bp - buff);
}

void
send_naws(World *w, int width, int height) {
  char buff[100], *bp = buff;

  char bs[4];
  writebuf(buff, &bp, IAC SB NAWS, 3);
  bs[0] = width >> 8 & 0xFF;
  bs[1] = width & 0xFF;
  bs[2] = height >> 8 & 0xFF;
  bs[3] = height & 0xFF;
  writebuf_escaped(buff, &bp, bs, 4);
  writebuf(buff, &bp, IAC SE, 2);
  write_raw(w->fd, buff, bp - buff);
}

void
send_ttype(World *w, char *what) {
  char buff[100], *bp = buff;
  writebuf(buff, &bp, IAC SB TTYPE IS, 4);
  writebuf_escaped(buff, &bp, what, strlen(what));
  writebuf(buff, &bp, IAC SE, 2);
  write_raw(w->fd, buff, bp - buff);
}

// When we get our first IAC, we send this.
void
try_iac(World *w) {
  char buff[100], *bp = buff;
  writebuf(buff, &bp, IAC WILL CHARSET, 3);
  write_raw(w->fd, buff, bp - buff);
}

#define readb(b) b = 0; if (recv_raw(w->fd, &b, 1, 0) < 1) return -1;
int
iac_do(World *w, int b) {
  switch (b) {
  case _LINEMODE:
    write_raw(w->fd, IAC WONT LINEMODE, 3);
    break;
  case _TTYPE:
    write_raw(w->fd, IAC WILL TTYPE, 3);
    break;
  case _NAWS:
    write_raw(w->fd, IAC WILL NAWS, 3);
    send_naws(w, 92, 19);
    break;
  case _CHARSET:
    send_charsets(w);
    break;
  }
  return 0;
}

int
iac_will(World *w, int b) {
  switch (b) {
  case _EOR:
    write_raw(w->fd, IAC DO EOR, 3);
    break;
  case _MSSP:
    write_raw(w->fd, IAC DO MSSP, 3);
    break;
  case _NAWS:
    write_raw(w->fd, IAC DO NAWS, 3);
    send_naws(w, 92, 19);
    break;
  case _CHARSET:
    write_raw(w->fd, IAC DO CHARSET, 3);
    break;
  }
  return 0;
}

int
iac_sb(World *w, int b) {
  char charset[20];
  int i;
  switch (b) {
  case _TTYPE:
    readb(b); // SEND
    readb(b); // IAC
    readb(b); // SE
    send_ttype(w, "Banana");
    break;
  case _MSSP:
    while (1) {
      readb(b);
      if (b == _IAC) {
        readb(b);
        if (b != _IAC) {
          return 0;
        }
      }
    }
    break;
  case _CHARSET:
    readb(b);
    if (b != _CHARSET_ACCEPTED) {
      readb(b); // IAC
      readb(b); // SE
    } else {
      for (i = 0; i < 20; i++) {
        readb(b);
        if (b == _IAC) break;
        charset[i] = b;
      }
      if (i == 20) {
        while (b != _IAC) {
          readb(b);
        }
      }
      readb(b); // SE
      slog("%s.%s: Charset '%s' accepted.", w->user->name, w->name, charset);
      snprintf(w->charset, 20, "%s", charset);
    }
  }
  return 0;
}

int
handle_iac(World *w, int b) {
  // W got an IAC command. If we haven't dealt with IAC before, then
  // send our own connection string.
  if (w->tried_iac == 0) {
    w->tried_iac = 1;
    try_iac(w);
  }
  switch (b) {
  case _DO:
    readb(b);
    return iac_do(w,b);
  case _WILL:
    readb(b);
    return iac_will(w,b);
  case _EOR:
  case _GA:
    return 1;
  case _SB:
    readb(b);
    return iac_sb(w,b);
  case _DONT:
  case _WONT:
    readb(b);
    // Okay, we won't?
  }
  return 0;
}
#undef readb

#define MAX_LINES 200
// raw_read: Returns 1 if the socket is closed, otherwise 0.
int
raw_read(World *w) {
  int   ret;
  char *buff = w->buff;
  int  *lbp = &w->lbp;
  char *lines[MAX_LINES];
  char *prompt = NULL;
  int   nlines = 0;
  int   i;
  int   b = 0;

#define readb(b) b = 0; if ((ret = recv_raw(w->fd, &b, 1, 0)) < 1) goto exit_sequence
  while (1) {
    readb(b);
    switch (b) {
    case '\r':
      break;
    case '\n':
      buff[*lbp] = '\0';
      lines[nlines++] = convert_to_json(w,buff);
      *lbp = 0;
      if (nlines >= MAX_LINES) {
        // Don't read no more, just wait until next one.
        goto exit_sequence;
      }
      break;
    case _IAC:
      readb(b);
      if (b != _IAC) {
        ret = handle_iac(w,b);
        if (ret < 0) {
          goto exit_sequence;
        } else if (ret > 0) {
          buff[*lbp] = '\0';
          prompt = convert_to_json(w,buff);
          goto exit_sequence;
        }
        break;
      }
    default:
      // TODO: If lbp starts approaching being  too big,
      // push the line as it is. Maybe at buffer_len - 30, start looking
      // for spaces?
      buff[(*lbp)++] = b;
    }
  }
#undef readb

exit_sequence:
  if (nlines > 0) {
    addLines(w->user, w, lines, nlines);
    for (i = 0; i < nlines; i++) {
      free(lines[i]);
    }
  }
  if (prompt) {
    queueEvent(w->user, w, 1, EVENT_WORLD_PROMPT,
               "world:'%s',text:'%s'", w->name, prompt);
    free(prompt);
  }
  return ret == 0;
}

void
net_read(World *w) {
  struct epoll_event epvt;
  net_lock();
  noisy_lock(&w->user->mutex, w->user->name);

  // Read data. Just throw it away for now.
  if (raw_read(w)) {
    epoll_ctl(epfd, EPOLL_CTL_DEL, w->fd, &epvt);
    world_dc_event(w->user, w);
    w->fd = -1;
    w->netstatus = WORLD_DISCONNECTED;
  }
#ifdef DEBUG_NET
  printf("\n");
  writing = -1;
#endif
  noisy_unlock(&w->user->mutex, w->user->name);
  net_unlock();
}

void
net_poll() {
  struct epoll_event events[MAX_EVENTS];
  time_t now = time(NULL);
  time_t next = time(NULL) + CLEANUP_TIMEOUT;
  int numevents;
  struct epoll_event epvt;
  World *w;
  int i;

  while (1) {
    numevents = epoll_wait(epfd, events, MAX_EVENTS, (next - now) * 1000);
    net_lock();
    for (i = 0; i < numevents; i++) {
      w = (World *) events[i].data.ptr;
      // Make sure we haven't been removed for bad behaviour.
      // TODO: A better algorithm?
      if (w->netstatus != WORLD_CONNECTED) continue;
      // If we got here, we have valid data to process. Or rather,
      // we have a valid socket to work on.
      if (events[i].events & EPOLLIN) {
        net_read(w);
      }
      if (events[i].events & EPOLLERR) {
        close(w->fd);
        epoll_ctl(epfd, EPOLL_CTL_DEL, w->fd, &epvt);
        world_dc_event(w->user, w);
        w->fd = -1;
        w->netstatus = WORLD_DISCONNECTED;
      }
    }
    now = time(NULL);
    if (now >= next) {
      sessions_expire();
      users_cleanup();
      next = now + CLEANUP_TIMEOUT;
    }
    net_unlock();
  }
}
