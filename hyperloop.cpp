#import <iostream>
#import <netinet/in.h>
#import <signal.h>
#import <sys/event.h>
#import <sys/socket.h>
#import <sys/time.h>
#import <sys/types.h>
#import <sstream>
#import <unistd.h>
#import <vector>
#import <cassert>
#import <algorithm>
#import <stdio.h>

template<class Error, class Success>
using Callback = std::function<void(Error, Success)>;
template<class ...A>
using Thunk = std::function<void(A...)>;

template<class ...A>
const auto nop = [](A...){};

class Circuit {
private:
    Thunk<struct kevent&>* thunk;
    struct kevent event;
public:
    Circuit(Thunk<struct kevent&>* th, struct kevent ev): thunk{th}, event{ev} {}
    
    void run(Thunk<> op = nop<>) {
        thunk->operator()(event);
        op.operator()();
    }
};

class EventToken {

};

struct EventCapture {
    EventCapture(uintptr_t i, u_short ff, short f): ident{i}, filter{f}, flags{ff} {};
    uintptr_t ident;
    short filter;
    u_short flags;
};

// class Capture {
// public:
//     enum class Errors {};

//     Errors apply(struct kevent&) {}
//     Errors unapply(struct kevent&) {}
// };

// class Token {
// friend class HyperLoop;
// Capture capture;
// public:
//     Token(Capture& c): capture {c} {}
// };

// class Applicable {
// private:
//     std::function<void()> callback;
// public:
//     Applicable(const std::function<void()>& c): callback{c} {} 
// };

class HyperLoop {
private:
    int queue;
    bool _abort = false;
    int count = 0;
public:
    HyperLoop(int q): queue{q} {}

    std::shared_ptr<Circuit> next() {
        if (_abort) return nullptr; // TODO: get rid of this
        if (count == 0) return nullptr;

        struct kevent event;

        int n = kevent(queue, NULL, 0, &event, 1, NULL);
        
        assert(n == 1);

        auto callback = static_cast<Thunk<struct kevent&>*>(event.udata);

        return std::shared_ptr<Circuit>(new Circuit(callback, event));
    }

    // watching an event increments the watch counter
    // as long as the counter is non-zero, next() will block to wait for new events
    void watch(EventCapture&& capture, Thunk<struct kevent&> callback) {
        count++;
        
        struct kevent event {
            .ident = capture.ident,
            .filter = capture.filter,
            .flags = capture.flags,
            .udata = new Thunk<struct kevent&>(callback),
        };

        int n = kevent(queue, &event, 1, NULL, 0, NULL);
        
        assert(n == 0);
    }

    // clear implicitly decrements the loop counter
    // if the counter reaches zero, we assume no more events will occur 
    void clear(EventCapture&& capture) {
        count--;

        struct kevent event {
            .ident = capture.ident,
            .filter = capture.filter,
            .flags = capture.flags,
        };

        int n = kevent(queue, &event, 1, NULL, 0, NULL);
        
        assert(n == 0);
    }

    // void watch(Token** token, Capture&& capture, std::function<void()> func) {
    //     struct kevent event {
    //         .udata = new Applicable {func}
    //     };

    //     capture.apply(event);

    //     int n = kevent(queue, &event, 1, NULL, 0, NULL);

    //     assert(n == 0);

    //     *token = new Token {capture};
    // }
};

int main () {

    // smart pointers work well with closures, references not so much...
    std::shared_ptr<HyperLoop> loop(new HyperLoop(kqueue()));
    std::shared_ptr<int> sockfd(new int);

    { // block SIGINT
        struct sigaction sa {
            .sa_handler = SIG_IGN,
        };
        if (sigaction(SIGINT, &sa, NULL) < 0) {
            throw std::runtime_error(strerror(errno));
        }

        // Token *token;
        // loop->watch(&token, Capture(), [](){});

        // add a watcher for SIGINT
        loop->watch(EventCapture(SIGINT, EV_ADD, EVFILT_SIGNAL), [=](struct kevent& event){
            loop->clear(EventCapture(*sockfd, EV_DELETE, EVFILT_READ));
            loop->clear(EventCapture(SIGINT, EV_DELETE, EVFILT_SIGNAL));
            
            close(*sockfd);

            std::cout << "Caught SIGINT" << std::endl;
            std::cout << "Gracefully Stopping Server" << std::endl;

            // free pointer
            delete (Thunk<struct kevent&>*) event.udata;

            return;
        });
    }

    { // listen to socket
        *sockfd = socket(AF_INET, SOCK_STREAM, 0);

        if (sockfd < 0) {
            throw std::runtime_error("Error Opening Socket");
        }

        sockaddr_in in {
            .sin_family = AF_INET,
            .sin_port = htons(8080),
            .sin_addr.s_addr = INADDR_ANY,
        };

        if (bind(*sockfd, (struct sockaddr*) &in, sizeof(in)) == -1) {
            throw std::runtime_error(strerror(errno));
        }

        if (listen(*sockfd, 10) == -1) {
            throw std::runtime_error(strerror(errno));
        }

        loop->watch(EventCapture(*sockfd, EV_ADD, EVFILT_READ), [=](struct kevent& event){
            sockaddr_in client = {};
            socklen_t len;
            auto request = new std::string;
            
            // should not block
            int conn = accept(*sockfd, (struct sockaddr *) &client, &len);
            auto sockPtr = static_cast<Thunk<struct kevent&>*>(event.udata);

            loop->watch(
                EventCapture(conn, EV_ADD, EVFILT_READ),
                [=](struct kevent& event){
                    int N = event.data;
                    char* input = new char[N]; 
                    read(conn, input, N);

                    // lame attempt at buffering
                    *request = *request + input;

                    if (event.flags & EV_EOF || (request->find("\r\n\r\n") > 0)) {
                        loop->clear(EventCapture(conn, EV_DELETE, EVFILT_READ));
                        shutdown(conn, SHUT_RD);

                        // wonderful cleanup
                        // delete (Thunk<struct kevent&>*) event.udata;
                        // delete (Thunk<struct kevent&>*) sockPtr;

                        std::cout << *request << std::endl;;

                        // request is "done"
                        loop->watch(EventCapture(conn, EV_ADD, EVFILT_WRITE), [=](struct kevent& event){
                            int N = event.data;
                            auto response = "HTTP/1.1 200 OK\r\nHost:localhost:8080\r\nContent-Length:12\r\n\r\nHello World!";
                            
                            assert(N > strlen(response) );

                            write(conn, response, strlen(response));
                            
                            loop->clear(EventCapture(conn, EV_DELETE, EVFILT_WRITE));
                            
                            close(conn);
                        });
                        
                        delete[] input;
                    }
                }
            );
        });
    }

    while(std::shared_ptr<Circuit> circuit = loop->next()) {
        // next event
        circuit->run([=](){
            // on complete?
        });
        // end?
    }

    // program done
}
