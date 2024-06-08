/*
 * Noxim - the NoC Simulator
 *
 * (C) 2005-2018 by the University of Catania
 * For the complete list of authors refer to file ../doc/AUTHORS.txt
 * For the license applied to these sources refer to file ../doc/LICENSE.txt
 *
 * This file contains the implementation of the router
 */

#include "Router.h"

inline int toggleKthBit(int n, int k)
{
	return (n ^ (1 << (k - 1)));
}

void Router::process()
{
	txProcess(); // in case flit go from RX stage to LT in a cycle
	ib2mmProcess();
	rxProcess();
}

void Router::rxProcess()
{
	if (reset.read())
	{
		TBufferFullStatus bfs;
		// Clear outputs and indexes of receiving protocol
		for (int i = 0; i < DIRECTIONS + 2; i++)
		{
			ack_rx[i].write(0);
			current_level_rx[i] = 0;
			buffer_full_status_rx[i].write(bfs);
		}
		routed_flits = 0;
		local_drained = 0;
	}
	else
	{
		// This process simply sees a flow of incoming flits. All arbitration
		// and wormhole related issues are addressed in the txProcess()
		// assert(false);

		for (int i = 0; i < DIRECTIONS + 2; i++)
		{
			// To accept a new flit, the following conditions must match:
			// 1) there is an incoming request
			// 2) there is a free slot in the input buffer of direction i
			// LOG<<"****RX****DIRECTION ="<<i<<  endl;

			if (req_rx[i].read() == 1 - current_level_rx[i])
			{
				Flit received_flit = flit_rx[i].read();
				// LOG<<"request opposite to the current_level, reading flit "<<received_flit<<endl;

				int vc = received_flit.vc_id;

				if (!buffer[i][vc].IsFull())
				{
					// Store the incoming flit in the circular buffer
					buffer[i][vc].Push(received_flit);

					LOG << " Flit " << received_flit << " collected from Input[" << i << "][" << vc << "]" << endl;

					power.bufferRouterPush();

					// Negate the old value for Alternating Bit Protocol (ABP)
					// LOG<<"INVERTING CL FROM "<< current_level_rx[i]<< " TO "<<  1 - current_level_rx[i]<<endl;
					current_level_rx[i] = 1 - current_level_rx[i];

					// if a new flit is injected from local PE
					if (received_flit.src_id == local_id)
						power.networkInterface();
				}
				else // buffer full
				{
					// should not happen with the new TBufferFullStatus control signals
					// except for flit coming from local PE, which don't use it
					LOG << " Flit " << received_flit << " buffer full Input[" << i << "][" << vc << "]" << endl;
					assert(i == DIRECTION_LOCAL);
				}
			}
			ack_rx[i].write(current_level_rx[i]);

			// updates the mask of VCs to prevent incoming data on full buffers @ rx stage should only push flits to input buffer
			TBufferFullStatus bfs;
			for (int vc = 0; vc < GlobalParams::n_virtual_channels; vc++)
			{
				bfs.mask[vc] = buffer[i][vc].IsFull();
				bfs.precise_occupied_slot[vc] = buffer[i][vc].Size();
			}
			buffer_full_status_rx[i].write(bfs);
		}
	}
}

