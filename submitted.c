#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <stdio.h>
#include <stdlib.h>

char *copy_charp_backward(char *from, char *to, char *dest) {
	char *last = from - 1;
	char *first = to - 1;
	while (first != last) {
		*dest = *first;
		--first;
		--dest;
	}
	return dest + 1;
}

void xn(int c) {
	if (c < 0) {
		write(2, "Fatal error\n", sizeof "Fatal error");
		exit(1);
	}
}
void xb(int c, int fd) {
	if (c) {
		write(2, "Fatal error\n", sizeof "Fatal error");
		close(fd);
		exit(1);
	}
}

typedef struct {
	int argc;
	char **argv;
} main_type;

typedef main_type* main_ptr;

typedef struct {
	int fd;
	int id_counter;
	struct sockaddr_in address;
	fd_set clients;
	fd_set to_read;
	fd_set to_write;
	int ids[FD_SETSIZE];
	char buffer[16777216];
} server_type;

typedef server_type* server_ptr;

void server_init(server_ptr self, int port) {
	self->fd = socket(AF_INET, SOCK_STREAM, 0);
	xn(self->fd);
	self->id_counter = -1;
	bzero(&self->address, sizeof self->address);
	self->address.sin_family = AF_INET;
	self->address.sin_addr.s_addr = htonl(2130706433); //127.0.0.1
	self->address.sin_port = htons(port);
	xb(bind(self->fd, (const struct sockaddr *)&self->address, sizeof self->address), self->fd);
	xb(listen(self->fd, FD_SETSIZE), self->fd);
	FD_ZERO(&self->clients);
	FD_SET(self->fd, &self->clients);
}

void server_reset_fds(server_ptr self) {
	self->to_read = self->clients;
	self->to_write = self->clients;
}

void server_select(server_ptr self) {
	int dummy;
	server_reset_fds(self);
	dummy = select(FD_SETSIZE, &self->to_read, NULL, NULL, NULL);
	if (dummy > 1)
		dummy = select(FD_SETSIZE, NULL, &self->to_write, NULL, NULL);
}

void server_send_raw(server_ptr self, char *first, size_t len) {
	for (int i = 0; i != self->fd; ++i) {
		if (FD_ISSET(i, &self->to_write))
			send(i, first, len, 0);
	}
	for (int i = self->fd + 1; i != FD_SETSIZE; ++i) {
		if (FD_ISSET(i, &self->to_write))
			send(i, first, len, 0);
	}
}

void server_greet(server_ptr self, int fd) {
	int len = sprintf(self->buffer, "server: client %d just arrived\n", self->ids[fd]);
	server_send_raw(self, self->buffer, len);
}

int server_accept(server_ptr self) {
	int fd;
	if (!FD_ISSET(self->fd, &self->to_read))
		return 0;
	fd = accept(self->fd, 0, 0);
	FD_SET(fd, &self->clients);
	self->ids[fd] = ++self->id_counter;
	server_greet(self, fd);
	return 0;
}

void server_disconnect(server_ptr self, int fd) {
	close(fd);
	int len = sprintf(self->buffer, "server: client %d just left\n", self->ids[fd]);
	server_send_raw(self, self->buffer, len);
	FD_CLR(fd, &self->clients);
	FD_CLR(fd, &self->to_read);
	FD_CLR(fd, &self->to_write);
}

char *unsafe_find_next_line(char *str) {
	while(*str != '\n')
		++str;
	return str + 1;
}

void server_send(server_ptr self, int prefix_len, int msg_len) {
	char *const end = self->buffer + msg_len + prefix_len;
	char *this_line = self->buffer;
	char *next_line = unsafe_find_next_line(this_line + prefix_len);
	server_send_raw(self, this_line, next_line - this_line);
	while (next_line != end) {
		this_line = copy_charp_backward(this_line, this_line + prefix_len, next_line - 1);
		next_line = unsafe_find_next_line(next_line);
		server_send_raw(self, this_line, next_line - this_line);
	}
}

void server_proccess_recieve_fd(server_ptr self, int fd) {
	const int prefix_len = sprintf(self->buffer, "client %d: ", self->ids[fd]);
	int recv_ret = recv(fd, self->buffer + prefix_len, (16777216 - 1) - prefix_len, 0);
	int is_set_write;
	if (recv_ret <= 0) {
		server_disconnect(self, fd);
		return;
	}
	if (self->buffer[recv_ret + prefix_len - 1] != '\n') {
		self->buffer[recv_ret + prefix_len] = '\n';
		++recv_ret;
	}
	is_set_write = FD_ISSET(fd, &self->to_write);
	if (is_set_write)
		FD_CLR(fd, &self->to_write);
	server_send(self, prefix_len, recv_ret);
	if (is_set_write)
		FD_SET(fd, &self->to_write);
}

void server_recieve(server_ptr self) {
	for (int i = 0; i != self->fd; ++i) {
		if (FD_ISSET(i, &self->to_read))
			server_proccess_recieve_fd(self, i);
	}
	for (int i = self->fd  + 1; i != FD_SETSIZE; ++i) {
		if (FD_ISSET(i, &self->to_read))
			server_proccess_recieve_fd(self, i);
	}
}

void server_iteration(server_ptr self) {
	server_select(self);
	if (server_accept(self))
		return;
	server_recieve(self);
}

void server_loop(server_ptr self) {
	for (;;)
		server_iteration(self);
}

void main_check(main_type args) {
	if (args.argc != 2) {
		write(2, "Wrong number of arguments\n", sizeof "Wrong number of arguments");
		exit(1);
	}
}

void main_server(main_type args) {
	server_ptr server = malloc(sizeof(server_ptr));
	if (!server) {
		write(2, "Fatal error\n", sizeof "Fatal error\n");
		exit(1);
	}
	const int port = atoi(args.argv[1]);
	server_init(server, port);
	server_loop(server);
}

int main(int argc, char *argv[]) {
	main_type args = {argc, argv};
	main_check(args);
	main_server(args);
}
