#include "RoutingProtocolImpl.h"
#include <unordered_set>
#include <cstring>

RoutingProtocolImpl::RoutingProtocolImpl(Node *n) : RoutingProtocol(n)
{
  sys = n;
  // add your own code
  // Initialize default intervals
  dv_update_interval = 30000; // 30 seconds in milliseconds
  neighbor_timeout = 15000;   // 15 seconds for neighbor timeouts in milliseconds
  last_dv_update_time = 0;    // Track last DV update time

  ls_update_interval = 30000; // 30 seconds for LS update interval
}

RoutingProtocolImpl::~RoutingProtocolImpl()
{
  // add your own code (if needed)
}

void RoutingProtocolImpl::init(unsigned short num_ports, unsigned short router_id, eProtocolType protocol_type)
{
  this->num_ports = num_ports;
  this->router_id = router_id;
  this->protocol_type = protocol_type;

  if (protocol_type == P_DV)
  {
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
    sys->set_alarm(this, 10000, (void *)1);

    // Set alarm to check for neighbor timeouts and routing table entry expiration every 1 second
    sys->set_alarm(this, 1000, (void *)2);
  }
  else if (protocol_type == P_LS)
  {
    ls_entry.last_update_time = sys->time();
    ls_entry.seq_num = 0;
    ls_entry.source_id = router_id;
    ls_entry.neighbor_rtt[router_id] = 0;

    sendPing();

    // Set periodic LS update alarm
    sys->set_alarm(this, ls_update_interval, (void *)0);

    // Set alarm to send ping messages for neighbor detection every 10 seconds
    sys->set_alarm(this, 10000, (void *)1);

    // Set alarm to check for neighbor timeouts and routing table entry expiration every 1 second
    sys->set_alarm(this, 1000, (void *)2);
  }
}

void RoutingProtocolImpl::handle_alarm(void *data)
{
  // DV
  if (protocol_type == P_DV)
  {
    // send the dv_table to neighbors every 30s
    if (data == nullptr)
    {
      sendDvUpdate();
      sys->set_alarm(this, dv_update_interval, nullptr);
    }
    else
    {
      int alarm_type = (int)(uintptr_t)data;
      // perdoicly send ping
      if (alarm_type == 1)
      {
        sendPing();
        sys->set_alarm(this, 10000, (void *)1); // 10s
      }
      // check the dv_table every 1s, clean up expired routing entries
      if (alarm_type == 2)
      {
        handleNeighborTimeout();
        // cleanExpiredEntry();
        sys->set_alarm(this, 1000, (void *)2); // 1s
      }
    }
  }
  // LS
  else
  {
    int alarm_type = (int)(uintptr_t)data;
    // send lsa every 30s
    if (alarm_type == 0)
    {
      floodLSA();
      sys->set_alarm(this, ls_update_interval, nullptr);
    }
    // send ping every 10s
    else if (alarm_type == 1)
    {
      sendPing();
      sys->set_alarm(this, 10000, (void *)1); // 10s
    }
    // check neighbor timeout every 1s
    else if (alarm_type == 2)
    {
      handleNeighborTimeout();
      sys->set_alarm(this, 1000, (void *)2); // 1s
    }
  }
}

void RoutingProtocolImpl::recv(unsigned short port, void *packet, unsigned short size)
{
  // Packet from other routers
  unsigned char *packet_data = (unsigned char *)packet;
  ePacketType packet_type = (ePacketType)packet_data[0];

  switch (packet_type)
  {
  // transmit packet
  case DATA:
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
    processLSA(port, packet, size);
    break;
  default:
    free(packet);
    break;
  }
}

