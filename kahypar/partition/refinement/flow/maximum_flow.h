/*******************************************************************************
 * This file is part of KaHyPar.
 *
 * Copyright (C) 2018 Sebastian Schlag <sebastian.schlag@kit.edu>
 * Copyright (C) 2018 Tobias Heuer <tobias.heuer@live.com>
 *
 * KaHyPar is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * KaHyPar is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with KaHyPar.  If not, see <http://www.gnu.org/licenses/>.
 *
******************************************************************************/

#pragma once

#include <algorithm>
#include <queue>
#include <unordered_map>
#include <utility>
#include <vector>

#include "kahypar/datastructure/fast_reset_array.h"
#include "kahypar/datastructure/fast_reset_flag_array.h"
#include "kahypar/datastructure/sparse_set.h"
#include "kahypar/definitions.h"
#include "kahypar/meta/mandatory.h"
#include "kahypar/meta/typelist.h"
#include "kahypar/partition/context.h"
#include "kahypar/partition/metrics.h"
#include "kahypar/partition/refinement/flow/external_flow/bk/graph.h"
#include "kahypar/partition/refinement/flow/external_flow/ibfs/ibfs.h"
#include "kahypar/partition/refinement/flow/flow_network.h"
#include "kahypar/partition/refinement/flow/most_balanced_minimum_cut.h"
#include "kahypar/utils/randomize.h"

namespace kahypar {
template <class Network = Mandatory>
class MaximumFlow {
 public:
  MaximumFlow(Hypergraph& hypergraph, const Context& context, Network& flow_network) :
    _hg(hypergraph),
    _context(context),
    _flowNetwork(flow_network),
    _parent(flow_network.initialSize(), nullptr),
    _visited(flow_network.initialSize()),
    _Q(),
    _mbmc(hypergraph, _context, flow_network),
    _originalPartID(_hg.initialNumNodes(), 0) { }

  virtual ~MaximumFlow() { }

  MaximumFlow(const MaximumFlow&) = delete;
  MaximumFlow& operator= (const MaximumFlow&) = delete;

  MaximumFlow(MaximumFlow&&) = delete;
  MaximumFlow& operator= (MaximumFlow&&) = delete;


  virtual Flow maximumFlow() = 0;

  HyperedgeWeight minimumSTCut(const PartitionID block_0, const PartitionID block_1) {
    if (_flowNetwork.isTrivialFlow()) {
      return INFTY;
    }

    PartitionID defaultPart = _context.local_search.flow.use_most_balanced_minimum_cut ? block_0 : block_1;
    for (const HypernodeID& hn : _flowNetwork.hypernodes()) {
      _originalPartID[hn] = _hg.partID(hn);
      moveHypernode(hn, defaultPart);
    }

    HighResClockTimepoint start = std::chrono::high_resolution_clock::now();
    HyperedgeWeight cut = maximumFlow();
    HighResClockTimepoint end = std::chrono::high_resolution_clock::now();
    _context.stats.add(StatTag::LocalSearch, "MaxFlow",
                       std::chrono::duration<double>(end - start).count());

    start = std::chrono::high_resolution_clock::now();
    if (_context.local_search.flow.use_most_balanced_minimum_cut) {
      _mbmc.mostBalancedMinimumCut(block_0, block_1);
    } else {
      bfs<true>(block_0);
    }
    end = std::chrono::high_resolution_clock::now();
    _context.stats.add(StatTag::LocalSearch, "MinCut",
                       std::chrono::duration<double>(end - start).count());

    return cut;
  }

  void rollback(bool storePartID = false) {
    for (const HypernodeID& hn : _flowNetwork.hypernodes()) {
      PartitionID from = _hg.partID(hn);
      moveHypernode(hn, _originalPartID[hn]);
      if (storePartID) _originalPartID[hn] = from;
    }
  }

  PartitionID getOriginalPartition(const HypernodeID hn) const {
    return _originalPartID[hn];
  }

