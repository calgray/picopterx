/**
 * @file picopter.cpp
 * @brief The main entry point to the server.
 */

#include "picopter.h"
#include "webInterface.h"
#include <thrift/concurrency/ThreadManager.h>
#include <thrift/concurrency/PosixThreadFactory.h>
#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/server/TThreadPoolServer.h>
#include <thrift/transport/TServerSocket.h>
#include <thrift/transport/TBufferTransports.h>
#include <sstream>
#include <csignal>

using namespace ::apache::thrift;
using namespace ::apache::thrift::protocol;
using namespace ::apache::thrift::transport;
using namespace ::apache::thrift::server;
using namespace ::apache::thrift::concurrency;

using boost::shared_ptr;
using std::this_thread::sleep_for;
using std::chrono::milliseconds;

using namespace  ::picopter;

std::unique_ptr<TThreadPoolServer> g_server(nullptr);
std::unique_ptr<picopter::FlightController> g_fc(nullptr);

class webInterfaceHandler : virtual public webInterfaceIf
{
private:
    const std::unique_ptr<picopter::FlightController> &m_fc;
    std::deque<navigation::Coord2D> m_pts;
public:
    webInterfaceHandler(std::unique_ptr<picopter::FlightController> &fc)
    : m_fc(fc)
    {
        // Your initialization goes here
    }

    bool beginWaypointsThread()
    {
        if (m_fc->GetCurrentTaskId() != TASK_NONE) {
            // ALREADY RUNNING
            return false;
        } else {
            Waypoints *wpts = new Waypoints(m_pts, WAYPOINT_SIMPLE);
            
            if (!m_fc->RunTask(TASK_WAYPOINTS, wpts, NULL)) {
                delete wpts;
                return false;
            }
        }
        return true;
    }

    bool beginLawnmowerThread()
    {
        if (m_fc->GetCurrentTaskId() != TASK_NONE) {
            // ALREADY RUNNING
            return false;
        }
        // Your implementation goes here
        //printf("beginLawnmowerThread\n");
        return false;
    }

    bool beginUserTrackingThread()
    {
        if (m_fc->GetCurrentTaskId() != TASK_NONE) {
            // ALREADY RUNNING
            return false;
        }
        // Your implementation goes here
        //printf("beginUserTrackingThread\n");
        return false;
    }
    
    bool beginObjectTrackingThread()
    {
        if (m_fc->GetCurrentTaskId() != TASK_NONE) {
            // ALREADY RUNNING
            return false;
        } else {
            ObjectTracker *trk = new ObjectTracker();
            
            if (!m_fc->RunTask(TASK_OBJECT_TRACKING, trk, NULL)) {
                delete trk;
                return false;
            }
        }
        return true;
    }

    bool allStop()
    {
        m_fc->Stop();
        return true;
    }

    void requestStatus(std::string& _return)
    {
        std::stringstream ss;
        ss << (*m_fc);
        _return = ss.str();
    }

    void requestCoords(coordDeg& _return)
    {
        GPSData d;
        m_fc->gps->GetLatest(&d);
        _return.lat = d.fix.lat;
        _return.lon = d.fix.lon;
        printf("requestCoords %f,%f\n", _return.lat, _return.lon);
    }

    double requestBearing()
    {
        GPSData d;
        m_fc->gps->GetLatest(&d);
        if (!std::isnan(d.fix.bearing)) {
            return d.fix.bearing;
        }
        return 0;
    }

    void requestNextWaypoint(coordDeg& _return)
    {
        // Your implementation goes here
        //printf("requestNextWaypoint\n");
    }

    bool updateUserPosition(const coordDeg& wpt)
    {
        // Your implementation goes here
        //printf("updateUserPosition\n");
        return false;
    }

    bool updateWaypoints(const std::vector<coordDeg> & wpts)
    {
        Log(LOG_INFO, "Updating waypoints");
        
        int i = 1;
        m_pts.clear();
        for (coordDeg v : wpts) {
            navigation::Coord2D wpt;
            Log(LOG_INFO, "%d: (%.6f,%.6f)", i++, v.lat, v.lon);
            wpt.lat = v.lat;
            wpt.lon = v.lon;
            m_pts.push_back(wpt);
        }
        //printf("updateWaypoints\n");
        return true;
    }

    bool resetWaypoints()
    {
        // Your implementation goes here
        //printf("resetWaypoints\n");
        return false;
    }
};

/**
 * SIGINT/SIGTERM interrupt handler
 */
void terminate(int signum) {
    static int killcount = 0;
    
    if (++killcount > 3) {
        Fatal("Okay you asked for it; exiting immediately!");
    } else {
        Log(LOG_WARNING, "Terminate signal received! Attempting termination...");
        
        if (!g_fc) {
            Fatal("Flight controller was not initialised; exiting immediately!");
        } else if (!g_server) {
            Fatal("Server was not initialised; exiting immediately!");
        }

        g_server->stop();
        sleep_for(milliseconds(400));
        if (g_fc) {
            g_fc->Stop();
        }
    }
}

int main(int argc, char **argv)
{
    LogInit();
    
    //Signal handlers
    struct sigaction signal_handler;	
	signal_handler.sa_handler = terminate;
	sigemptyset(&signal_handler.sa_mask);
	signal_handler.sa_flags = 0;
	
	sigaction(SIGTERM, &signal_handler, NULL);
	sigaction(SIGINT,  &signal_handler, NULL);
    
    int port = 9090;
    
    try {
        g_fc.reset(new picopter::FlightController());
    } catch (const std::invalid_argument &e) {
        Fatal("Failed to initialise %s which is required, exiting.", e.what());
    }
    
    shared_ptr<webInterfaceHandler> handler(new webInterfaceHandler(g_fc));
    shared_ptr<TProcessor> processor(new webInterfaceProcessor(handler));
    shared_ptr<TServerTransport> serverTransport(new TServerSocket(port));
    shared_ptr<TTransportFactory> transportFactory(new TBufferedTransportFactory());
    shared_ptr<TProtocolFactory> protocolFactory(new TBinaryProtocolFactory());

    shared_ptr<ThreadManager> threadManager(ThreadManager::newSimpleThreadManager(1));
	shared_ptr<PosixThreadFactory> threadFactory(new PosixThreadFactory());
	threadManager->threadFactory(threadFactory);
	threadManager->start();
    
    g_server.reset(new TThreadPoolServer(processor,serverTransport,transportFactory,protocolFactory,threadManager));
    try {
        Log(LOG_INFO, "Server started.");
        g_server->serve();
    } catch (const TTransportException &e) {
        Fatal("Cannot start server: Thrift port 9090 is already in use.");
    }
    
    Log(LOG_INFO, "Server stopped.");
    return 0;
}

