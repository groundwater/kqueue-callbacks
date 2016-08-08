#import <iostream>
#import <netinet/in.h>
#import <sys/event.h>
#import <sys/socket.h>
#import <sys/time.h>
#import <sys/types.h>
#import <unistd.h>
#import <vector>
#import <cassert>

class Socket {};

struct Event {
    Event(uintptr_t i, u_short ff, short f): ident{i}, filter{f}, flags{ff} {};

    uintptr_t ident;
    short filter;
    u_short flags;
};

using FN = std::function<void(struct kevent&)>;

class EventHandle {
private:
    friend class EventLoop;
    FN* func;
    EventHandle(FN* n): func{n} {}
public:
    void clear() {
        delete func;
    }
};

class EventLoop {
private:
    int queue;
public:
    EventLoop(int q): queue {q} {}
    EventLoop(): EventLoop(kqueue()) {}

    EventHandle handle(Event event, FN fn) {
        
        // copy the lambda onto the heap
        // we need to keep this around well after this stack unwinds
        auto func = new FN(fn);

        // The EventHandle is the way we can clean up after ourselves later.
        // Be sure to call .clear() after the lambda finishes executing.
        // Remember we support a lambda running 0-N times. 
        EventHandle eh(func);

        struct kevent e {
            .ident = event.ident,
            .filter = event.filter,
            .flags = event.flags,
            .udata = func,
        };

        // if you only want to set a new listener, set arg 3/4 to (NULL, 0) respectively
        // n should always be 0
        int n = kevent(queue, &e, 1, NULL, 0, NULL);
        assert(n == 0);

        return eh;
    }

    EventHandle handle(Event event) {
        struct kevent e {
            .ident = event.ident,
            .filter = event.filter,
            .flags = event.flags,
        };
        int n = kevent(queue, &e, 1, NULL, 0, NULL);

        return EventHandle {nullptr};
    }


    void next() {
        struct kevent e;
        int n = kevent(queue, NULL, 0, &e, 1, NULL);
        auto fn = static_cast<FN *>(e.udata);

        (*fn)(e);
    }
};

int main () {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in in_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(8080),
        .sin_addr.s_addr = INADDR_ANY,
    };
    bind(sockfd, (struct sockaddr*) &in_addr, sizeof(in_addr));
    listen(sockfd, 10);
    
    EventLoop el;

    // being callbacks!
    EventHandle listen_handle = el.handle(Event(sockfd, EV_ADD|EV_ENABLE, EVFILT_READ), [=, &el](struct kevent& e){
        // this callback runs when accept() is ready to not block 

        sockaddr_in cliaddr;
        socklen_t len;
        
        // should not block
        int fd = accept(sockfd, (struct sockaddr*) &cliaddr, &len);

        EventHandle read_handle = el.handle(Event(fd, EV_ADD|EV_ENABLE, EVFILT_READ), [=, &el, &read_handle](struct kevent& e){
            // this callback runs when read() is ready to not block

            // the .data property holds the number of bytes to be read 
            const int N = (int) e.data;

            char* buff = new char[N + 1];
            buff[N] = '\0';
            
            // read only the bytes ready to be read
            // assert( n == N )
            int n = read(fd, buff, N);
            assert(n == N);

            // just dump everything to stdout atm
            std::cout << std::string(buff);

            // if the socket is closed, the EV_EOF flag will be set
            // we should remove the kqueue listener, and delete the associated closure
            if (e.flags & EV_EOF) {
                el.handle(Event(fd, EV_DELETE, EVFILT_READ));
                read_handle.clear();
            }

            delete[] buff;
        });
    });

    while(true) {
        el.next();
    }
}
