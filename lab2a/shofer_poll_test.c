/* poll_input.c
    Licensed under GNU General Public License v2 or later.
*/
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/sysmacros.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#define errExit(msg)    do { perror(msg); exit(EXIT_FAILURE); \
                        } while (0)

volatile sig_atomic_t stop = 0;

void init(void)
{
    char str[64];
    system("./load_shofer");
    struct stat st;
    dev_t dev;
    for (int i = 0; i < 6; ++i) 
    {
        snprintf(str, sizeof(str), "/dev/shofer%d", i);

        if (stat(str, &st) == -1) 
        {
            perror("stat");
            continue;
        }

        printf("%s -> Major: %u, Minor: %u\n",
               str,
               major(st.st_rdev),
               minor(st.st_rdev));

        //mknod(str, S_IFCHR | 0666, dev);
    }
}

void intHandler(int sig) 
{
    stop = 1;
}

void pollTask(unsigned int rw, char *argv[])
{
    int            ready;
    char           buf;
    nfds_t         num_open_fds, nfds;
    ssize_t        s;
    struct pollfd  *pfds;

    num_open_fds = nfds = atoi(argv[1]);
    pfds = calloc(nfds, sizeof(struct pollfd));
    if (pfds == NULL)
        errExit("malloc");

    // Open each file on command line, and add it to 'pfds' array.
    char devPath[13];
    for (nfds_t j = 0; j < nfds; j++) {
        snprintf(devPath, sizeof(devPath), "/dev/shofer%d", (int)j);
        if (rw == 0)
            pfds[j].fd = open(devPath, O_RDONLY);
        else
            pfds[j].fd = open(devPath, O_WRONLY | O_NONBLOCK);
        
        if (pfds[j].fd == -1)
            errExit("open");
        
        if (rw == 1)
            printf("\tOpened \"%s\" on fd %d\n", devPath, pfds[j].fd);
        else
            printf("Opened \"%s\" on fd %d\n", devPath, pfds[j].fd);
        
        if (rw == 0)
        {
            pfds[j].events = POLLIN;
        }
        else
        {
            pfds[j].events = POLLOUT;
        }
        
        memset(devPath, 0, strlen(devPath));
    }
    // Keep calling poll() as long as at least one file descriptor is open
    while (num_open_fds && !stop) 
    {
        if (rw == 1)
            printf("\tAbout to poll()\n");
        else
            printf("About to poll()\n");
        ready = poll(pfds, nfds, -1);
        if (ready == -1)
        {
            if (errno == EINTR)
            {
                if (stop) {break;}
                else continue;
            }
            else
            {
                errExit("poll");
            }
        }
        printf("Ready: %d\n", ready);

        // Deal with array returned by poll(). 
        nfds_t writable[6] = {0};
        int i = 0;
        for (nfds_t j = 0; j < nfds; j++) 
        {
            if (pfds[j].revents != 0) 
            {
                if (rw == 0)
                {
                    printf("  fd=%d; events: %s%s%s\n", pfds[j].fd,
                        (pfds[j].revents & POLLIN) ? "POLLIN "  : "",
                        (pfds[j].revents & POLLHUP) ? "POLLHUP " : "",
                        (pfds[j].revents & POLLERR) ? "POLLERR " : "");
                    if (pfds[j].revents & POLLIN)
                    {
                        s = read(pfds[j].fd, &buf, sizeof(buf));
                        if (s == -1)
                            errExit("read");
                        printf("    read %zd byte: %c\n", s, buf);
                    }
                }
                else if (rw == 1)
                {
                    printf("\t  fd=%d; events: %s%s%s\n", pfds[j].fd,
                        (pfds[j].revents & POLLOUT) ? "POLLOUT " : "",
                        (pfds[j].revents & POLLHUP) ? "POLLHUP " : "",
                        (pfds[j].revents & POLLERR) ? "POLLERR " : "");
                    buf = 'p';
                    if (pfds[j].revents & POLLOUT)
                    {
                        writable[i] = j;
                        i = i + 1;
                    }
                }
            }
        }
        if (rw == 1 && i > 0)
        {
            printf("writable fds:\n");
            for (int k = 0; k < i; ++k)
            {
                printf("%ld, ", writable[k]);
            }
            printf("\n");
            nfds_t rand_index = rand() % i; 
            printf("rand_index: %ld\n", rand_index);
            s = write(pfds[writable[rand_index]].fd, &buf, sizeof(buf));
            if (s == -1)
                if (errno == EINTR && stop)
                {
                    for (nfds_t j = 0; j < nfds; j++)
                    {
                        close(pfds[j].fd);
                    }
                    printf("\n\tAll file descriptors closed; bye\n");
                    free(pfds);
                    break;
                }
                else errExit("write");
            printf("    write %zd byte: %c\n", s, buf);
            sleep(5);
            i = 0;
            memset(writable, -1, sizeof(writable));
        }
    }

    for (nfds_t j = 0; j < nfds; j++)
    {
        close(pfds[j].fd);
    }
    if (rw == 1)
        printf("\n\tAll file descriptors closed; bye\n");
    else
        printf("\nAll file descriptors closed; bye\n");
    free(pfds);
}

int main(int argc, char *argv[])
{
    init();
    srand(time(NULL));
    signal(SIGINT, intHandler);

    if (argc < 2) {
        fprintf(stderr, "Usage: %s DRIVER_NUM\n", argv[0]);
        system("./unload_shofer");
        exit(EXIT_FAILURE);
    }

    if (fork() == 0) 
    {
        pollTask(1, argv);
    }
    else 
    {
        pollTask(0, argv);
        wait(NULL);
        system("./unload_shofer");
    }
    return 0;
}