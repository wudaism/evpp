#include <iostream>

#include <stdio.h>
#include <stdlib.h>

#include <evpp/timestamp.h>
#include <evpp/event_loop_thread.h>
#include <evpp/event_loop.h>
#include <evpp/dns_resolver.h>

#include <evpp/httpc/request.h>
#include <evpp/httpc/conn.h>
#include <evpp/httpc/response.h>

#include <evpp/http/service.h>
#include <evpp/http/context.h>
#include <evpp/http/http_server.h>
#include "../../examples/winmain-inl.h"

static bool g_stopping = false;
static void RequestHandler(evpp::EventLoop* loop, const evpp::http::ContextPtr& ctx, const evpp::http::HTTPSendResponseCallback& cb) {
    std::stringstream oss;
    oss << "func=" << __FUNCTION__ << " OK"
        << " ip=" << ctx->remote_ip() << "\n"
        << " uri=" << ctx->uri() << "\n"
        << " body=" << ctx->body().ToString() << "\n";
    cb(oss.str());
}

static void DefaultRequestHandler(evpp::EventLoop* loop, const evpp::http::ContextPtr& ctx, const evpp::http::HTTPSendResponseCallback& cb) {
    //std::cout << __func__ << " called ...\n";
    std::stringstream oss;
    oss << "func=" << __FUNCTION__ << "\n"
        << " ip=" << ctx->remote_ip() << "\n"
        << " uri=" << ctx->uri() << "\n"
        << " body=" << ctx->body().ToString() << "\n";

    if (ctx->uri().find("stop") != std::string::npos) {
        g_stopping = true;
    }

    cb(oss.str());
}

namespace {
    static std::vector<int> g_listening_port;

    static std::string GetHttpServerURL() {
        assert(g_listening_port.size() > 0);
        static int i = 0;
        std::ostringstream oss;
        oss << "http://127.0.0.1:" << g_listening_port[(i++ % g_listening_port.size())];
        return oss.str();
    }

    void testDefaultHandler1(evpp::EventLoop* loop, int* finished) {
        std::string uri = "/status?a=1";
        std::string url = GetHttpServerURL() + uri;
        auto r = new evpp::httpc::Request(loop, url, "", evpp::Duration(1.0));
        auto f = [r, finished](const std::shared_ptr<evpp::httpc::Response>& response) {
            std::string result = response->body().ToString();
            assert(!result.empty());
            assert(result.find("uri=/status") != std::string::npos);
            assert(result.find("uri=/status?a=1") == std::string::npos);
            assert(result.find("func=DefaultRequestHandler") != std::string::npos);
            *finished += 1;
            delete r;
        };

        r->Execute(f);
    }

    void testDefaultHandler2(evpp::EventLoop* loop, int* finished) {
        std::string uri = "/status";
        std::string body = "The http request body.";
        std::string url = GetHttpServerURL() + uri;
        auto r = new evpp::httpc::Request(loop, url, body, evpp::Duration(1.0));
        auto f = [body, r, finished](const std::shared_ptr<evpp::httpc::Response>& response) {
            std::string result = response->body().ToString();
            assert(!result.empty());
            assert(result.find("uri=/status") != std::string::npos);
            assert(result.find("func=DefaultRequestHandler") != std::string::npos);
            assert(result.find(body.c_str()) != std::string::npos);
            *finished += 1;
            delete r;
        };

        r->Execute(f);
    }

    void testDefaultHandler3(evpp::EventLoop* loop, int* finished) {
        std::string uri = "/status/method/method2/xx";
        std::string url = GetHttpServerURL() + uri;
        auto r = new evpp::httpc::Request(loop, url, "", evpp::Duration(1.0));
        auto f = [r, finished](const std::shared_ptr<evpp::httpc::Response>& response) {
            std::string result = response->body().ToString();
            assert(!result.empty());
            assert(result.find("uri=/status/method/method2/xx") != std::string::npos);
            assert(result.find("func=DefaultRequestHandler") != std::string::npos);
            *finished += 1;
            delete r;
        };

        r->Execute(f);
    }

