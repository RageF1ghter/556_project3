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

    sendPing();
    sendDvUpdate();
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
    // send the dv_table to neighbors every 30s
    if(data == nullptr){
      sendDvUpdate();
      sys->set_alarm(this, dv_update_interval, nullptr);
    }else{
      int alarm_type = (int)(uintptr_t)data;
      // perdoicly send ping
      if(alarm_type == 1){
        sendPing();
        sys->set_alarm(this, 10000, (void*)1); // 10s
      }
      // check the dv_table every 1s, clean up expired routing entries 
      if(alarm_type == 2){
        handleNeighborTimeout();
        // cleanExpiredEntry();
        sys->set_alarm(this, 1000, (void*)2); // 1s
      }
    }
  }else{
    // Add your own code
    // LS
  }
}

void RoutingProtocolImpl::recv(unsigned short port, void *packet, unsigned short size) {
  // Packet from other routers
  unsigned char *packet_data = (unsigned char*)packet;
  ePacketType packet_type = (ePacketType)packet_data[0];

  switch (packet_type){
    case DATA:
      // transmit packet
      // printTheTable();
      // printThePorts();
      passPacket(packet, size);
      break;
    case PING:
      // cout<<"sending ping packet"<<endl;
      processPing(port, packet, size);
      break;
    case PONG:
      processPong(port, packet, size);
      break;
    case DV:
      processDV(port, packet, size);
      break;
    case LS:
      // processLS(port, packet, size);
      break;
    default: 
      free(packet);
      break;
  }

}

 
// send updated routing table to neighbors
void RoutingProtocolImpl::sendDvUpdate() {
  // format the dv_entries
  vector<pair<unsigned short, unsigned short>> dv_entries;
  
  // Build DV entries with poison reverse
  for (auto &row : dv_table) {
      unsigned short dest = row.first;
      unsigned short cost = row.second.cost;
      // Exclude self and unreachable nodes
      if (dest != router_id && cost != INFINITY_COST && dest != 0) {
          // Apply poison reverse 
          // TODO: CLEAN UP THE DV TABLE WHEN RECEIVE IT.
          // if (row.second.next_hop == neighbor.first) {
          //     cost = INFINITY_COST;
          // }
          dv_entries.push_back({dest, cost});
      }
  }
  
  // cout<<"dv_entries"<<endl;
  // for(auto& entry: dv_entries){
  //   cout<< "source: " << router_id <<" destionation: " << entry.first<<" , cost: " << entry.second<<endl;
  // }

  // send the dv_table to neighbors
  for(auto& neighbor: neighbor_ports){
    if(neighbor.first == router_id){
      continue;
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

    // Neighbor ID (2 bytes)
    packet_data[6] = (neighbor.first >> 8) & 0xFF;    // High byte
    packet_data[7] = neighbor.first & 0xFF;           // Low byte

    // Payload (Routing Entries)
    size_t offset = 8;
    for (size_t i = 0; i < dv_entries.size(); i++) {
      unsigned short dest = dv_entries[i].first;
      unsigned short cost = dv_entries[i].second;

      // Destionation ID (2 bytes)
      packet_data[offset] = (dest >> 8) & 0xFF;   // High byte
      packet_data[offset + 1] = dest & 0xFF;      // Low byte
      // Cost (2 bytes)
      packet_data[offset + 2] = (cost >> 8) & 0xFF;  // High byte
      packet_data[offset + 3] = cost & 0xFF;         // Low byte

      offset += 4; // Move to the next entry
    }

    // Send the DV update packet
    sys->send(neighbor.second, packet, (unsigned short)dv_packet_size);
  }
  // last_dv_update_time = sys->time();
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
    packet_data[9] = (curTime >> 16) & 0xFF;
    packet_data[10] = (curTime >> 8) & 0xFF;
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
    cout << router_id << " RTT to neighbor " << neighbor_id << ": " << rtt << endl;
    // printTheTable();

    // Update neighbor's last heard time
    neighbors[neighbor_id] = current_time;

    // Update neighbor-to-port mapping
    neighbor_ports[neighbor_id] = port;

    // Update the DV table
    if (dv_table.find(neighbor_id) == dv_table.end()) {
        // Neighbor doesn't exist in DV table, add it
        cout << "Adding new neighbor " << neighbor_id << " to DV table" << endl;
        dv_table[neighbor_id].destination = neighbor_id;
        dv_table[neighbor_id].cost = rtt;
        dv_table[neighbor_id].next_hop = neighbor_id;
        dv_table[neighbor_id].last_update_time = current_time;
    } else {
        // Neighbor exists, update its cost and handle previously unreachable neighbors
        unsigned int old_cost = dv_table[neighbor_id].cost;
        if (old_cost == INFINITY_COST || rtt < old_cost) {
          
            // Update to the new RTT if previously unreachable or RTT has improved
            cout << "Updating cost to neighbor " << neighbor_id << " in DV table" << endl;
            dv_table[neighbor_id].cost = rtt;
            dv_table[neighbor_id].next_hop = neighbor_id;
            dv_table[neighbor_id].last_update_time = current_time;

            // Update any dependent routes
            for (auto& entry : dv_table) {
                if (entry.second.next_hop == neighbor_id && entry.first != neighbor_id) {
                    entry.second.cost = rtt + (entry.second.cost - old_cost);
                    entry.second.last_update_time = current_time;
                }
            }
            // printTheTable();
        }
    }

    // Free the packet memory
    free(packet);

    // Trigger a DV update
    sendDvUpdate();
}


// clean dead neighbor
void RoutingProtocolImpl::handleNeighborTimeout() {
    unsigned int curTime = sys->time();
    auto it = neighbors.begin();

    while (it != neighbors.end()) {
        // Check if the neighbor is timed out
        if (curTime - it->second > neighbor_timeout) {
            unsigned short neighbor_id = it->first;

            // Clean the dv_table
            for (auto& row : dv_table) {
                if (row.second.next_hop == neighbor_id) {
                    row.second.cost = INFINITY_COST;
                    row.second.last_update_time = curTime;
                }
            }

            // Clean the neighbor_ports map
            if (neighbor_ports.find(neighbor_id) != neighbor_ports.end()) {
                cout << "Cleaning neighbor from ports: " << neighbor_id << endl;
                neighbor_ports.erase(neighbor_id);
            }

            // Erase the neighbor from the neighbors map
            cout << "Cleaning neighbor from timeout: " << neighbor_id << endl;
            it = neighbors.erase(it);

            // Print the updated tables for debugging
            // printTheTable();
            // printThePorts();
            // printTimeout();

            // Send a triggered DV update
            sendDvUpdate();
        } else {
            // Move to the next neighbor
            ++it;
        }
    }
}


// update the dv table
void RoutingProtocolImpl::processDV(unsigned short port, void *packet, unsigned short size){
    unsigned char *packet_data = (unsigned char*)packet;
    unsigned short neighbor_id = ((unsigned short)packet_data[4] << 8) | packet_data[5];
    unsigned short self_id = ((unsigned short)packet_data[6] << 8) | packet_data[7];

    if (self_id != router_id) {
        // cout<<"dest id: " << self_id << " current router's id: " << router_id << endl;
        cout << "Destination id != current router's id" << endl;
        printPacket(packet, size);
        free(packet);
        return;
    }

    unsigned short num_entries = ((unsigned short)packet_data[2] << 8) | packet_data[3];

    // Update neighbor's last heard time
    neighbors[neighbor_id] = sys->time();

    cout<<"before processing dv"<<endl;
    printTheTable();
    printPacket(packet, size);

    // process the packet
    for (int i = 0; i < num_entries; i++) {
        size_t offset = 8 + 4 * i;
        if (offset + 3 >= size) {
            // Prevent out-of-bounds access
            break;
        }

        unsigned short node_id = ((unsigned short)packet_data[offset] << 8) | packet_data[offset + 1];
        unsigned short cost = ((unsigned short)packet_data[offset + 2] << 8) | packet_data[offset + 3];

        // Handle INFINITY_COST and integer overflows
        unsigned int neighbor_cost = dv_table[neighbor_id].cost;
        unsigned int total_cost = (cost == INFINITY_COST || neighbor_cost == INFINITY_COST)
                                    ? INFINITY_COST
                                    : cost + neighbor_cost;

        if (total_cost > INFINITY_COST) {
            total_cost = INFINITY_COST;
        }

        // add an intermediate node to the dv_table
        if(dv_table.find(node_id) == dv_table.end()){
          dv_table[node_id].destination = node_id;
          dv_table[node_id].cost = total_cost;
          dv_table[node_id].next_hop = neighbor_id;   // next hop is the neighbor
          dv_table[node_id].last_update_time = sys->time();
        }else{
          unsigned int original_cost = dv_table[node_id].cost;
          // Update the cost and next-hop
          if (total_cost < original_cost) {
              dv_table[node_id].cost = (unsigned short)total_cost;
              dv_table[node_id].next_hop = neighbor_id;
              dv_table[node_id].last_update_time = sys->time();
          }
        }

        

    }
    cout<<"after processing dv"<<endl;
    printTheTable();

    free(packet);
}


void RoutingProtocolImpl::passPacket(void *packet, unsigned short size){
  unsigned char *packet_data = (unsigned char*)packet;
  unsigned short dest = ((unsigned short)packet_data[6] << 8) | packet_data[7];

  // printTheTable();
  if (dest == router_id) {
    cout << "Packet is received and freed" << endl;
    free(packet);
  } 
  else 
  {
    if (dv_table.find(dest) == dv_table.end() || dv_table[dest].cost == INFINITY_COST) {
        // Destination unreachable
        cout << "Destination unreachable, dropping packet" << endl;
        free(packet);
        return;
    }

    unsigned short next = dv_table[dest].next_hop;
    
    if (neighbor_ports.find(next) == neighbor_ports.end()) {
        // Next hop not found
        cout << "Next hop not found, dropping packet" << endl;
        free(packet);
        return;
    }
    // cout<<"DATA packet sent"<<endl;
    unsigned short port = neighbor_ports[next];
    sys->send(port, packet, size);
  }
}


void RoutingProtocolImpl::printTheTable(){
  cout<<router_id<<endl;
  for(auto& entry: dv_table){
    cout<<"destionation: " << entry.first<<" , next hop router: "<<entry.second.next_hop << " , port: " << neighbor_ports[entry.second.next_hop]<< " , cost: " << entry.second.cost<<endl;
  }
  cout<<endl;
}

void RoutingProtocolImpl::printThePorts(){
  cout<<router_id<<endl;
  for(auto& neighbor: neighbor_ports){
    cout<<"neighbor: " << neighbor.first<<" , port: "<<neighbor.second <<endl;
  }
  cout<<endl;
}

void RoutingProtocolImpl::printTimeout(){
  cout<<router_id<<endl;
  for(auto& neighbor: neighbors){
    cout<<"neighbor: " << neighbor.first<<" , lastseen: "<<neighbor.second <<endl;
  }
  cout<<endl;
}

void RoutingProtocolImpl::printPacket(void *packet, unsigned short size){
  unsigned char *packet_data = (unsigned char*)packet;
  unsigned short num_entries = ((unsigned short)packet_data[2] << 8) | packet_data[3];
  unsigned short source = ((unsigned short)packet_data[4] << 8) | packet_data[5];
  unsigned short dest = ((unsigned short)packet_data[6] << 8) | packet_data[7];
  cout<<"source: "<<source<<", dest: "<<dest<<endl;
  for (int i = 0; i < num_entries; i++) {
      size_t offset = 8 + 4 * i;
      if (offset + 3 >= size) {
          // Prevent out-of-bounds access
          break;
      }
      unsigned short node_id = ((unsigned short)packet_data[offset] << 8) | packet_data[offset + 1];
      unsigned short cost = ((unsigned short)packet_data[offset + 2] << 8) | packet_data[offset + 3];
      cout<<"to: " << node_id<<" , cost: " << cost<<endl;
  }

}