  template <bool assignHypernodes = false>
  bool bfs(const PartitionID block = 0) {
    bool augmentingPathExists = false;
    _parent.resetUsedEntries();
    _visited.reset();
    while (!_Q.empty()) {
      _Q.pop();
    }

    // Initialize queue with all source nodes
    for (const NodeID& s : _flowNetwork.sources()) {
      _visited.set(s, true);
      _parent.set(s, nullptr);
      _Q.push(s);
    }

    while (!_Q.empty()) {
      NodeID u = _Q.front();
      _Q.pop();

      if (assignHypernodes) {
        if (_flowNetwork.interpreteHypernode(u)) {
          moveHypernode(u, block);
        } else if (_flowNetwork.interpreteHyperedge(u)) {
          HyperedgeID he = _flowNetwork.mapToHyperedgeID(u);
          for (const HypernodeID& pin : _hg.pins(he)) {
            if (_flowNetwork.containsHypernode(pin)) {
              moveHypernode(pin, block);
            }
          }
        }
      }

      if (_flowNetwork.isSink(u)) {
        augmentingPathExists = true;
        continue;
      }

      for (FlowEdge& e : _flowNetwork.incidentEdges(u)) {
        NodeID v = e.target;
        if (!_visited[v] && _flowNetwork.residualCapacity(e)) {
          _parent.set(v, &e);
          _visited.set(v, true);
          _Q.push(v);
        }
      }
    }
    return augmentingPathExists;
  }

 protected:
  template <typename T>
  FRIEND_TEST(AMaximumFlow, ChecksIfAugmentingPathExist);
  template <typename T>
  FRIEND_TEST(AMaximumFlow, AugmentAlongPath);

  Flow augment(NodeID cur, Flow minFlow = INFTY) {
    if (_flowNetwork.isSource(cur) || minFlow == 0) {
      return minFlow;
    } else {
      FlowEdge* e = _parent.get(cur);
      Flow f = augment(e->source, std::min(minFlow, _flowNetwork.residualCapacity(*e)));

      ASSERT([&]() {
          Flow residualForwardBefore = _flowNetwork.residualCapacity(*e);
          Flow residualBackwardBefore = _flowNetwork.residualCapacity(_flowNetwork.reverseEdge(*e));
          _flowNetwork.increaseFlow(*e, f);
          Flow residualForwardAfter = _flowNetwork.residualCapacity(*e);
          Flow residualBackwardAfter = _flowNetwork.residualCapacity(_flowNetwork.reverseEdge(*e));
          if (residualForwardBefore != INFTY && residualForwardBefore != residualForwardAfter + f) {
            LOG << "Residual capacity should be " << (residualForwardBefore - f) << "!";
            return false;
          }
          if (residualBackwardBefore != INFTY && residualBackwardBefore != residualBackwardAfter - f) {
            LOG << "Residual capacity should be " << (residualBackwardBefore + f) << "!";
            return false;
          }
          _flowNetwork.increaseFlow(_flowNetwork.reverseEdge(*e), f);
          residualForwardAfter = _flowNetwork.residualCapacity(*e);
          residualBackwardAfter = _flowNetwork.residualCapacity(_flowNetwork.reverseEdge(*e));
          if (residualForwardBefore != residualForwardAfter ||
              residualBackwardBefore != residualBackwardAfter) {
            LOG << "Restoring original capacities failed!";
            return false;
          }
          return true;
        } (), "Flow is not increased correctly!");

      _flowNetwork.increaseFlow(*e, f);
      return f;
    }
  }

  void moveHypernode(HypernodeID hn, PartitionID to) {
    ASSERT(_hg.partID(hn) != -1, "Hypernode " << hn << " should be assigned to a part");
    PartitionID from = _hg.partID(hn);
    if (from != to) {
      _hg.changeNodePart(hn, from, to);
    }
  }

  Hypergraph& _hg;
  const Context& _context;
  Network& _flowNetwork;