////////////////////////// Common //////////////////////////////////////////
// received PING and send PONG back
void RoutingProtocolImpl::processPing(unsigned short port, void *packet, unsigned short size)
{
    if (size < 12)
    {
        // Packet too small, discard
        cout << "Packet too small, discard" << endl;
        free(packet);
        return;
    }

    // Decode the received packet
    unsigned char *received_data = (unsigned char *)packet;

    // Allocate a new packet for the PONG response
    void *pong_packet = malloc(size);
    unsigned char *pong_data = (unsigned char *)pong_packet;

    // Copy the received packet data to the new packet
    memcpy(pong_data, received_data, size);

    // Modify the new packet to be a PONG
    pong_data[0] = PONG;

    // Swap source and destination IDs
    pong_data[6] = received_data[4];
    pong_data[7] = received_data[5];
    pong_data[4] = (router_id >> 8) & 0xFF; // High byte of router_id
    pong_data[5] = router_id & 0xFF;        // Low byte of router_id

    // Timestamp remains unchanged

    // Send the PONG packet
    sys->send(port, pong_packet, size);

    // Free the received packet
    free(packet);
}


// recv PONG and update the neighbor_ports map
void RoutingProtocolImpl::processPong(unsigned short port, void *packet, unsigned short size)
{
  unsigned char *packet_data = (unsigned char *)packet;

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

  // DV
  if (protocol_type == P_DV)
  {
    // Update the DV table
    if (dv_table.find(neighbor_id) == dv_table.end())
    {
      // Neighbor doesn't exist in DV table, add it
      cout << "Adding new neighbor " << neighbor_id << " to DV table" << endl;
      dv_table[neighbor_id].destination = neighbor_id;
      dv_table[neighbor_id].cost = rtt;
      dv_table[neighbor_id].next_hop = neighbor_id;
      dv_table[neighbor_id].last_update_time = current_time;
    }
    else
    {
      // Neighbor exists, update its cost and handle previously unreachable neighbors
      unsigned int old_cost = dv_table[neighbor_id].cost;
      if (old_cost == INFINITY_COST || rtt < old_cost)
      {
        // Update to the new RTT if previously unreachable or RTT has improved
        cout << "Updating cost to neighbor " << neighbor_id << " in DV table" << endl;
        dv_table[neighbor_id].cost = rtt;
        dv_table[neighbor_id].next_hop = neighbor_id;
        dv_table[neighbor_id].last_update_time = current_time;

        // Update any dependent routes
        for (auto &entry : dv_table)
        {
          if (entry.second.next_hop == neighbor_id && entry.first != neighbor_id)
          {
            entry.second.cost = rtt + (entry.second.cost - old_cost);
            entry.second.last_update_time = current_time;
          }
        }
        // printTheTable();
      }
    }
    // Trigger a DV update
    sendDvUpdate();
  }
  // LS
  else
  {
    // add new neighbor or update
    ls_entry.neighbor_rtt[neighbor_id] = rtt;

    // update the lsbd
    lsdb[router_id].neighbor_rtt[neighbor_id] = rtt;
    // printLSDB();

    // update neighbor's last heard time
    neighbors[neighbor_id] = current_time;

    // Increment sequence number
    ls_entry.seq_num++;
    ls_entry.last_update_time = current_time;

    // Flood the updated LSA to all neighbors
    floodLSA();
  }

  // Free the packet memory
  free(packet);
}

// clean dead neighbor
void RoutingProtocolImpl::handleNeighborTimeout()
{
  unsigned int curTime = sys->time();
  auto it = neighbors.begin();
  while (it != neighbors.end())
  {
    // Check if the neighbor is timed out
    if (curTime - it->second > neighbor_timeout)
    {
      unsigned short neighbor_id = it->first;
      cout << router_id << " to " << it->first << " neighbor timeout" << endl;

      // DV protocol cleanup
      if (protocol_type == P_DV)
      {
        // Clean the dv_table
        for (auto &row : dv_table)
        {
          if (row.second.next_hop == neighbor_id)
          {
            row.second.cost = INFINITY_COST;
            row.second.last_update_time = curTime;
          }
        }

        // Clean the neighbor_ports map
        if (neighbor_ports.find(neighbor_id) != neighbor_ports.end())
        {
          cout << "Cleaning neighbor from ports: " << neighbor_id << endl;
          neighbor_ports.erase(neighbor_id);
        }
      }
      // LS protocol cleanup
      else
      {
        // Update self LSA
        ls_entry.neighbor_rtt[neighbor_id] = INFINITY_COST;
        lsdb[router_id].neighbor_rtt[neighbor_id] = INFINITY_COST;

        // Remove unreachable neighbor from the routing table
        for (auto it_rt = routing_table.begin(); it_rt != routing_table.end();)
        {
          if (it_rt->second == neighbor_id)
          {
            it_rt = routing_table.erase(it_rt); // Safe erase
          }
          else
          {
            ++it_rt;
          }
        }
        runDijkstra();
        printLSDB();
        printRoutingTable();
      }

      // Remove the neighbor from the neighbors map safely
      cout << "Removing neighbor " << neighbor_id << " from neighbors list due to timeout." << endl;
      it = neighbors.erase(it); // Safely update the iterator
    }
    else
    {
      // Move to the next neighbor
      ++it;
    }
  }
}

