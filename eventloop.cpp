#import <iostream>
#import <netinet/in.h>
#import <sys/event.h>
#import <sys/socket.h>
#import <sys/time.h>
#import <sys/types.h>
#import <unistd.h>
#import <vector>

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
        int n = kevent(queue, &e, 1, NULL, 0, NULL);

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

    EventHandle listen_handle = el.handle(Event(sockfd, EV_ADD|EV_ENABLE, EVFILT_READ), [=, &el](struct kevent& e){
        sockaddr_in cliaddr;
        socklen_t len;
        
        int fd = accept(sockfd, (struct sockaddr*) &cliaddr, &len);

        EventHandle read_handle = el.handle(Event(fd, EV_ADD|EV_ENABLE, EVFILT_READ), [=, &el, &read_handle](struct kevent& e){
            const int N = (int) e.data;
            char* buff = new char[N + 1];
            
            buff[N] = '\0';
            
            int n = read(fd, buff, N);

            std::cout << std::string(buff);

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