  // Datastructure for BFS
  FastResetArray<FlowEdge*> _parent;
  FastResetFlagArray<> _visited;
  std::queue<NodeID> _Q;

  MostBalancedMinimumCut<Network> _mbmc;

  std::vector<PartitionID> _originalPartID;
};

template <class Network = Mandatory>
class EdmondKarp : public MaximumFlow<Network>{
  using Base = MaximumFlow<Network>;

 public:
  EdmondKarp(Hypergraph& hypergraph, const Context& context, Network& flow_network) :
    Base(hypergraph, context, flow_network) { }

  ~EdmondKarp() = default;

  EdmondKarp(const EdmondKarp&) = delete;
  EdmondKarp& operator= (const EdmondKarp&) = delete;

  EdmondKarp(EdmondKarp&&) = delete;
  EdmondKarp& operator= (EdmondKarp&&) = delete;

  Flow maximumFlow() {
    Flow maxFlow = 0;
    while (Base::bfs()) {
      for (const NodeID& t : _flowNetwork.sinks()) {
        if (_parent.get(t) != nullptr) {
          maxFlow += Base::augment(t);
        }
      }
    }
    ASSERT(!Base::bfs(), "Found augmenting path after flow computation finished!");
    return maxFlow;
  }

 private:
  template <typename T>
  FRIEND_TEST(AMaximumFlow, ChecksIfAugmentingPathExist);
  template <typename T>
  FRIEND_TEST(AMaximumFlow, AugmentAlongPath);

  using Base::_hg;
  using Base::_context;
  using Base::_flowNetwork;
  using Base::_parent;
};


template <class Network = Mandatory>
class GoldbergTarjan : public MaximumFlow<Network>{
  using Base = MaximumFlow<Network>;

  using ConstIncidenceIterator = std::vector<FlowEdge>::const_iterator;
  using IncidenceIterator = std::vector<FlowEdge>::iterator;

 public:
  GoldbergTarjan(Hypergraph& hypergraph, const Context& context, Network& flow_network) :
    Base(hypergraph, context, flow_network),
    _num_nodes(0),
    _excess(flow_network.initialSize(), 0),
    _distance(flow_network.initialSize(), 0),
    _count(flow_network.initialSize(), 0),
    _active(flow_network.initialSize()),
    _edgeIterator(flow_network.initialSize()),
    _q(),
    _work(0) { }

  ~GoldbergTarjan() = default;

  GoldbergTarjan(const GoldbergTarjan&) = delete;
  GoldbergTarjan& operator= (const GoldbergTarjan&) = delete;

  GoldbergTarjan(GoldbergTarjan&&) = delete;
  GoldbergTarjan& operator= (GoldbergTarjan&&) = delete;


  Flow maximumFlow() {
    _num_nodes = _flowNetwork.numNodes() + 2;
    init();
    global_relabeling();

    Flow maxFlow = 0;
    while (!_q.empty()) {
      NodeID cur = _q.front();
      _active.set(cur, false);
      _q.pop();

      if (_flowNetwork.isSource(cur) && _distance.get(cur) == _num_nodes + 1) {
        _excess.set(cur, 0);
      } else if (_flowNetwork.isSink(cur) && _distance.get(cur) == 1) {
        maxFlow += _excess.get(cur);
        _excess.set(cur, 0);
      } else {
        discharge(cur);
      }

      _work++;
      if (_work > _num_nodes) {
        global_relabeling();
        _work = 0;
      }
    }

    ASSERT([&]() {
        for (const NodeID& node : _flowNetwork.nodes()) {
          if (_excess.get(node) > 0) {
            return false;
          }
        }
        return true;
      } (), "After maximum flow execution no node should have a remaining excess!");

    ASSERT(!Base::bfs(), "Found augmenting path after flow computation finished!");

    return maxFlow;
  }

 protected:
  template <typename T>
  FRIEND_TEST(AMaximumFlow, ChecksIfAugmentingPathExist);
  template <typename T>
  FRIEND_TEST(AMaximumFlow, AugmentAlongPath);

