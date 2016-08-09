#import <iostream>
#import <netinet/in.h>
#import <sys/event.h>
#import <sys/socket.h>
#import <sys/time.h>
#import <sys/types.h>
#import <unistd.h>
#import <vector>
#import <cassert>

template<class T>
using PT = std::shared_ptr<T>;

// forward declarations
class Event;
class EventHandle;

using FN = std::function<void(struct kevent&, EventHandle * eh)>;

class EventLoop {
friend class EventHandle;
private:
    int queue;
public:
    int queueSize = -100;
    EventLoop(int q): queue {q}, queueSize {0} {
        std::cout << "Creating EventLoop" << std::endl;
    }
    
    // delte copy constructor
    EventLoop(const EventLoop& that) = delete;
    EventLoop & operator=(const EventLoop&) = delete;

    void handle(Event event, FN fn);
    void handle(Event event);

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
    EventHandle(FN n, EventLoop& el): func{n}, eloop{el} {}
public:
    FN func;
    void clear() {
        std::cout << eloop.queueSize << std::endl;
        std::cout << "decrement" << std::endl;
        eloop.queueSize -= 1;
        std::cout << eloop.queueSize << std::endl;
        
        delete this;
    }
};

void EventLoop::handle(Event event, FN fn) {
    std::cout << "increment" << std::endl;
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

    std::cout << queueSize << std::endl;

    struct kevent e;
    int n = kevent(queue, NULL, 0, &e, 1, NULL);
    auto el = static_cast<EventHandle *>(e.udata);

    el->func(e, el);

    return (queueSize > 0);
}

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
    
    EventLoop el(kqueue());

    el.handle(Event(sockfd, EV_ADD|EV_ENABLE, EVFILT_READ), [&sockfd, &el](struct kevent& e, EventHandle * listen_handle){
        // this callback runs when accept() is ready to not block 

        sockaddr_in cliaddr = {};
        socklen_t len;
        
        // should not block
        int fd = accept(sockfd, (struct sockaddr*) &cliaddr, &len);

        el.handle(Event(fd, EV_ADD|EV_ENABLE, EVFILT_READ), [sockfd, fd, &el, listen_handle](struct kevent& e, EventHandle * read_handle) {
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
                el.handle(Event(sockfd, EV_DELETE, EVFILT_READ));
                listen_handle->clear();
            }

            // if the socket is closed, the EV_EOF flag will be set
            // we should remove the kqueue listener, and delete the associated closure
            if (e.flags & EV_EOF) {
                std::cout << "socket close" << std::endl;
                el.handle(Event(fd, EV_DELETE, EVFILT_READ));
                read_handle->clear();
            }

            delete[] buff;
        });
    });

    while(bool c = el.next()) {
        //
    }

    std::cout << "End" << std::endl;
}