void Router::ib2mmProcess()
{
	if (reset.read())
	{
	}
	else
	{
		// get LAT
		int LAT[DIRECTIONS + 2] = {0};

		for (int j = 0; j < MAX_MIDDLE_MEMORY; j++)
		{
			for (int k = 0; k < GlobalParams::MM_buffer_depth; k++)
			{
				// (k-circular_timestamp) % GlobalParams::MM_buffer_depth // in case (k-circular_timestamp) could be negative
				int def_timestamp = (((k - circular_timestamp) % GlobalParams::MM_buffer_depth) + GlobalParams::MM_buffer_depth) % GlobalParams::MM_buffer_depth;
				if (!Middle_buffer[j][k].IsEmpty())
				{

					Flit flit = Middle_buffer[j][k].Front();
					if (def_timestamp > LAT[flit.next_outport])
						LAT[flit.next_outport] = def_timestamp;
				}
			}
			// get not_available_slots of next-hop buffers
			int not_available_slots[DIRECTIONS + 2][MAX_VIRTUAL_CHANNELS];

			for (int j = 0; j < DIRECTIONS + 2; j++)
			{

				TBufferFullStatus bfs = buffer_full_status_tx[j].read();

				for (int k = 0; k < MAX_VIRTUAL_CHANNELS; k++)
				{
					not_available_slots[j][k] = bfs.precise_occupied_slot[k];
				}
			}
			// add Middle memory occupied slots
			for (int j = 0; j < MAX_MIDDLE_MEMORY; j++)
			{
				for (int k = 0; k < GlobalParams::MM_buffer_depth; k++)
				{

					if (!Middle_buffer[j][k].IsEmpty())
					{
						Flit flit = Middle_buffer[j][k].Front();
						not_available_slots[flit.next_outport][flit.vc_id] += 1;
					}
				}
			}

			/*
				phase0 choose WIN VCs of each input buffer	// I don't care if it follows hardware design or not
			*/
			/*
				phase1 route WIN VCs' front flits
			*/
			int Candidate_outport[DIRECTIONS + 2] = {0};
			RouteData Candidate_RouteData[DIRECTIONS + 2];

			for (int j = 0; j < DIRECTIONS + 2; j++) {
				int i = j; // no need rotation here

				for (int k = 0; k < GlobalParams::n_virtual_channels; k++) {
					int vc = (start_from_vc[i] + k) % (GlobalParams::n_virtual_channels);

					if (!buffer[i][vc].IsEmpty())
					{
						Flit flit = buffer[i][vc].Front();
						power.bufferRouterFront(); // it's autually zero

						// LOG<< "current_id= "<< route_data.current_id <<" for sending " << flit << endl;
						RouteData route_data;
						route_data.current_id = local_id;
						route_data.dir_in = i;
						route_data.src_id = flit.src_id;
						route_data.dst_id = flit.dst_id;
						route_data.vc_id = flit.vc_id;
						route_data.routed = true;

						// TODO: see PER POSTERI (adaptive routing should not recompute route if already reserved)
						int o = route(route_data);

						// if (o>=DIRECTION_HUB_RELAY)
						// {
						// 	Flit f = buffer[i][vc].Pop();
						// 	f.hub_relay_node = o-DIRECTION_HUB_RELAY;
						// 	buffer[i][vc].Push(f);
						// 	o = DIRECTION_HUB;
						// }

						Candidate_outport[i] = o;
						Candidate_RouteData[i] = route_data;
						break;
					}
				}
				start_from_vc[i] = (start_from_vc[i] + 1) % GlobalParams::n_virtual_channels;
			} // LOG << "end of phase0, 1" << endl;

			/*
				phase2 check CR & VA (by buffer_full_status_tx.precise_occupied_slot + status of Middle_buffer = not_available_slots)
				// pop flits to Middle Memory here
			*/

			int offset[DIRECTIONS + 2] = {0};
			int TS[DIRECTIONS + 2] = {0};

			// TS calculate

			// Add latest output-port timestamp to TS
			// Add offset
			for (int j = 0; j < DIRECTIONS + 2; j++)
			{
				// first WIN flit to be assigned changes through time -> to have the same input-port importance
				int i = (start_from_port + j) % (DIRECTIONS + 2); // rotate i
				int o = Candidate_outport[i];

				// curr + 1
				TS[i] += (offset[o] + LAT[o]); // avoid departure conflict
				offset[o] += 1;
			}

			// From input buffer to middle memory (next_outport assign here)
			for (int j = 0; j < DIRECTIONS + 2; j++)
			{
				int i = (start_from_port + j) % (DIRECTIONS + 2); // rotate i

				pair<bool, pair<int, int>> p;

				// LOG << j << ":" << TS[i] << " " << offset[i] << " " << LAT[i] << " " << Candidate_RouteData[i].routed <<endl; // for  debug

				if (TS[i] > GlobalParams::MM_buffer_depth || !Candidate_RouteData[i].routed)
				{
					p = make_pair(false, make_pair(0, 0)); // (VACRpass, (vc_id, MM index))
				}
				else
				{

					TS[i] = (TS[i] + circular_timestamp) % 4; // 4 is GlobalParams::MM_buffer_depth

					p = GetValidNVC(not_available_slots, TS[i], Candidate_outport[i]);
					// LOG << "i = " << i << endl;
					LOG << "new: " << not_available_slots[Candidate_outport[i]][p.second.first] << endl;
				}

				if (p.first) // if pass
				{ // transfer flit from input buffer to middle memory and change vc_id
					Flit flit = buffer[i][Candidate_RouteData[i].vc_id].Pop();
					// LOG << "Pop buffer[ " << i << " ]" << endl;
					flit.vc_id = p.second.first;
					LOG << "newvc: " << p.second.first << endl;
					;
					flit.next_outport = Candidate_outport[i];
					Middle_buffer[p.second.second][TS[i]].Push(flit);
				}
			}

			start_from_port = (start_from_port + 1) % (DIRECTIONS + 2);

			// updates the mask of VCs to prevent incoming data on full buffers @ ib2mm stage should only pop flits from input buffer

			for (int i = 0; i < DIRECTIONS + 2; i++)
			{
				TBufferFullStatus bfs;
				for (int vc = 0; vc < GlobalParams::n_virtual_channels; vc++)
				{
					bfs.mask[vc] = buffer[i][vc].IsFull();
					bfs.precise_occupied_slot[vc] = buffer[i][vc].Size();
				}
				buffer_full_status_rx[i].write(bfs);
			}
		}
	}
}

