#include "RoutingProtocolImpl.h"

RoutingProtocolImpl::RoutingProtocolImpl(Node *n) : RoutingProtocol(n) {
  sys = n;
  // add your own code
  // Initialize default intervals
  dv_update_interval = 30000;  // 30 seconds in milliseconds
  neighbor_timeout = 15000;    // 15 seconds for neighbor timeouts in milliseconds
  last_dv_update_time = 0;     // Track last DV update time
}

RoutingProtocolImpl::~RoutingProtocolImpl() {
  // add your own code (if needed)
}

void RoutingProtocolImpl::init(unsigned short num_ports, unsigned short router_id, eProtocolType protocol_type) {
  this->num_ports = num_ports;
  this->router_id = router_id;
  this->protocol_type = protocol_type;
  
  if(protocol_type == P_DV){
    RoutingEntry self_entry;
    self_entry.destination = router_id;
    self_entry.cost = 0;
    self_entry.next_hop = router_id;
    self_entry.last_update_time = sys->time();
    // Add self entry to DV table
    dv_table[router_id] = self_entry;

    // Set periodic DV update alarm
    sys->set_alarm(this, dv_update_interval, nullptr);

    // Set alarm to send ping messages for neighbor detection every 10 seconds
    sys->set_alarm(this, 10000, (void*)1);

    // Set alarm to check for neighbor timeouts and routing table entry expiration every 1 second
    sys->set_alarm(this, 1000, (void*)2);

  } else if (protocol_type == P_LS){
    // Add your own code
  }
}

void RoutingProtocolImpl::handle_alarm(void *data) {
  if(protocol_type == P_DV){
    if(data == nullptr){
      sendDvUpdate();
      sys->set_alarm(this, dv_update_interval, nullptr);
    }else{
      int alarm_type = (int)(uintptr_t)data;
      // perdoicly send ping
      if(alarm_type == 1){
        sendPing();
        sys->set_alarm(this, 10000, (void*)1);  // send PING in 10 seconds
      }
      // clean up expired routing entries
      if(alarm_type == 2){
        handleNeighborTimeout();
        // cleanExpiredEntry();
        sys->set_alarm(this, 1000, (void*)2); // Reschedule in 1 seconds
      }
    }
  }else{
    // Add your own code
    // LS
  }
}

void RoutingProtocolImpl::recv(unsigned short port, void *packet, unsigned short size) {
  // Packet from the router itself
  if(port == SPECIAL_PORT){
    free(packet);
    return;
  }

  // Packet from other routers
  ePacketType packet_type = (ePacketType)ntohs(*(unsigned short*)packet);
  switch (packet_type){
    case DATA:
      // transmit packet?
      
      break;
    case PING:
      processPing(port, packet, size);
      break;
    case PONG:
      processPong(port, packet, size);
      break;
    case DV:
      processDV(port, packet, size);
      break;
    case LS:
      processLS(port, packet, size);
      break;
    default: 
      free(packet);
      break;
  }

}


// add more of your own code
// update routing table to neighbors
void RoutingProtocolImpl::sendDvUpdate() {
    for (auto& neighbor: neighbor_ports) {
        unsigned short neighbor_port = neighbor_ports[neighbor.first];
        vector<pair<unsigned short, unsigned short>> dv_entries;

        // Exclude the router itself and unreachable routers
        for (auto &row : dv_table) {
            if (row.second.destination != router_id && row.second.cost != INFINITY_COST) {
                dv_entries.push_back({row.second.destination, row.second.cost});
            }
        }

        // Packet format: type(1 byte) + reserved(1 byte) + size(2 bytes) + source(2 bytes) + destination(2 bytes) + entries
        size_t dv_packet_size = 8 + dv_entries.size() * 4; // 8 bytes header + 4 bytes per entry
        void *packet = malloc(dv_packet_size);
        unsigned char *packet_data = (unsigned char*)packet;

        // Packet Type (1 byte)
        packet_data[0] = DV; // DV should be an unsigned char value

        // Reserved (1 byte)
        packet_data[1] = 0; // Reserved byte

        // Number of entries (2 bytes)
        unsigned short num_entries = (unsigned short)dv_entries.size();
        packet_data[2] = (num_entries >> 8) & 0xFF; // High byte
        packet_data[3] = num_entries & 0xFF;        // Low byte

        // Source ID (2 bytes)
        packet_data[4] = (router_id >> 8) & 0xFF;   // High byte
        packet_data[5] = router_id & 0xFF;          // Low byte

        // Destination ID (2 bytes)
        packet_data[6] = (neighbor.first >> 8) & 0xFF;    // High byte
        packet_data[7] = neighbor.first & 0xFF;           // Low byte

        // Payload (Routing Entries)
        size_t offset = 8;
        for (size_t i = 0; i < dv_entries.size(); i++) {
            unsigned short node_id = dv_entries[i].first;
            unsigned short cost = dv_entries[i].second;

            // Node ID (2 bytes)
            packet_data[offset] = (node_id >> 8) & 0xFF;   // High byte
            packet_data[offset + 1] = node_id & 0xFF;      // Low byte

            // Cost (2 bytes)
            packet_data[offset + 2] = (cost >> 8) & 0xFF;  // High byte
            packet_data[offset + 3] = cost & 0xFF;         // Low byte

            offset += 4; // Move to the next entry
        }

        // Send the DV update packet
        sys->send(neighbor_port, packet, (unsigned short)dv_packet_size);
    }
    last_dv_update_time = sys->time();
}