// pass the packet to the next hop
void RoutingProtocolImpl::passPacket(void *packet, unsigned short size)
{
  unsigned char *packet_data = (unsigned char *)packet;
  unsigned short dest = ((unsigned short)packet_data[6] << 8) | packet_data[7];

  // printTheTable();
  if (dest == router_id)
  {
    cout << "Packet is received and freed" << endl;
    free(packet);
  }
  else
  {
    if (protocol_type == P_DV)
    {
      if (dv_table.find(dest) == dv_table.end() || dv_table[dest].cost == INFINITY_COST)
      {
        // Destination unreachable
        cout << "Destination unreachable, dropping packet" << endl;
        free(packet);
        return;
      }

      unsigned short next = dv_table[dest].next_hop;

      if (neighbor_ports.find(next) == neighbor_ports.end())
      {
        // Next hop not found
        cout << "Next hop not found, dropping packet" << endl;
        free(packet);
        return;
      }
      // cout<<"DATA packet sent"<<endl;
      unsigned short port = neighbor_ports[next];
      sys->send(port, packet, size);
    }
    if (protocol_type == P_LS)
    {
      // find next hop
      if (routing_table.find(dest) == routing_table.end())
      {
        // Destination unreachable
        cout << "Destination unreachable, dropping packet" << endl;
        free(packet);
        return;
      }
      unsigned short next = routing_table[dest];

      // find next hop's port
      if (neighbor_ports.find(next) == neighbor_ports.end())
      {
        cout << "Next hop not found, dropping packet" << endl;
        free(packet);
        return;
      }
      // cout<<"DATA packet sent"<<endl;
      unsigned short port = neighbor_ports[next];
      sys->send(port, packet, size);
    }
  }
}


////////////////////////////////////////// DV //////////////////////////////////////////
// send updated routing table to neighbors
void RoutingProtocolImpl::sendDvUpdate()
{
  // format the dv_entries
  vector<pair<unsigned short, unsigned short>> dv_entries;

  // Build DV entries with poison reverse
  for (auto &row : dv_table)
  {
    unsigned short dest = row.first;
    unsigned short cost = row.second.cost;
    // Exclude self and unreachable nodes
    if (dest != router_id && dest != 0)
    {
      dv_entries.push_back({dest, cost});
    }
  }

  // cout<<"dv_entries"<<endl;
  // for(auto& entry: dv_entries){
  //   cout<< "source: " << router_id <<" destionation: " << entry.first<<" , cost: " << entry.second<<endl;
  // }

  // send the dv_table to neighbors
  for (auto &neighbor : neighbor_ports)
  {
    if (neighbor.first == router_id)
    {
      continue;
    }
    // Packet format: type(1 byte) + reserved(1 byte) + size(2 bytes) + source(2 bytes) + destination(2 bytes) + entries
    size_t dv_packet_size = 8 + dv_entries.size() * 4; // 8 bytes header + 4 bytes per entry
    void *packet = malloc(dv_packet_size);
    unsigned char *packet_data = (unsigned char *)packet;

    // Packet Type (1 byte)
    packet_data[0] = DV; // DV should be an unsigned char value

    // Reserved (1 byte)
    packet_data[1] = 0; // Reserved byte

    // Number of entries (2 bytes)
    unsigned short num_entries = (unsigned short)dv_entries.size();
    packet_data[2] = (num_entries >> 8) & 0xFF; // High byte
    packet_data[3] = num_entries & 0xFF;        // Low byte

    // Source ID (2 bytes)
    packet_data[4] = (router_id >> 8) & 0xFF; // High byte
    packet_data[5] = router_id & 0xFF;        // Low byte

    // Neighbor ID (2 bytes)
    packet_data[6] = (neighbor.first >> 8) & 0xFF; // High byte
    packet_data[7] = neighbor.first & 0xFF;        // Low byte

    // Payload (Routing Entries)
    size_t offset = 8;
    for (size_t i = 0; i < dv_entries.size(); i++)
    {
      unsigned short dest = dv_entries[i].first;
      unsigned short cost = dv_entries[i].second;

      // Destionation ID (2 bytes)
      packet_data[offset] = (dest >> 8) & 0xFF; // High byte
      packet_data[offset + 1] = dest & 0xFF;    // Low byte
      // Cost (2 bytes)
      packet_data[offset + 2] = (cost >> 8) & 0xFF; // High byte
      packet_data[offset + 3] = cost & 0xFF;        // Low byte

      offset += 4; // Move to the next entry
    }

    // Send the DV update packet
    sys->send(neighbor.second, packet, (unsigned short)dv_packet_size);
  }
  // last_dv_update_time = sys->time();
}

