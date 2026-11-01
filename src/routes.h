#pragma once
// The web server's routes as a table in flash, answered by one handler (spec 025).
// Each server.on() puts a handler object, a Uri and a std::function on the heap, about
// 80 B a route: 60 routes held 4.7 KB. A route here is 36 B of flash and no RAM.
//
// Matches as server.on() does: the exact path, and the method unless it is HTTP_ANY. A
// request no route takes falls through to onNotFound(), so a wrong method is a 404, as
// before. The table is PROGMEM: the path is read with strcmp_P, and the other fields are
// 32-bit words at 32-bit offsets, which flash can be read as directly.
#include <ESP8266WebServer.h>
#include <pgmspace.h>

bool hostAllowed();   // main.cpp: the request is addressed to this clock (spec 029)

struct Route {
  char path[24];       // a longer path does not compile
  HTTPMethod method;   // HTTP_ANY: any
  void (*handle)();
  void (*upload)();    // nullptr: no upload
};

class RouteTable : public esp8266webserver::RequestHandler<WiFiServer> {
 public:
  RouteTable(const Route* routes, size_t count) : routes_(routes), count_(count) {}

  bool canHandle(HTTPMethod method, const String& uri) override { return find(method, uri); }
  bool canUpload(const String& uri) override {
    const Route* r = find(HTTP_POST, uri);
    return r && r->upload;
  }
  bool handle(ESP8266WebServer& server, HTTPMethod method, const String& uri) override {
    const Route* r = find(method, uri);
    if (!r) return false;
    if (!hostAllowed()) { server.send(403, PSTR("text/plain"), PSTR("Unknown host name")); return true; }
    r->handle();
    return true;
  }
  void upload(ESP8266WebServer&, const String& uri, HTTPUpload&) override {
    const Route* r = find(HTTP_POST, uri);
    if (r && r->upload && hostAllowed()) r->upload();   // refused: the body is dropped, handle() answers 403
  }

 private:
  const Route* find(HTTPMethod method, const String& uri) const {
    for (size_t i = 0; i < count_; ++i) {
      const Route& r = routes_[i];
      if ((r.method == HTTP_ANY || r.method == method) && strcmp_P(uri.c_str(), r.path) == 0) return &r;
    }
    return nullptr;
  }
  const Route* routes_;
  size_t count_;
};
