/** net.h
 *
 * Network library for Banana.
 */

#ifndef _MY_NET_H_
#define _MY_NET_H_

struct user;
struct world;

int net_init();
void net_poll();

inline void net_lock();
inline void net_unlock();
void net_connect(struct world *world, char *host, char *port);
void net_disconnect(struct world *world);
void net_send(World *world, char *text);

#endif