    void testPushBootHandler(evpp::EventLoop* loop, int* finished) {
        std::string uri = "/push/boot";
        std::string url = GetHttpServerURL() + uri;
        auto r = new evpp::httpc::Request(loop, url, "", evpp::Duration(1.0));
        auto f = [r, finished](const std::shared_ptr<evpp::httpc::Response>& response) {
            std::string result = response->body().ToString();
            assert(!result.empty());
            assert(result.find("uri=/push/boot") != std::string::npos);
            assert(result.find("func=RequestHandler") != std::string::npos);
            *finished += 1;
            delete r;
        };

        r->Execute(f);
    }

    void testStop(evpp::EventLoop* loop, int* finished) {
        std::string uri = "/mod/stop";
        std::string url = GetHttpServerURL() + uri;
        auto r = new evpp::httpc::Request(loop, url, "", evpp::Duration(1.0));
        auto f = [r, finished](const std::shared_ptr<evpp::httpc::Response>& response) {
            std::string result = response->body().ToString();
            assert(!result.empty());
            assert(result.find("uri=/mod/stop") != std::string::npos);
            assert(result.find("func=DefaultRequestHandler") != std::string::npos);
            *finished += 1;
            delete r;
        };

        r->Execute(f);
    }

    static void TestAll() {
        evpp::EventLoopThread t;
        t.Start(true);
        int finished = 0;
        testDefaultHandler1(t.event_loop(), &finished);
        testDefaultHandler2(t.event_loop(), &finished);
        testDefaultHandler3(t.event_loop(), &finished);
        testPushBootHandler(t.event_loop(), &finished);
        testStop(t.event_loop(), &finished);

        while (true) {
            usleep(10);

            if (finished == 5) {
                break;
            }
        }

        t.Stop(true);
    }
}

void TestHTTPServer() {
    for (int i = 0; i < 40; ++i) {
        evpp::http::Server ph(i);
        ph.RegisterDefaultHandler(&DefaultRequestHandler);
        ph.RegisterHandler("/push/boot", &RequestHandler);
        bool r = ph.Init(g_listening_port) && ph.Start();
        assert(r);
        TestAll();
        ph.Stop(true);
    }
}

void TestDNSResolver() {
    for (int i = 0; i < 40; i++) {
        bool resolved = false;
        bool deleted = false;
        auto fn_resolved = [&resolved](const std::vector <struct in_addr>& addrs) {
            LOG_INFO << "Entering fn_resolved";
            resolved = true;
        };

        evpp::Duration delay(double(3.0)); // 3s
        std::unique_ptr<evpp::EventLoopThread> t(new evpp::EventLoopThread);
        t->Start(true);
        std::shared_ptr<evpp::DNSResolver> dns_resolver(new evpp::DNSResolver(t->event_loop(), "www.so.com", evpp::Duration(1.0), fn_resolved));
        dns_resolver->Start();

        while (!resolved) {
            usleep(1);
        }

        auto fn_deleter = [&deleted, dns_resolver]() {
            LOG_INFO << "Entering fn_deleter";
            deleted = true;
        };

        t->event_loop()->QueueInLoop(fn_deleter);
        dns_resolver.reset();
        while (!deleted) {
            usleep(1);
        }

        t->Stop(true);
        t.reset();
        if (evpp::GetActiveEventCount() != 0) {
            assert(evpp::GetActiveEventCount() == 0);
        }
    }
}

int main(int argc, char* argv[]) {
    int port = 8080;
    if (argc > 1) {
        if (std::string("-h") == argv[1] ||
                std::string("--h") == argv[1] ||
                std::string("-help") == argv[1] ||
                std::string("--help") == argv[1]) {
            std::cout << "usage : " << argv[0] << " <listen_port1> <listen_port2> ...\n";
            std::cout << " e.g. : " << argv[0] << " 8080\n";
            std::cout << " e.g. : " << argv[0] << " 8080 8081\n";
            std::cout << " e.g. : " << argv[0] << " 8080 8081 8082\n";
            return 0;
        }
    }

    if (argc == 1) {
        g_listening_port.push_back (port);
    } else {
        for (int i = 1; i < argc; i++) {
            port = std::atoi(argv[1]);
            g_listening_port.push_back(port);
        }
    }

    // We are running forever
    // If the program stops at somewhere there must be a bug to be fixed.
    for (;;) {
        TestHTTPServer();
        TestDNSResolver();
    }
    return 0;
}