void Router::txProcess()
{

	if (reset.read())
	{
		// Clear outputs and indexes of transmitting protocol
		for (int i = 0; i < DIRECTIONS + 2; i++)
		{
			req_tx[i].write(0);
			current_level_tx[i] = 0;
		}

		// Reset circular timestamp
		circular_timestamp = 0; // indicate which position of Middle Memory is current time
	}
	else
	{

		for (int i = 0; i < MAX_MIDDLE_MEMORY; i++)
		{
			if (!Middle_buffer[i][circular_timestamp].IsEmpty())
			{
				Flit flit = Middle_buffer[i][circular_timestamp].Front();
				int o = flit.next_outport;
				int vc = flit.vc_id;
				bool ABP = (current_level_tx[o] == ack_tx[o].read());
				bool bfsmaskfalse = (buffer_full_status_tx[o].read().mask[vc] == false);
				if (ABP && bfsmaskfalse)
				{

					// if (GlobalParams::verbose_mode > VERBOSE_OFF)
					LOG << "Middle Memory [" << i << "] forwarded to Output[" << o << "][" << vc << "], flit: " << flit << endl;

					flit_tx[o].write(flit);
					current_level_tx[o] = 1 - current_level_tx[o];
					req_tx[o].write(current_level_tx[o]);
					Middle_buffer[i][circular_timestamp].Pop();

					/* Power & Stats ------------------------------------------------- */
					if (o == DIRECTION_HUB)
						power.r2hLink();
					else
						power.r2rLink();

					power.bufferRouterPop();
					power.crossBar();

					if (o == DIRECTION_LOCAL)
					{
						power.networkInterface();
						LOG << "Consumed flit " << flit << endl;
						stats.receivedFlit(sc_time_stamp().to_double() / GlobalParams::clock_period_ps, flit);
						if (GlobalParams::max_volume_to_be_drained)
						{
							if (drained_volume >= GlobalParams::max_volume_to_be_drained)
								sc_stop();
							else
							{
								drained_volume++;
								local_drained++;
							}
						}
					}
					else if (i != DIRECTION_LOCAL) // not generated locally
						routed_flits++;
					/* End Power & Stats ------------------------------------------------- */

					// LOG<<"END_OK_cl_tx="<<current_level_tx[o]<<"_req_tx="<<req_tx[o].read()<<" _ack= "<<ack_tx[o].read()<< endl;
				}
				else
				{
					LOG << "ABP: " << ABP << ", bfsmaskfalse: " << bfsmaskfalse << endl;
					LOG << " Cannot forward Input[" << i << "][" << vc << "] to Output[" << o << "], flit: " << flit << endl;
					LOG << " **DEBUG ABP: current_level_tx: " << current_level_tx[o] << " ack_tx: " << ack_tx[o].read() << endl;
					LOG << " **DEBUG buffer_full_status_tx " << buffer_full_status_tx[o].read().mask[vc] << endl;
				}
			}

		} // for loop directions

		// end of tx stage update circular TS
		circular_timestamp++;
		if (circular_timestamp >= GlobalParams::MM_buffer_depth)
			circular_timestamp = 0;
	}
}

