#ifndef ROUTINGPROTOCOLIMPL_H
#define ROUTINGPROTOCOLIMPL_H

#include "RoutingProtocol.h"
#include "Node.h"
#include <unordered_map>

struct RoutingEntry {
    unsigned short destination;        // Destination router ID
    unsigned short cost;               // Cost to reach this destination
    unsigned short next_hop;           // Next hop router ID
    unsigned int last_update_time;     // The last time this entry was updated
};

struct LSA {
    unsigned short source_id; // Originating router ID
    unsigned long seq_num;    // Sequence number
    unsigned int last_update_time; // The last time this entry was updated
    std::unordered_map<unsigned short, unsigned short> neighbor_rtt; // Neighbor (ID, cost)
};

class RoutingProtocolImpl : public RoutingProtocol {
  public:
    RoutingProtocolImpl(Node *n);
    ~RoutingProtocolImpl();

    void init(unsigned short num_ports, unsigned short router_id, eProtocolType protocol_type);
    // Initializes the routing protocol with the number of ports, router ID, and protocol type.

    void handle_alarm(void *data);
    // Handles alarms for periodic tasks like sending updates and neighbor timeouts.

    void recv(unsigned short port, void *packet, unsigned short size);
    // Receives packets and processes them based on their type.

  private:
    Node *sys;                        // Pointer to the Node object for system interactions
    unsigned short router_id;         // ID of this router
    unsigned short num_ports;         // Number of ports on this router
    eProtocolType protocol_type;      // Protocol type (P_DV or P_LS)

    // Reordered variables to match initialization order and prevent memory overwrite issues
    unsigned int dv_update_interval;   // Interval for periodic DV updates (milliseconds)
    unsigned int ls_update_interval;   // Interval for periodic LS updates (milliseconds)
    unsigned int neighbor_timeout;     // Timeout to consider a neighbor dead (milliseconds)
    unsigned int last_dv_update_time;  // Last time a DV update was sent

    // DV Protocol Data Structures
    std::unordered_map<unsigned short, RoutingEntry> dv_table;    // DV routing table
    std::unordered_map<unsigned short, unsigned int> neighbors;   // Neighbors with last heard time
    std::unordered_map<unsigned short, int> neighbor_ports;       // Mapping from neighbor ID to port number

    // DV Protocol Methods
    void sendDvUpdate();
    void sendPing();
    void handleNeighborTimeout();
    void processPing(unsigned short port, void *packet, unsigned short size);
    void processPong(unsigned short port, void *packet, unsigned short size);
    void processDV(unsigned short port, void *packet, unsigned short size);
    void passPacket(void *packet, unsigned short size);

    // LS Protocol Data Structures
    std::unordered_map<unsigned short, LSA> lsdb;                    // Link State Database
    LSA ls_entry;                                                    // Self LSA entry
    std::unordered_map<unsigned short, unsigned short> routing_table; // Routing table for LS protocol

    // LS Protocol Methods
    void floodLSA();
    void passLSA(unsigned short port, void *packet, unsigned short size);
    void processLSA(unsigned short port, void *packet, unsigned short size);
    void runDijkstra();

    // Helper Methods
    void printTheTable();
    void printThePorts();
    void printTimeout();
    void printPacket(void *packet, unsigned short size);
    void printLSDB();
    void printRoutingTable();
};

#endif // ROUTINGPROTOCOLIMPL_H