// detect existance of neighbor
void RoutingProtocolImpl::sendPing(){
  for(unsigned short port = 0; port < num_ports; port++){
    // Packet format: type(1 byte) + reserved(1 byte) + size(2 bytes) + source(2 bytes) + destination(2 bytes unused) + payload(4 bytes)
    size_t ping_packet_size = 12;
    void *packet = malloc(ping_packet_size);
    unsigned char *packet_data = (unsigned char*)packet;
    // Packet Type (1 byte)
    packet_data[0] = PING;
    // Reserved (1 byte)
    packet_data[1] = 0; // Reserved byte
    // Number of entries (2 bytes)
    packet_data[2] = 0;
    packet_data[3] = 0;
    // Source ID (2 bytes)
    packet_data[4] = (router_id >> 8) & 0xFF;   // High byte
    packet_data[5] = router_id & 0xFF;          // Low byte
    // Destination ID (2 bytes)
    packet_data[6] = 0;
    packet_data[7] = 0;
    // time (4 bytes)
    unsigned int curTime = sys->time();
    packet_data[8] = (curTime >> 24) & 0xFF;
    packet_data[9] = (curTime >> 24) & 0xFF;
    packet_data[10] = (curTime >> 24) & 0xFF;
    packet_data[11] = curTime & 0xFF;

    sys->send(port, packet, (unsigned short)ping_packet_size);
  }
}

// received PING and send PONG back
void RoutingProtocolImpl::processPing(unsigned short port, void *packet, unsigned short size){
  if (size < 12) {
      // Packet too small, discard
      cout<<"Packet too small, discard"<<endl;
      free(packet);
      return;
  }

  // decode the packet
  unsigned char *packet_data = (unsigned char*)packet;
  packet_data[0] = PONG;

  // copy source to dest.
  packet_data[6] = packet_data[4];
  packet_data[7] = packet_data[5];
  // set the source
  packet_data[4] = (router_id >> 8) & 0xFF;   // High byte
  packet_data[5] = router_id & 0xFF;          // Low byte
  // ts remains unchanged
  sys->send(port, packet, size);
}

// recv PONG and update the neighbor_ports map
void RoutingProtocolImpl::processPong(unsigned short port, void *packet, unsigned short size) {
  if (size < 12) {
      // Packet too small, discard
      free(packet);
      return;
  }

  unsigned char *packet_data = (unsigned char*)packet;

  // Extract neighbor's ID from the PONG packet (source ID)
  unsigned short neighbor_id = ((unsigned short)packet_data[4] << 8) | packet_data[5];

  // Extract the timestamp from the payload
  unsigned int sent_time = ((unsigned int)packet_data[8] << 24) |
                            ((unsigned int)packet_data[9] << 16) |
                            ((unsigned int)packet_data[10] << 8) |
                            ((unsigned int)packet_data[11]);

  // Compute RTT
  unsigned int current_time = sys->time();
  unsigned int rtt = current_time - sent_time;

  // Update neighbor's last heard time
  neighbors[neighbor_id] = current_time;

  // Update neighbor-to-port mapping
  neighbor_ports[neighbor_id] = port;

  // Free the packet memory
  free(packet);
}

// clean dead neighbor
void RoutingProtocolImpl::handleNeighborTimeout(){
  // id, timestamp
  for(auto& neighbor: neighbors){
    unsigned int curTime = sys->time();
    if(curTime - neighbor.second > neighbor_timeout){
      // clean the dv_table
      for(auto& row: dv_table){
        if(row.second.next_hop == neighbor.first){
          row.second.cost = INFINITY_COST;
          row.second.last_update_time = curTime;
        }
      }
      // clean the neighbor timeout table
      neighbors.erase(neighbor.first);
    }
  }
}

// update the dv table
void RoutingProtocolImpl::processDV(unsigned short port, void *packet, unsigned short size){
  unsigned char *packet_data = (unsigned char*)packet;
  unsigned short neighbor_id = ((unsigned short)packet_data[4] << 8) | packet_data[5];
  unsigned short self_id = ((unsigned short)packet_data[6] << 8) | packet_data[7];

  if(self_id != router_id){
    cout<<"Destination id != current router's id"<<endl;
    return;
  }

  unsigned short num_entries = ((unsigned short)packet_data[2] << 8) | packet_data[3];

  for(int i = 0; i < num_entries; i++){
    unsigned short node_id = ((unsigned short)packet_data[8 + 4*i] << 8) | packet_data[9 + 4*i];
    unsigned short cost = ((unsigned short)packet_data[10 + 4*i] << 8) | packet_data[11 + 4*i];
    unsigned short original_cost = dv_table[node_id].cost;
    // cur -> neighbor -> dest < cur->dest
    // update the cost and next-hop
    if(cost + dv_table[neighbor_id].cost < original_cost){
      dv_table[node_id].cost = cost + dv_table[neighbor_id].cost;
      dv_table[node_id].next_hop = neighbor_id;
    }
  }

}