// return (VACRpass, (vc_id, MM index))
pair<bool, pair<int, int>> Router::GetValidNVC(int not_available_slots[][MAX_VIRTUAL_CHANNELS], int TS, int next_port)
{
	bool pass = 0;
	int vc_id = 0;
	int MM_index = 0;

	// VA
	for (int j = 0; j < GlobalParams::n_virtual_channels; ++j)
	{

		int i = (start_from_port + j) % GlobalParams::n_virtual_channels;

		if (!pass && not_available_slots[next_port][i] < GlobalParams::buffer_depth)
		{ // buffer_depth == 4
			pass = 1;
			vc_id = i;
		}
	}

	if (!pass)
	{
		LOG << "port[" << next_port << "] all vc full" << endl;
		return make_pair(pass, make_pair(0, 0));
	}

	// CR
	pass = 0;
	for (int j = 0; j < MAX_MIDDLE_MEMORY; ++j)
	{
		if (!pass && Middle_buffer[j][TS].IsEmpty())
		{
			pass = 1;
			for (int i = 0; i < MAX_MIDDLE_MEMORY; ++i)
			{
				if (i != j && !Middle_buffer[i][TS].IsEmpty() && Middle_buffer[i][TS].Front().next_outport == next_port)
					pass = 0;
			}
			if (pass)
			{
				MM_index = j;
				LOG << "not_available_slots[next_port][vc_id]: " << not_available_slots[next_port][vc_id] << endl;
				not_available_slots[next_port][vc_id]++;
			}
		}
	}
	return make_pair(pass, make_pair(vc_id, MM_index));
}

NoP_data Router::getCurrentNoPData()
{
	NoP_data NoP_data;

	for (int j = 0; j < DIRECTIONS; j++)
	{
		try
		{
			NoP_data.channel_status_neighbor[j].free_slots = free_slots_neighbor[j].read();
			NoP_data.channel_status_neighbor[j].available = (reservation_table.isNotReserved(j));
		}
		catch (int e)
		{
			if (e != NOT_VALID)
				assert(false);
			// Nothing to do if an NOT_VALID direction is caught
		};
	}

	NoP_data.sender_id = local_id;

	return NoP_data;
}

void Router::perCycleUpdate()
{
	if (reset.read())
	{
		for (int i = 0; i < DIRECTIONS + 1; i++)
			free_slots[i].write(buffer[i][DEFAULT_VC].GetMaxBufferSize());
	}
	else
	{
		selectionStrategy->perCycleUpdate(this);

		power.leakageRouter();
		for (int i = 0; i < DIRECTIONS + 1; i++)
		{
			for (int vc = 0; vc < GlobalParams::n_virtual_channels; vc++)
			{
				power.leakageBufferRouter();
				power.leakageLinkRouter2Router();
			}
		}

		power.leakageLinkRouter2Hub();
	}
}

vector<int> Router::nextDeltaHops(RouteData rd)
{

	if (GlobalParams::topology == TOPOLOGY_MESH)
	{
		cout << "Mesh topologies are not supported for nextDeltaHops() ";
		assert(false);
	}
	// annotate the initial nodes
	int src = rd.src_id;
	int dst = rd.dst_id;

	int current_node = src;
	vector<int> direction; // initially is empty
	vector<int> next_hops;

	int sw = GlobalParams::n_delta_tiles / 2; // sw: switch number in each stage
	int stg = log2(GlobalParams::n_delta_tiles);
	int c;
	//---From Source to stage 0 (return the sw attached to the source)---
	// Topology omega
	if (GlobalParams::topology == TOPOLOGY_OMEGA)
	{
		if (current_node < (GlobalParams::n_delta_tiles / 2))
			c = current_node;
		else if (current_node >= (GlobalParams::n_delta_tiles / 2))
			c = (current_node - (GlobalParams::n_delta_tiles / 2));
	}
	// Other delta topologies: Butterfly and baseline
	else if ((GlobalParams::topology == TOPOLOGY_BUTTERFLY) || (GlobalParams::topology == TOPOLOGY_BASELINE))
	{
		c = (current_node >> 1);
	}

	Coord temp_coord;
	temp_coord.x = 0;
	temp_coord.y = c;
	int N = coord2Id(temp_coord);

	next_hops.push_back(N);
	current_node = N;

	//---From stage 0 to Destination---
	int current_stage = 0;

	while (current_stage < stg - 1)
	{
		Coord new_coord;
		int y = id2Coord(current_node).y;

		rd.current_id = current_node;
		direction = routingAlgorithm->route(this, rd);

		int bit_to_check = stg - current_stage - 1;

		int bit_checked = (y & (1 << (bit_to_check - 1))) > 0 ? 1 : 0;

		// computes next node coords
		new_coord.x = current_stage + 1;
		if (bit_checked ^ direction[0])
			new_coord.y = toggleKthBit(y, bit_to_check);
		else
			new_coord.y = y;

		current_node = coord2Id(new_coord);
		next_hops.push_back(current_node);
		current_stage = id2Coord(current_node).x;
	}

	next_hops.push_back(dst);

	return next_hops;
}