  void init() {
    _visited.reset();
    _excess.resetUsedEntries();
    _distance.resetUsedEntries();
    _count.resetUsedEntries();
    _active.reset();
    while (!_q.empty()) {
      _q.pop();
    }

    for (const NodeID& node : _flowNetwork.nodes()) {
      _edgeIterator[node] = _flowNetwork.incidentEdges(node);
    }

    _count.set(0, _num_nodes - 1);
    const Flow initialInfinity = _flowNetwork.totalWeightHyperedges();
    for (const NodeID& s : _flowNetwork.sources()) {
      _excess.set(s, initialInfinity);
      if (_flowNetwork.isHypernode(s)) {
        _excess.set(s, 0);
        updateDistance(s, _num_nodes + 1);
        for (FlowEdge& e : _flowNetwork.incidentEdges(s)) {
          NodeID target = e.target;
          if (_flowNetwork.residualCapacity(e)) {
            Flow initialPush = std::min(initialInfinity, _flowNetwork.residualCapacity(e));
            _excess.update(target, initialPush);
            _flowNetwork.increaseFlow(e, initialPush);
            enqueue(target);
          }
        }
      }
      enqueue(s);
    }
  }

  void push(FlowEdge& e) {
    NodeID u = e.source;
    NodeID v = e.target;
    ASSERT(_excess.get(u) > 0, "There is no flow, which can be pushed over edge (" << u << "," << v << ")!");

    Flow delta = std::min(_excess.get(u), _flowNetwork.residualCapacity(e));

    if (_distance.get(u) != _distance.get(v) + 1 || delta == 0) {
      return;
    }

    _excess.update(u, -delta);
    _excess.update(v, delta);
    _flowNetwork.increaseFlow(e, delta);

    enqueue(v);
    ASSERT(_flowNetwork.residualCapacity(_flowNetwork.reverseEdge(e)),
           "Node " << v << " should be active and residual capacity of edge (" << u << "," << v << ")"
                   << " should be greater than zero!");
  }

  void gap_heurisitc(const NodeID distance) {
    for (const NodeID& node : _flowNetwork.nodes()) {
      NodeID node_dist = _distance.get(node);
      if (node_dist < distance || node_dist >= _num_nodes) continue;
      updateDistance(node, _num_nodes);
      enqueue(node);
    }
  }

  void global_relabeling() {
    ASSERT(_Q.empty(), "BFS queue is not empty!");
    _visited.reset();

    for (const NodeID& t : _flowNetwork.sinks()) {
      updateDistance(t, 1);
      _visited.set(t, true);
      _Q.push(t);
    }

    while (!_Q.empty()) {
      NodeID node = _Q.front();
      _Q.pop();
      _edgeIterator[node] = _flowNetwork.incidentEdges(node);

      for (FlowEdge& e : _flowNetwork.incidentEdges(node)) {
        NodeID target = e.target;
        if (!_visited[target] && _flowNetwork.residualCapacity(_flowNetwork.reverseEdge(e)) &&
            !_flowNetwork.isSource(target)) {
          updateDistance(target, _distance.get(node) + 1);
          _visited.set(target, true);
          _Q.push(target);
        }
      }
    }

    _visited.reset();
  }

  inline void updateDistance(const NodeID u, const NodeID value) {
    NodeID old_value = _distance.get(u);
    if (old_value < _num_nodes) {
      _count.update(old_value, -1);
    }
    if (value < _num_nodes) {
      _count.update(value, 1);
    }
    _distance.set(u, value);
  }

  void relabel(const NodeID u) {
    if (_flowNetwork.isSink(u)) {
      updateDistance(u, 1);
    } else {
      NodeID label = _flowNetwork.isSource(u) ? _num_nodes : INVALID_NODE;
      for (FlowEdge& e : _flowNetwork.incidentEdges(u)) {
        NodeID v = e.target;
        ASSERT(!_visited[v], "Node " << v << " should not be visited!");
        if (_flowNetwork.residualCapacity(e)) {
          label = std::min(label, _distance.get(v));
        }
      }
      updateDistance(u, label + 1);
    }
  }