// detect existance of neighbor
void RoutingProtocolImpl::sendPing()
{
  for (unsigned short port = 0; port < num_ports; port++)
  {
    // Packet format: type(1 byte) + reserved(1 byte) + size(2 bytes) + source(2 bytes) + destination(2 bytes unused) + payload(4 bytes)
    size_t ping_packet_size = 12;
    void *packet = malloc(ping_packet_size);
    unsigned char *packet_data = (unsigned char *)packet;
    // Packet Type (1 byte)
    packet_data[0] = PING;
    // Reserved (1 byte)
    packet_data[1] = 0; // Reserved byte
    // Number of entries (2 bytes)
    packet_data[2] = 0;
    packet_data[3] = 0;
    // Source ID (2 bytes)
    packet_data[4] = (router_id >> 8) & 0xFF; // High byte
    packet_data[5] = router_id & 0xFF;        // Low byte
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

// update the dv table
void RoutingProtocolImpl::processDV(unsigned short port, void *packet, unsigned short size)
{
  unsigned char *packet_data = (unsigned char *)packet;
  unsigned short neighbor_id = ((unsigned short)packet_data[4] << 8) | packet_data[5];
  unsigned short self_id = ((unsigned short)packet_data[6] << 8) | packet_data[7];

  if (self_id != router_id)
  {
    // cout<<"dest id: " << self_id << " current router's id: " << router_id << endl;
    cout << "Destination id != current router's id" << endl;
    printPacket(packet, size);
    free(packet);
    return;
  }

  unsigned short num_entries = ((unsigned short)packet_data[2] << 8) | packet_data[3];

  // Update neighbor's last heard time
  neighbors[neighbor_id] = sys->time();

  // cout<<"before processing dv"<<endl;
  // printTheTable();
  // printPacket(packet, size);

  // process the packet
  for (int i = 0; i < num_entries; i++)
  {
    size_t offset = 8 + 4 * i;
    if (offset + 3 >= size)
    {
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

    if (total_cost > INFINITY_COST)
    {
      total_cost = INFINITY_COST;
    }

    // add an intermediate node to the dv_table
    if (dv_table.find(node_id) == dv_table.end())
    {
      dv_table[node_id].destination = node_id;
      dv_table[node_id].cost = total_cost;
      dv_table[node_id].next_hop = neighbor_id; // next hop is the neighbor
      dv_table[node_id].last_update_time = sys->time();
    }
    else
    {
      unsigned int original_cost = dv_table[node_id].cost;
      // Update the cost and next-hop
      // set the cost to infinity if the dead link in the path
      if (total_cost == INFINITY_COST && dv_table[node_id].next_hop == neighbor_id)
      {
        dv_table[node_id].cost = (unsigned short)total_cost;
        dv_table[node_id].next_hop = neighbor_id;
        dv_table[node_id].last_update_time = sys->time();
      }
      else if (total_cost < original_cost)
      {
        dv_table[node_id].cost = (unsigned short)total_cost;
        dv_table[node_id].next_hop = neighbor_id;
        dv_table[node_id].last_update_time = sys->time();
      }
    }
  }
  // cout<<"after processing dv"<<endl;
  // printTheTable();

  free(packet);
}


///////////////////////////// LS ////////////////////////////////////////////
// sending self lsa to all neighbors
void RoutingProtocolImpl::floodLSA()
{
  // format the lsa content
  vector<pair<unsigned short, unsigned short>> neighbor_costs;
  for (const auto &neighbor : ls_entry.neighbor_rtt)
  {
    unsigned short dest = neighbor.first;
    unsigned short cost = neighbor.second;

    if (dest != router_id && dest != 0)
    {
      neighbor_costs.push_back({dest, cost});
    }
  }

  for (auto &neighbor : neighbor_ports)
  {
    if (neighbor.first == router_id)
    {
      continue;
    }
    size_t offset = 12;
    // Packet format: type(1 byte) + reserved(1 byte) + size(2 bytes) + source(2 bytes) + destination(2 bytes) + entries
    size_t ls_packet_size = offset + ls_entry.neighbor_rtt.size() * 4; // 8 bytes header + 4 bytes per entry
    void *packet = malloc(ls_packet_size);
    unsigned char *packet_data = (unsigned char *)packet;

    // Packet Type (1 byte)
    packet_data[0] = LS;

    // Reserved (1 byte)
    packet_data[1] = 0; // Reserved byte

    // Number of entries (2 bytes)
    unsigned short num_entries = (unsigned short)ls_entry.neighbor_rtt.size();
    packet_data[2] = (num_entries >> 8) & 0xFF; // High byte
    packet_data[3] = num_entries & 0xFF;        // Low byte

    // Source ID (2 bytes)
    packet_data[4] = (router_id >> 8) & 0xFF; // High byte
    packet_data[5] = router_id & 0xFF;        // Low byte

    // Ignore (2 bytes)
    packet_data[6] = 0;
    packet_data[7] = 0;

    // sequence number (4 bytes)
    packet_data[8] = (ls_entry.seq_num >> 24) & 0xFF;
    packet_data[9] = (ls_entry.seq_num >> 16) & 0xFF;
    packet_data[10] = (ls_entry.seq_num >> 8) & 0xFF;
    packet_data[11] = ls_entry.seq_num & 0xFF;

    // Payload (Routing Entries)
    cout << router_id << " LSA payload sent to " << neighbor.first << endl;
    for (pair<unsigned short, unsigned short> neighbor_cost : neighbor_costs)
    {
      unsigned short dest = neighbor_cost.first;
      unsigned short cost = neighbor_cost.second;
      if (dest == 0)
      {
        continue;
      }
      cout << "dest: " << dest << " cost: " << cost << endl;
      // Destionation ID (2 bytes)
      packet_data[offset] = (dest >> 8) & 0xFF; // High byte
      packet_data[offset + 1] = dest & 0xFF;    // Low byte
      // Cost (2 bytes)
      packet_data[offset + 2] = (cost >> 8) & 0xFF; // High byte
      packet_data[offset + 3] = cost & 0xFF;        // Low byte

      offset += 4; // Move to the next entry
    }

    // Send the LS update packet
    sys->send(neighbor.second, packet, (unsigned short)ls_packet_size);
  }
}

// pass LSA to other nieghbors
void RoutingProtocolImpl::passLSA(unsigned short port, void *packet, unsigned short size)
{
  for (auto &neighbor : neighbor_ports)
  {
    // exclude self and the incoming neighbor
    if (neighbor.second == port || neighbor.first == router_id)
    {
      continue;
    }
    // Create a copy of the packet to forward
    void *packet_copy = malloc(size);
    memcpy(packet_copy, packet, size);

    // Forward the LSA to the neighbor
    sys->send(neighbor.second, packet_copy, size);
  }
  // Free the original packet if necessary
  free(packet);
}

// process the LSA packet, update the lsdb, and pass to other neighbors
void RoutingProtocolImpl::processLSA(unsigned short port, void *packet, unsigned short size)
{
  unsigned char *packet_data = (unsigned char *)packet;

  printPacket(packet, size);
  // Extract LSA details
  unsigned short source_id = ((unsigned short)packet_data[4] << 8) | packet_data[5];
  unsigned int seq_num = ((unsigned int)packet_data[8] << 24) |
                         ((unsigned int)packet_data[9] << 16) |
                         ((unsigned int)packet_data[10] << 8) |
                         ((unsigned int)packet_data[11]);
  cout << router_id << " Received LSA from " << source_id << endl;
  // Check if LSA is new
  if (lsdb.find(source_id) == lsdb.end() || seq_num > lsdb[source_id].seq_num)
  {
    // Update LSDB with new LSA
    LSA new_lsa;
    new_lsa.source_id = source_id;
    new_lsa.seq_num = seq_num;
    new_lsa.last_update_time = sys->time(); // update the last update time
    new_lsa.neighbor_rtt.clear();

    // Parse neighbors from the packet
    unsigned short num_neighbors = ((unsigned short)packet_data[2] << 8) | packet_data[3];
    size_t offset = 12;
    for (int i = 0; i < num_neighbors; i++)
    {
      unsigned short neighbor_id = ((unsigned short)packet_data[offset] << 8) | packet_data[offset + 1];
      unsigned short cost = ((unsigned short)packet_data[offset + 2] << 8) | packet_data[offset + 3];
      if (neighbor_id == 0)
      {
        continue;
      }
      new_lsa.neighbor_rtt[neighbor_id] = cost;
      offset += 4;
    }

    // Update the LSDB
    lsdb[source_id] = new_lsa;

    printLSDB();

    // update the routing table
    runDijkstra();

    // Propagate the LSA to other neighbors
    passLSA(port, packet, size);
  }
  else
  {
    // LSA is stale or duplicate, discard it
    free(packet);
  }
}

void RoutingProtocolImpl::runDijkstra()
{
  // Priority queue to store {cost, router_id}, sorted by cost
  priority_queue<pair<unsigned int, unsigned short>,
                 vector<pair<unsigned int, unsigned short>>,
                 greater<pair<unsigned int, unsigned short>>>
      pq;

  unordered_map<unsigned short, unsigned int> dist;   // Distance to each router
  unordered_map<unsigned short, unsigned short> prev; // Previous router for path reconstruction
  unordered_set<unsigned short> visited;              // Set of visited nodes

  // Initialize distances
  for (auto &entry : lsdb)
  {
    dist[entry.first] = INFINITY_COST; // Start with all nodes at infinite cost
  }
  dist[router_id] = 0;              // Distance to self is 0
  pq.push(make_pair(0, router_id)); // Add the current router to the queue

  // Dijkstra's Algorithm
  while (!pq.empty())
  {
    pair<unsigned int, unsigned short> current = pq.top();
    pq.pop();

    unsigned int current_cost = current.first;
    unsigned short current_router = current.second;

    // If already visited, skip
    if (visited.find(current_router) != visited.end())
      continue;

    visited.insert(current_router); // Mark as visited

    // If the cost in the queue is outdated, skip
    if (current_cost > dist[current_router])
      continue;

    // Process neighbors of the current router
    if (lsdb.find(current_router) != lsdb.end())
    {
      for (auto &neighbor : lsdb[current_router].neighbor_rtt)
      {
        unsigned short neighbor_id = neighbor.first;
        unsigned int link_cost = neighbor.second;

        // Skip neighbors with infinite cost or invalid links
        if (link_cost == INFINITY_COST)
          continue;

        // Calculate new cost to neighbor
        unsigned int new_cost = current_cost + link_cost;
        if (new_cost > INFINITY_COST)
          new_cost = INFINITY_COST;

        if (new_cost < dist[neighbor_id])
        {
          dist[neighbor_id] = new_cost;
          prev[neighbor_id] = current_router;
          pq.push(make_pair(new_cost, neighbor_id));
        }
      }
    }
  }

  // Build the routing table from the shortest paths
  routing_table.clear();
  for (auto &entry : dist)
  {
    unsigned short destination = entry.first;
    if (destination == router_id || dist[destination] == INFINITY_COST)
      continue;

    // Reconstruct the path to find the next hop
    unsigned short next_hop = destination;
    unordered_set<unsigned short> path_nodes; // Nodes in the current path

    while (true)
    {
      if (path_nodes.find(next_hop) != path_nodes.end())
      {
        // Loop detected
        cout << "Error: Loop detected while reconstructing path to " << destination << endl;
        break;
      }
      path_nodes.insert(next_hop);

      if (prev.find(next_hop) == prev.end())
      {
        // No path to destination
        cout << "Error: No path to " << destination << endl;
        break;
      }

      if (prev[next_hop] == router_id)
      {
        // Found the next hop
        routing_table[destination] = next_hop;
        break;
      }

      next_hop = prev[next_hop];
    }
  }

  // Print the computed routing table for debugging
  cout << "Routing Table for Router " << router_id << ":" << endl;
  for (auto &entry : routing_table)
  {
    cout << "Destination: " << entry.first
         << ", Next Hop: " << entry.second
         << ", Cost: " << dist[entry.first] << endl;
  }
}



///////////////////////////// Debugging //////////////////////////////////////////
void RoutingProtocolImpl::printTheTable()
{
  cout << router_id << endl;
  for (auto &entry : dv_table)
  {
    cout << "destionation: " << entry.first << " , next hop router: " << entry.second.next_hop << " , port: " << neighbor_ports[entry.second.next_hop] << " , cost: " << entry.second.cost << " , last_updated_at: " << entry.second.last_update_time << endl;
  }
  cout << endl;
}

void RoutingProtocolImpl::printThePorts()
{
  cout << router_id << endl;
  for (auto &neighbor : neighbor_ports)
  {
    cout << "neighbor: " << neighbor.first << " , port: " << neighbor.second << endl;
  }
  cout << endl;
}

void RoutingProtocolImpl::printTimeout()
{
  cout << router_id << endl;
  for (auto &neighbor : neighbors)
  {
    cout << "neighbor: " << neighbor.first << " , lastseen: " << neighbor.second << endl;
  }
  cout << endl;
}

void RoutingProtocolImpl::printPacket(void *packet, unsigned short size)
{
  unsigned char *packet_data = (unsigned char *)packet;
  unsigned short num_entries = ((unsigned short)packet_data[2] << 8) | packet_data[3];
  unsigned short source = ((unsigned short)packet_data[4] << 8) | packet_data[5];
  unsigned short dest = ((unsigned short)packet_data[6] << 8) | packet_data[7];
  cout << "source: " << source << ", dest: " << dest << endl;
  for (int i = 0; i < num_entries; i++)
  {
    size_t offset = 12 + 4 * i;
    if (offset + 3 >= size)
    {
      // Prevent out-of-bounds access
      break;
    }
    unsigned short node_id = ((unsigned short)packet_data[offset] << 8) | packet_data[offset + 1];
    unsigned short cost = ((unsigned short)packet_data[offset + 2] << 8) | packet_data[offset + 3];
    cout << "to: " << node_id << " , cost: " << cost << endl;
  }
}

void RoutingProtocolImpl::printLSDB()
{
  cout << "======================" << endl;
  cout << "LSDB  for " << router_id << endl;
  for (auto &lsa : lsdb)
  {
    cout << "source: " << lsa.first << endl;
    for (auto &neighbor : lsa.second.neighbor_rtt)
    {
      cout << "neighbor: " << neighbor.first << " , cost: " << neighbor.second << endl;
    }
  }
  cout << "======================" << endl;
}

void RoutingProtocolImpl::printRoutingTable()
{
  cout<< "/////////////////////" << endl;
  cout<< "Routing Table for router " << router_id << endl;
  for(auto &entry: routing_table){
    cout << "dest: " << entry.first << " , next hop: " << entry.second << endl;
  }
  cout<< "/////////////////////" << endl;
}