vector<int> Router::routingFunction(const RouteData &route_data)
{
	if (GlobalParams::use_winoc)
	{
		// - If the current node C and the destination D are connected to an radiohub, use wireless
		// - If D is not directly connected to a radio hub, wireless
		// communication can still  be used if some intermediate node "I" in the routing
		// path is reachable from current node C.
		// - Since further wired hops will be required from I -> D, a threshold "winoc_dst_hops"
		// can be specified (via command line) to determine the max distance from the intermediate
		// node I and the destination D.
		// - NOTE: default threshold is 0, which means I=D, i.e., we explicitly ask the destination D to be connected to the
		// target radio hub
		if (hasRadioHub(local_id))
		{
			// Check if destination is directly connected to an hub
			if (hasRadioHub(route_data.dst_id) &&
				!sameRadioHub(local_id, route_data.dst_id))
			{
				map<int, int>::iterator it1 = GlobalParams::hub_for_tile.find(route_data.dst_id);
				map<int, int>::iterator it2 = GlobalParams::hub_for_tile.find(route_data.current_id);

				if (connectedHubs(it1->second, it2->second))
				{
					LOG << "Destination node " << route_data.dst_id << " is directly connected to a reachable RadioHub" << endl;
					vector<int> dirv;
					dirv.push_back(DIRECTION_HUB);
					return dirv;
				}
			}
			// let's check whether some node in the route has an acceptable distance to the dst
			if (GlobalParams::winoc_dst_hops > 0)
			{
				// TODO: for the moment, just print the set of nexts hops to check everything is ok
				LOG << "NEXT_DELTA_HOPS (from node " << route_data.src_id << " to " << route_data.dst_id << ") >>>> :";
				vector<int> nexthops;
				nexthops = nextDeltaHops(route_data);
				// for (int i=0;i<nexthops.size();i++) cout << "(" << nexthops[i] <<")-->";
				// cout << endl;
				for (int i = 1; i <= GlobalParams::winoc_dst_hops; i++)
				{
					int dest_position = nexthops.size() - 1;
					int candidate_hop = nexthops[dest_position - i];
					if (hasRadioHub(candidate_hop) && !sameRadioHub(local_id, candidate_hop))
					{
						// LOG << "Checking candidate hop " << candidate_hop << " ... It's OK!" << endl;
						LOG << "Relaying to hub-connected node " << candidate_hop << " to reach destination " << route_data.dst_id << endl;
						vector<int> dirv;
						dirv.push_back(DIRECTION_HUB_RELAY + candidate_hop);
						return dirv;
					}
					// else
					//  LOG << "Checking candidate hop " << candidate_hop << " ... NOT OK" << endl;
				}
			}
		}
	}
	// TODO: fix all the deprecated verbose mode logs
	if (GlobalParams::verbose_mode > VERBOSE_OFF)
		LOG << "Wired routing for dst = " << route_data.dst_id << endl;

	// not wireless direction taken, apply normal routing
	return routingAlgorithm->route(this, route_data);
}

int Router::route(const RouteData &route_data)
{

	if (route_data.dst_id == local_id)
		return DIRECTION_LOCAL;

	power.routing();
	vector<int> candidate_channels = routingFunction(route_data);

	power.selection();
	return selectionFunction(candidate_channels, route_data);
}

void Router::NoP_report() const
{
	NoP_data NoP_tmp;
	LOG << "NoP report: " << endl;

	for (int i = 0; i < DIRECTIONS; i++)
	{
		NoP_tmp = NoP_data_in[i].read();
		if (NoP_tmp.sender_id != NOT_VALID)
			cout << NoP_tmp;
	}
}

//---------------------------------------------------------------------------

int Router::NoPScore(const NoP_data &nop_data,
					 const vector<int> &nop_channels) const
{
	int score = 0;

	for (unsigned int i = 0; i < nop_channels.size(); i++)
	{
		int available;

		if (nop_data.channel_status_neighbor[nop_channels[i]].available)
			available = 1;
		else
			available = 0;

		int free_slots =
			nop_data.channel_status_neighbor[nop_channels[i]].free_slots;

		score += available * free_slots;
	}

	return score;
}

