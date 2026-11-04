/**********************************************************************************
*        File: WebServer.h
* Description: Embedded HTTP server of the web interface.
*              Serves the static files of the interface (web/ folder) and a small JSON API:
*                GET  /api/state?since=N   clock, cabins, waiting passengers, statistics, events after N
*                GET  /api/config          current settings and daily traffic profile
*                GET  /api/report          final report (text lines)
*                POST /api/control         action=pause|resume|step|speed (ms=..., value=...)
*                POST /api/call            from=F&to=T: a passenger chosen by the user
*                POST /api/wave            kind=morning|lunch|evening|random&count=N
*                POST /api/elevator        id=A&outOfOrder=true|false
*                POST /api/restart         new run with the given settings (same names as config.json)
*                GET  /api/runs            archive of the completed runs (settings, scenario, results)
*                POST /api/runs/clear      empties the archive
*                POST /api/batch           comparison: scenario settings plus strategies=..., parkings=..., elevators=...
*                POST /api/batch/cancel    stops the comparison after the current variant
*      Author: Emanuele Merlo (emanuele.merlo@gmail.com)
*       Notes: Based on cpp-httplib (third_party/cpp-httplib, MIT license).
**********************************************************************************/

#pragma once

#include <memory>
#include <string>

class Simulation;

class WebServer final
{
public:
  WebServer(Simulation& simulation, std::string webRoot);
  ~WebServer();

  WebServer(const WebServer&) = delete;
  WebServer& operator=(const WebServer&) = delete;
  WebServer(WebServer&&) = delete;
  WebServer& operator=(WebServer&&) = delete;

public:
  /** \brief Starts listening in a background thread. Returns false if the address cannot be bound. */
  bool Start(const std::string& host, int port);
  void Stop();

private:
  struct Implementation;
  std::unique_ptr<Implementation> m_implementation;
};
