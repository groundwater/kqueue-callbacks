#import <iostream>
#import <netinet/in.h>
#import <sys/event.h>
#import <sys/socket.h>
#import <sys/time.h>
#import <sys/types.h>
#import <unistd.h>
#import <vector>
#import <cassert>

// forward declarations
class Event;
class EventHandle;
class EventLoop;

// type aliases
using FN = std::function<void(struct kevent&, EventHandle *)>;

// headers
class EventLoop {
friend class EventHandle;
private:
    int queue;
public:
    int queueSize;
    EventLoop(int);

    void handle(Event, FN);
    void handle(Event);

    bool next();
};

struct Event {
    Event(uintptr_t i, u_short ff, short f): ident{i}, filter{f}, flags{ff} {};
    uintptr_t ident;
    short filter;
    u_short flags;
};

class EventHandle {
friend class EventLoop;
private:
    EventLoop &eloop;
    EventHandle(FN, EventLoop&);
public:
    FN func;
    void clear();
};

// implementations

EventHandle::EventHandle(FN n, EventLoop &el): func{n}, eloop{el} {}

void EventHandle::clear()  {
    eloop.queueSize -= 1;
    delete this;
}

EventLoop::EventLoop(int q): queue {q}, queueSize {0} {}

void EventLoop::handle(Event event, FN fn) {
    queueSize++;
    
    // The EventHandle is the way we can clean up after ourselves later.
    // Be sure to call .clear() after the lambda finishes executing.
    // Remember we support a lambda running 0-N times. 
    EventHandle * eh(new EventHandle(fn, *this)); // valgrind says this is leaking

    struct kevent e {
        .ident = event.ident,
        .filter = event.filter,
        .flags = event.flags,
        .udata = eh,
    };

    // if you only want to set a new listener, set arg 3/4 to (NULL, 0) respectively
    // n should always be 0
    int n = kevent(queue, &e, 1, NULL, 0, NULL);
    assert(n == 0);
}

void EventLoop::handle(Event event) {
    struct kevent e {
        .ident = event.ident,
        .filter = event.filter,
        .flags = event.flags,
    };
    int n = kevent(queue, &e, 1, NULL, 0, NULL);
}

bool EventLoop::next() {
    assert(queueSize >= 0);

    struct kevent e;
    int n = kevent(queue, NULL, 0, &e, 1, NULL);
    auto el = static_cast<EventHandle *>(e.udata);

    el->func(e, el);

    return (queueSize > 0);
}

// runtime

int main () {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);

    if (sockfd < 0) {
        throw std::runtime_error("Error Opening Socket");
    }

    sockaddr_in in_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(8080),
        .sin_addr.s_addr = INADDR_ANY,
    };
    
    if (bind(sockfd, (struct sockaddr*) &in_addr, sizeof(in_addr)) == -1) {
        throw std::runtime_error("Socket Binding Failed");
    }

    if (listen(sockfd, 10) == -1) {
        throw std::runtime_error("Socket Listen Failed");
    }
    
    EventLoop event_loop(kqueue());

    event_loop.handle(Event(sockfd, EV_ADD|EV_ENABLE, EVFILT_READ), [&sockfd, &event_loop](struct kevent& e, EventHandle * listen_handle){
        // this callback runs when accept() is ready to not block 

        sockaddr_in cliaddr = {};
        socklen_t len;
        
        // should not block
        int fd = accept(sockfd, (struct sockaddr*) &cliaddr, &len);

        event_loop.handle(Event(fd, EV_ADD|EV_ENABLE, EVFILT_READ), [sockfd, fd, &event_loop, listen_handle](struct kevent& e, EventHandle * read_handle) {
            // this callback runs when read() is ready to not block

            // the .data property holds the number of bytes to be read 
            const int N = (int) e.data;

            char* buff = new char[N + 1];
            buff[N] = '\0';
            
            // read only the bytes ready to be read
            // assert( n == N )
            int n = read(fd, buff, N);
            assert(n == N);

            auto s = std::string(buff);
            std::cout << s;

            if (s.find("quit") == 0) {
                std::cout << "quitting" << std::endl;
                event_loop.handle(Event(sockfd, EV_DELETE, EVFILT_READ));
                listen_handle->clear();
            }

            // if the socket is closed, the EV_EOF flag will be set
            // we should remove the kqueue listener, and delete the associated closure
            if (e.flags & EV_EOF) {
                std::cout << "socket close" << std::endl;
                event_loop.handle(Event(fd, EV_DELETE, EVFILT_READ));
                read_handle->clear();
            }

            delete[] buff;
        });
    });

    while(bool c = event_loop.next()) {
        //
    }

    std::cout << "End" << std::endl;
}