int Router::selectionFunction(const vector<int> &directions,
							  const RouteData &route_data)
{
	// not so elegant but fast escape ;)
	if (directions.size() == 1)
		return directions[0]; 

	return selectionStrategy->apply(this, directions, route_data);
}

void Router::configure(const int _id,
					   const double _warm_up_time,
					   const unsigned int _max_buffer_size,
					   GlobalRoutingTable &grt)
{
	local_id = _id;
	stats.configure(_id, _warm_up_time);

	start_from_port = DIRECTION_LOCAL;

	if (grt.isValid())
		routing_table.configure(grt, _id);

	reservation_table.setSize(DIRECTIONS + 2);

	for (int i = 0; i < DIRECTIONS + 2; i++)
	{
		for (int vc = 0; vc < GlobalParams::n_virtual_channels; vc++)
		{
			buffer[i][vc].SetMaxBufferSize(_max_buffer_size);
			buffer[i][vc].setLabel(string(name()) + "->buffer[" + i_to_string(i) + "]");
		}
		start_from_vc[i] = 0;
	}

	if (GlobalParams::topology == TOPOLOGY_MESH)
	{
		int row = _id / GlobalParams::mesh_dim_x;
		int col = _id % GlobalParams::mesh_dim_x;

		for (int vc = 0; vc < GlobalParams::n_virtual_channels; vc++)
		{
			if (row == 0)
				buffer[DIRECTION_NORTH][vc].Disable();
			if (row == GlobalParams::mesh_dim_y - 1)
				buffer[DIRECTION_SOUTH][vc].Disable();
			if (col == 0)
				buffer[DIRECTION_WEST][vc].Disable();
			if (col == GlobalParams::mesh_dim_x - 1)
				buffer[DIRECTION_EAST][vc].Disable();
		}
	}
}

unsigned long Router::getRoutedFlits()
{
	return routed_flits;
}

int Router::reflexDirection(int direction) const
{
	if (direction == DIRECTION_NORTH)
		return DIRECTION_SOUTH;
	if (direction == DIRECTION_EAST)
		return DIRECTION_WEST;
	if (direction == DIRECTION_WEST)
		return DIRECTION_EAST;
	if (direction == DIRECTION_SOUTH)
		return DIRECTION_NORTH;

	// you shouldn't be here
	assert(false);
	return NOT_VALID;
}

int Router::getNeighborId(int _id, int direction) const
{
	assert(GlobalParams::topology == TOPOLOGY_MESH);

	Coord my_coord = id2Coord(_id);

	switch (direction)
	{
	case DIRECTION_NORTH:
		if (my_coord.y == 0)
			return NOT_VALID;
		my_coord.y--;
		break;
	case DIRECTION_SOUTH:
		if (my_coord.y == GlobalParams::mesh_dim_y - 1)
			return NOT_VALID;
		my_coord.y++;
		break;
	case DIRECTION_EAST:
		if (my_coord.x == GlobalParams::mesh_dim_x - 1)
			return NOT_VALID;
		my_coord.x++;
		break;
	case DIRECTION_WEST:
		if (my_coord.x == 0)
			return NOT_VALID;
		my_coord.x--;
		break;
	default:
		LOG << "Direction not valid : " << direction;
		assert(false);
	}

	int neighbor_id = coord2Id(my_coord);

	return neighbor_id;
}

bool Router::inCongestion()
{
	for (int i = 0; i < DIRECTIONS; i++)
	{

		if (free_slots_neighbor[i] == NOT_VALID)
			continue;

		int flits = GlobalParams::buffer_depth - free_slots_neighbor[i];
		if (flits > (int)(GlobalParams::buffer_depth * GlobalParams::dyad_threshold))
			return true;
	}

	return false;
}

void Router::ShowBuffersStats(std::ostream &out)
{
	for (int i = 0; i < DIRECTIONS + 2; i++)
		for (int vc = 0; vc < GlobalParams::n_virtual_channels; vc++)
			buffer[i][vc].ShowStats(out);
}

bool Router::connectedHubs(int src_hub, int dst_hub)
{
	vector<int> &first = GlobalParams::hub_configuration[src_hub].txChannels;
	vector<int> &second = GlobalParams::hub_configuration[dst_hub].rxChannels;

	vector<int> intersection;

	for (unsigned int i = 0; i < first.size(); i++)
	{
		for (unsigned int j = 0; j < second.size(); j++)
		{
			if (first[i] == second[j])
				intersection.push_back(first[i]);
		}
	}

	if (intersection.size() == 0)
		return false;
	else
		return true;
}
