#include <sys/types.h>

#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <pthread.h>
#include <memory.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>

#define PORT_NUMBER 7983
#define BUF_SIZE 40960
#define WRITE_SIZE 256

class Trace
{
#ifdef TRACE
  const char *name;
#endif
public:
  Trace(const char *n)
  {
#ifdef TRACE
    name = n;
    fprintf(stderr, "entering %s\n", n);
    fflush(stderr);
#endif
  }
  void Log(const char *str)
  {
#ifdef TRACE
    fprintf(stderr, "%s", str);
    fflush(stderr);
#endif
  }
  template <typename... T>
  void Log(const char *str, T... args)
  {
#ifdef TRACE
    fprintf(stderr, str, args);
    fflush(stderr);
#endif
  }
  ~Trace()
  {
#ifdef TRACE
    fprintf(stderr, "exiting %s\n", name);
#endif
  }
};

struct thread_data
{
  int sock1, sock2;

  thread_data(int s1, int s2) : sock1(s1), sock2(s2) {}
};

int read_to_buffer(int sock, char *buf, int &offset)
{
  Trace trace("read_to_buffer");

  long available;

  ioctl(sock, FIONREAD, &available, sizeof(available));

  int remaining = BUF_SIZE - offset;

  int bytes_to_get = available;
  if (bytes_to_get > remaining)
    bytes_to_get = remaining;

  trace.Log("available = %d, remaining = %d, bytes_to_get = %d\n", available, remaining, bytes_to_get);

  int starting_offset = offset;

  while (bytes_to_get > 0)
  {
    trace.Log("buf address = %p, offset = %d, length = %d\n", buf, offset, bytes_to_get);

    int got = recv(sock, buf + offset, bytes_to_get, 0);

    if (got <= 0)
    {
      int errno_val = errno;

      trace.Log("recv() returned %d, errno = %d\n", got, errno);
      return got;
    }

    offset += got;
    bytes_to_get -= got;
  }

  return (offset - starting_offset);
}

int write_from_buffer(int sock, char *buf, int &offset)
{
  Trace trace("write_from_buffer");

  long original_flags;

  original_flags = fcntl(sock, F_GETFL, 0);

  fcntl(sock, F_SETFL, original_flags | O_NONBLOCK);

  int write_offset = 0;

  while (write_offset < offset)
  {
    int bytes_to_send = 512;

    if (bytes_to_send > offset - write_offset)
      bytes_to_send = offset - write_offset;

    int sent = send(sock, buf + write_offset, offset - write_offset, 0);

    if (sent <= 0)
    {
      int errno_val = errno;

      trace.Log("send() returned %d, errno == %d (EAGAIN == %d)\n", sent, errno_val, EAGAIN);
      if (errno_val == EAGAIN)
        break;
      return sent;
    }

    write_offset += sent;
  }

  fcntl(sock, F_SETFL, original_flags);

  memmove(buf, buf + write_offset, offset - write_offset);

  offset -= write_offset;

  return write_offset;
}

void *thread_proc(void *arg)
{{
  Trace trace("thread_proc");

  thread_data *data = (thread_data *)arg;

  int sock1 = data->sock1;
  int sock2 = data->sock2;

  delete data;

  int cookie1;

  //char sock1_buf[BUF_SIZE], sock2_buf[BUF_SIZE];
  char *sock1_buf = new char[BUF_SIZE], *sock2_buf = new char[BUF_SIZE];
  int sock1_buf_offset = 0, sock2_buf_offset = 0;

  int max_fd;

  int cookie2;

  cookie1 = rand();
  cookie2 = cookie1;

  if (sock1 > sock2)
    max_fd = sock1;
  else
    max_fd = sock2;

  while (true)
  {
    fd_set want_read, want_write;

    FD_ZERO(&want_read);
    FD_ZERO(&want_write);

    if (sock1_buf_offset < BUF_SIZE)
      FD_SET(sock1, &want_read);
    if (sock2_buf_offset < BUF_SIZE)
      FD_SET(sock2, &want_read);
    if (sock1_buf_offset > 0)
      FD_SET(sock2, &want_write);
    if (sock2_buf_offset > 0)
      FD_SET(sock1, &want_write);

    timeval tv;

    tv.tv_sec = 10;
    tv.tv_usec = 0;

    int matches = select(max_fd + 1, &want_read, &want_write, NULL, &tv);

    if (matches == 0)
      continue;

    if (FD_ISSET(sock1, &want_read))
      if (read_to_buffer(sock1, sock1_buf, sock1_buf_offset) <= 0)
        break;
    if (FD_ISSET(sock2, &want_read))
      if (read_to_buffer(sock2, sock2_buf, sock2_buf_offset) <= 0)
        break;
    if (FD_ISSET(sock1, &want_write))
      if (write_from_buffer(sock1, sock2_buf, sock2_buf_offset) <= 0)
        break;
    if (FD_ISSET(sock2, &want_write))
      if (write_from_buffer(sock2, sock1_buf, sock1_buf_offset) <= 0)
        break;
  }

  delete[] sock1_buf;
  delete[] sock2_buf;

  close(sock1);
  close(sock2);

  if (cookie1 == cookie2)
    trace.Log("cookies are the same, the stack is probably fine\n");
  else
    trace.Log("warning! %d != %d, stack was probably overwritten!\n", cookie1, cookie2);

}pthread_exit(NULL);}

int main()
{
  Trace trace("main");

  srand(time(NULL));

  int listen_fd = socket(AF_INET, SOCK_STREAM, 0);

  int on = 1;

  setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

  sockaddr_in sin;

  memset(&sin, 0, sizeof(sin));
  sin.sin_family = AF_INET;
  sin.sin_port = htons(PORT_NUMBER);

  if (bind(listen_fd, (struct sockaddr *)&sin, sizeof(sin)))
    return 1;

  if (listen(listen_fd, 5))
    return 1;

  while (true)
  {
    socklen_t sin_len = sizeof(sin);

    int new_client_1 = accept(listen_fd, (struct sockaddr *)&sin, &sin_len);
    int new_client_2 = accept(listen_fd, (struct sockaddr *)&sin, &sin_len);

    if ((new_client_1 < 0) || (new_client_2 < 0))
    {
      if (new_client_1 >= 0)
        close(new_client_1);
      if (new_client_2 >= 0)
        close(new_client_2);

      continue;
    }

    pthread_t tid;

    pthread_create(&tid, NULL, thread_proc, new thread_data(new_client_1, new_client_2));
    pthread_detach(tid);
  }
}