  void enqueue(const NodeID u) {
    if (_active[u]) return;
    if (_excess.get(u) > 0) {
      _active.set(u, true);
      _q.push(u);
    }
  }

  void discharge(const NodeID u) {
    while (_excess.get(u) > 0) {
      for ( ; _edgeIterator[u].first != _edgeIterator[u].second; ++(_edgeIterator[u].first)) {
        FlowEdge& e = *(_edgeIterator[u].first);
        ASSERT(!_visited[e.target], "Node " << e.target << " should not be visited!");
        if (_flowNetwork.residualCapacity(e)) {
          push(e);
        }
        if (_excess.get(u) == 0) break;
      }

      if (_edgeIterator[u].first == _edgeIterator[u].second) {
        NodeID cur_dist = _distance.get(u);
        if (cur_dist < _num_nodes && _count.get(cur_dist) == 1) {
          gap_heurisitc(cur_dist);
        } else {
          relabel(u);
        }
        _edgeIterator[u] = _flowNetwork.incidentEdges(u);
      }

      if (_flowNetwork.isSource(u) && _distance.get(u) == _num_nodes + 1) {
        _excess.set(u, 0);
      }
    }
  }

  using Base::_hg;
  using Base::_context;
  using Base::_flowNetwork;
  using Base::_visited;
  using Base::_Q;

  size_t _num_nodes;
  FastResetArray<Flow> _excess;
  FastResetArray<NodeID> _distance;
  FastResetArray<int> _count;
  FastResetFlagArray<> _active;
  std::vector<std::pair<IncidenceIterator, IncidenceIterator> > _edgeIterator;
  std::queue<NodeID> _q;
  size_t _work;
};


template <class Network = Mandatory>
class BoykovKolmogorov : public MaximumFlow<Network>{
  using Base = MaximumFlow<Network>;
  using FlowGraph = maxflow::Graph<int, int, int>;

 public:
  BoykovKolmogorov(Hypergraph& hypergraph, const Context& context, Network& flow_network) :
    Base(hypergraph, context, flow_network),
    _flowGraph(hypergraph.initialNumNodes() + 2 * hypergraph.initialNumEdges(),
               hypergraph.initialNumNodes() + 2 * hypergraph.initialNumEdges()),
    _flowNetworkMapping(hypergraph.initialNumNodes() + 2 * hypergraph.initialNumEdges(), 0) { }

  ~BoykovKolmogorov() = default;

  BoykovKolmogorov(const BoykovKolmogorov&) = delete;
  BoykovKolmogorov& operator= (const BoykovKolmogorov&) = delete;

  BoykovKolmogorov(BoykovKolmogorov&&) = delete;
  BoykovKolmogorov& operator= (BoykovKolmogorov&&) = delete;

  Flow maximumFlow() {
    mapToExternalFlowNetwork();

    Flow maxFlow = _flowGraph.maxflow();

    FlowGraph::arc* a = _flowGraph.get_first_arc();
    while (a != _flowGraph.arc_last) {
      Flow flow = a->flowEdge->capacity - _flowGraph.get_rcap(a);
      if (flow != 0) a->flowEdge->increaseFlow(flow);
      a = _flowGraph.get_next_arc(a);
    }

    ASSERT(!Base::bfs(), "Found augmenting path after flow computation finished!");
    return maxFlow;
  }

