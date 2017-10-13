#include <signal.h>
#include <errno.h>
#include <stdlib.h>
#include <pthread.h>
#include <poll.h>
#include <sys/epoll.h>
#include <sys/ptrace.h>

#include <r_table.h>
#include <service.h>
#include <vu_fd_table.h>
#include <syscall_defs.h>
#include <umvu_peekpoke.h>
#include <vu_slow_calls.h>

#define SIZEOF_SIGSET (_NSIG / 8)


struct slowcall {
	int epfd;
	pid_t pid;
	//char *stack[2048 - sizeof(int) - sizeof(pid_t)];
};

struct slowcall *vu_slowcall_in(struct vuht_entry_t *ht, int fd, uint32_t events, int nested) {
	void *private = NULL;
	int sfd = vu_fd_get_sfd(fd, &private, nested);
	int epfd = r_epoll_create1(EPOLL_CLOEXEC);
	struct epoll_event event = {.events = events, .data.fd = fd};
	int ret_value = service_syscall(ht, __VU_epoll_ctl)(epfd, EPOLL_CTL_ADD, sfd, &event);
	//printk("vu_slowcall_in... %d (add %d)\n", epfd, ret_value);
	if (ret_value < 0) {
		r_close(epfd);
		return NULL;
	} else {
		struct slowcall *sc = malloc(sizeof(struct slowcall));
		sc->epfd = epfd;
		return sc;
	}
}

static void slow_thread(int epfd) {
	//struct epoll_event useless;
	struct pollfd pfd = {epfd, POLLIN, 0};
	//printk("vu_slowcall_during... %d\n", epfd);
	//int ret_value = r_epoll_wait(epfd, &useless, 1, -1);
	poll(&pfd, 1, -1);

	//printk("vu_slowcall_wakeup %d %d\n", ret_value, errno);
}	

void vu_slowcall_during(struct slowcall *sc) {
	//printk(">>>>>>>>>%lu\n", pthread_self());

	if ((sc->pid = r_fork()) == 0) {
		slow_thread(sc->epfd);
		r_exit(1);
	}
	//printk(">>>>>>>>> NEW %d\n", newthread);
}

int vu_slowcall_out(struct slowcall *sc, struct vuht_entry_t *ht, int fd, uint32_t events, int nested) {
	void *private = NULL;
  int sfd = vu_fd_get_sfd(fd, &private, nested);
	int rv = r_kill(sc->pid, SIGTERM);
	struct epoll_event event = {.events = events, .data.fd = fd};
	//printk("vu_slowcall_wakeup...\n");
	service_syscall(ht, __VU_epoll_ctl)(sc->epfd, EPOLL_CTL_DEL, sfd, &event);
	r_close(sc->epfd);
	free(sc);
	return rv;
}
