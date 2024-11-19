#ifndef ROUTINGPROTOCOLIMPL_H
#define ROUTINGPROTOCOLIMPL_H

#include "RoutingProtocol.h"
#include "Node.h"
#include <unordered_map>
using namespace std;

struct RoutingEntry {
    unsigned short destination;        // Destination router ID
    unsigned short cost;               // Cost to reach this destination
    unsigned short next_hop;           // Next hop router ID
    unsigned int last_update_time;     // The last time this entry was updated
};

class RoutingProtocolImpl : public RoutingProtocol {
  public:
    RoutingProtocolImpl(Node *n);
    ~RoutingProtocolImpl();

    void init(unsigned short num_ports, unsigned short router_id, eProtocolType protocol_type);
    // As discussed in the assignment document, your RoutingProtocolImpl is
    // first initialized with the total number of ports on the router,
    // the router's ID, and the protocol type (P_DV or P_LS) that
    // should be used. See global.h for definitions of constants P_DV
    // and P_LS.

    void handle_alarm(void *data);
    // As discussed in the assignment document, when an alarm scheduled by your
    // RoutingProtoclImpl fires, your RoutingProtocolImpl's
    // handle_alarm() function will be called, with the original piece
    // of "data" memory supplied to set_alarm() provided. After you
    // handle an alarm, the memory pointed to by "data" is under your
    // ownership and you should free it if appropriate.

    void recv(unsigned short port, void *packet, unsigned short size);
    // When a packet is received, your recv() function will be called
    // with the port number on which the packet arrives from, the
    // pointer to the packet memory, and the size of the packet in
    // bytes. When you receive a packet, the packet memory is under
    // your ownership and you should free it if appropriate. When a
    // DATA packet is created at a router by the simulator, your
    // recv() function will be called for such DATA packet, but with a
    // special port number of SPECIAL_PORT (see global.h) to indicate
    // that the packet is generated locally and not received from 
    // a neighbor router.

 private:
    Node *sys; // To store Node object; used to access GSR9999 interfaces 
    unsigned short router_id;
    unsigned short num_ports;
    eProtocolType protocol_type;
    unsigned int dv_update_interval;   // Interval for periodic DV updates (ms)
    unsigned int neighbor_timeout;     // Timeout to consider a neighbor dead (ms)
    unsigned int last_dv_update_time;  // Last time a DV update was sent

    unordered_map<unsigned short, RoutingEntry> dv_table; // Key: dest., val: dest, cost, next hop, and last_update_time
    unordered_map<unsigned short, unsigned int> neighbors;// Key: router id, val: timestamp
    unordered_map<unsigned short, int> neighbor_ports; // Key: router id, val: port.


    void sendDvUpdate();
    void sendPing();
    void handleNeighborTimeout();
    // void cleanExpiredEntry();
    void processPing(unsigned short port, void *packet, unsigned short size);
    void processPong(unsigned short port, void *packet, unsigned short size);
    void processDV(unsigned short port, void *packet, unsigned short size);
    void processLS(unsigned short port, void *packet, unsigned short size);

    void passPacket(void *packet, unsigned short size);

    //helper
    void printTheTable();
    void printThePorts();
    void printTimeout();
    void printPacket(void *packet, unsigned short size);
};

#endif