 private:
  void mapToExternalFlowNetwork() {
    _flowGraph.reset();
    _visited.reset();
    const Flow infty = _flowNetwork.totalWeightHyperedges();

    for (const NodeID& node : _flowNetwork.nodes()) {
      NodeID id = _flowGraph.add_node();
      _flowNetworkMapping[node] = id;
      if (_flowNetwork.isSource(node)) {
        _flowGraph.add_tweights(id, infty, 0);
      }
      if (_flowNetwork.isSink(node)) {
        _flowGraph.add_tweights(id, 0, infty);
      }
    }

    for (const NodeID node : _flowNetwork.nodes()) {
      NodeID u = _flowNetworkMapping[node];
      for (FlowEdge& edge : _flowNetwork.incidentEdges(node)) {
        NodeID v = _flowNetworkMapping[edge.target];
        Capacity c = edge.capacity;
        FlowEdge& rev_edge = _flowNetwork.reverseEdge(edge);
        Capacity rev_c = rev_edge.capacity;
        if (!_visited[edge.target]) {
          FlowGraph::arc* a = _flowGraph.add_edge(u, v, c, rev_c);
          a->flowEdge = &edge;
          a->sister->flowEdge = &rev_edge;
        }
      }
      _visited.set(node, true);
    }
  }

  using Base::_hg;
  using Base::_context;
  using Base::_flowNetwork;
  using Base::_parent;
  using Base::_visited;

  FlowGraph _flowGraph;
  std::vector<NodeID> _flowNetworkMapping;
};


template <class Network = Mandatory>
class IBFS : public MaximumFlow<Network>{
  using Base = MaximumFlow<Network>;
  using FlowGraph = ibfs::IBFSGraph;

 public:
  IBFS(Hypergraph& hypergraph, const Context& context, Network& flow_network) :
    Base(hypergraph, context, flow_network),
    _flowGraph(FlowGraph::IB_INIT_COMPACT),
    _flowNetworkMapping(hypergraph.initialNumNodes() + 2 * hypergraph.initialNumEdges(), 0) { }

  ~IBFS() = default;

  IBFS(const IBFS&) = delete;
  IBFS& operator= (const IBFS&) = delete;

  IBFS(IBFS&&) = delete;
  IBFS& operator= (IBFS&&) = delete;

  Flow maximumFlow() {
    mapToExternalFlowNetwork();

    _flowGraph.computeMaxFlow();
    Flow maxFlow = _flowGraph.getFlow();

    FlowGraph::Arc* a = _flowGraph.arcs;
    while (a != _flowGraph.arcEnd) {
      Flow flow = a->flowEdge->capacity - a->rCap;
      if (flow != 0) a->flowEdge->increaseFlow(flow);
      a++;
    }

    ASSERT(!Base::bfs(), "Found augmenting path after flow computation finished!");
    return maxFlow;
  }

 private:

  void mapToExternalFlowNetwork() {
    _flowGraph.initSize(_flowNetwork.numNodes(), _flowNetwork.numEdges() - _flowNetwork.numUndirectedEdges());
    _visited.reset();
    const Flow infty = _flowNetwork.totalWeightHyperedges();
    NodeID cur_id = 0;

    for (const NodeID& node : _flowNetwork.nodes()) {
      _flowGraph.addNode(cur_id,
                         _flowNetwork.isSource(node) ? infty : 0,
                         _flowNetwork.isSink(node) ? infty : 0);
      _flowNetworkMapping[node] = cur_id;
      cur_id++;
    }

    for (const NodeID node : _flowNetwork.nodes()) {
      NodeID u = _flowNetworkMapping[node];
      for (FlowEdge& edge : _flowNetwork.incidentEdges(node)) {
        NodeID v = _flowNetworkMapping[edge.target];
        Capacity c = edge.capacity;
        FlowEdge& rev_edge = _flowNetwork.reverseEdge(edge);
        Capacity rev_c = rev_edge.capacity;
        if (!_visited[edge.target]) {
          _flowGraph.addEdge(u, v, c, rev_c, &edge, &rev_edge);
        }
      }
      _visited.set(node, true);
    }

    _flowGraph.initGraph();
  }

  using Base::_hg;
  using Base::_context;
  using Base::_flowNetwork;
  using Base::_parent;
  using Base::_visited;

  FlowGraph _flowGraph;
  std::vector<NodeID> _flowNetworkMapping;
};



}  // namespace kahypar