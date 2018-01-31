/*******************************************************************************
 * This file is part of KaHyPar.
 *
 * Copyright (C) 2017 Tobias Heuer <tobias.heuer@live.com>
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

#include <set>
#include <vector>

#include "gmock/gmock.h"

#include "kahypar/definitions.h"
#include "kahypar/partition/metrics.h"
#include "kahypar/partition/refinement/flow/flow_network.h"

using::testing::Test;
using::testing::Eq;

namespace kahypar {
#define INCOMING(X) flowNetwork.mapToIncommingHyperedgeID(X)
#define OUTGOING(X) flowNetwork.mapToOutgoingHyperedgeID(X)
#define EDGE(X, C) std::make_pair(X, C)

typedef std::pair<NodeID, Capacity> edge;

template <class Network = Mandatory>
class FlowNetworkTest : public::testing::TestWithParam<std::pair<NodeID, std::set<edge> > >{
 public:
  FlowNetworkTest() :
    context(),
    hypergraph(10, 7, HyperedgeIndexVector { 0, 5, 7, 9, 11, 14, 16, 18 },
               HyperedgeVector { 0, 1, 2, 3, 4, 4, 5, 5, 6, 5, 7, 5, 6, 7, 6, 8, 7, 9 }),
    flowNetwork(hypergraph, context) { }

  void setupFlowNetwork() {
    std::vector<HypernodeID> block0 = { 0, 2, 4, 9 };
    std::vector<HypernodeID> block1 = { 1, 3, 5, 6, 7, 8 };
    for (const HypernodeID& hn : block0) {
      hypergraph.setNodePart(hn, 0);
    }
    for (const HypernodeID& hn : block1) {
      hypergraph.setNodePart(hn, 1);
    }
    for (HypernodeID node = 2; node <= 7; ++node) {
      flowNetwork.addHypernode(node);
    }
    flowNetwork.build(0, 1);
  }

  void testSourcesAndSinks(const std::set<NodeID>& sources, const std::set<NodeID>& sinks) {
    size_t num_sources = 0;
    size_t num_sinks = 0;
    for (const NodeID& node : flowNetwork.nodes()) {
      /*LOG << V(node) << V(flowNetwork.mapToHyperedgeID(node)) << V(flowNetwork.isSource(node)) << V(flowNetwork.isSink(node))
          << "(" << (sources.find(node) != sources.end()) << "," << (sinks.find(node) != sinks.end()) << ")";*/
      if (sources.find(node) != sources.end()) {
        ASSERT_TRUE(flowNetwork.isSource(node));
        num_sources++;
      } else {
        ASSERT_FALSE(flowNetwork.isSource(node));
      }
      if (sinks.find(node) != sinks.end()) {
        ASSERT_TRUE(flowNetwork.isSink(node));
        num_sinks++;
      } else {
        ASSERT_FALSE(flowNetwork.isSink(node));
      }
    }
    ASSERT_EQ(num_sources, sources.size());
    ASSERT_EQ(num_sinks, sinks.size());
  }

  void testIncidentEdges(const NodeID node, const std::set<edge> edges) {
    size_t num_edges = 0;
    for (const FlowEdge& e : flowNetwork.incidentEdges(node)) {
      if (flowNetwork.residualCapacity(e) > 0) {
        num_edges++;
        ASSERT_TRUE(edges.find(EDGE(e.target, e.capacity)) != edges.end());
      }
    }
    ASSERT_EQ(num_edges, edges.size());
  }


  Context context;
  Hypergraph hypergraph;
  Network flowNetwork;
};

// ###################### Lawler Flow Network Test ######################

using LawlerNetworkTest = FlowNetworkTest<LawlerNetwork>;

TEST_F(LawlerNetworkTest, NodesInFlowProblem) {
  setupFlowNetwork();
  std::set<NodeID> nodes = { 2, 3, 4, 5, 6, 7, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23 };
  size_t num_nodes = 0;
  for (const NodeID node : flowNetwork.nodes()) {
    ASSERT_TRUE(nodes.find(node) != nodes.end());
    num_nodes++;
  }
  ASSERT_EQ(num_nodes, nodes.size());
}

INSTANTIATE_TEST_CASE_P(LawlerNetworkStructure,
                        LawlerNetworkTest,
                        ::testing::Values(std::make_pair(2, std::set<edge>({ EDGE(10, INFTY) })),
                                          std::make_pair(4, std::set<edge>({ EDGE(10, INFTY),
                                                                             EDGE(11, INFTY) })),
                                          std::make_pair(5, std::set<edge>({ EDGE(11, INFTY),
                                                                             EDGE(12, INFTY),
                                                                             EDGE(13, INFTY),
                                                                             EDGE(14, INFTY) })),
                                          std::make_pair(10, std::set<edge>({ EDGE(17, 1) })),
                                          std::make_pair(17, std::set<edge>({ EDGE(2, INFTY),
                                                                              EDGE(3, INFTY),
                                                                              EDGE(4, INFTY) })),
                                          std::make_pair(14, std::set<edge>({ EDGE(21, 1) })),
                                          std::make_pair(21, std::set<edge>({ EDGE(5, INFTY),
                                                                              EDGE(6, INFTY),
                                                                              EDGE(7, INFTY) }))));

TEST_P(LawlerNetworkTest, IncidentEdgesOfANode) {
  setupFlowNetwork();
  NodeID node = GetParam().first;
  std::set<edge> incidentEdges = GetParam().second;
  testIncidentEdges(node, incidentEdges);
}

TEST_F(LawlerNetworkTest, SourceAndSinkSetup) {
  setupFlowNetwork();
  testSourcesAndSinks({ INCOMING(0), INCOMING(6) },
                      { OUTGOING(0), OUTGOING(5) });
}

// ###################### Node Degree Flow Network Test ######################

using HeuerNetworkTest = FlowNetworkTest<HeuerNetwork>;

TEST_F(HeuerNetworkTest, NodesInFlowProblem) {
  setupFlowNetwork();
  std::set<NodeID> nodes = { 5, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23 };
  size_t num_nodes = 0;
  for (const NodeID node : flowNetwork.nodes()) {
    ASSERT_TRUE(nodes.find(node) != nodes.end());
    num_nodes++;
  }
  ASSERT_EQ(num_nodes, nodes.size());
}

INSTANTIATE_TEST_CASE_P(NodeDegreeNetworkStructure,
                        HeuerNetworkTest,
                        ::testing::Values(std::make_pair(10, std::set<edge>({ EDGE(17, 1) })),
                                          std::make_pair(17, std::set<edge>({ EDGE(11, INFTY) })),
                                          std::make_pair(11, std::set<edge>({ EDGE(18, 1) })),
                                          std::make_pair(5, std::set<edge>({ EDGE(11, INFTY),
                                                                             EDGE(12, INFTY),
                                                                             EDGE(13, INFTY),
                                                                             EDGE(14, INFTY) })),
                                          std::make_pair(21, std::set<edge>({ EDGE(5, INFTY),
                                                                              EDGE(12, INFTY),
                                                                              EDGE(13, INFTY),
                                                                              EDGE(15, INFTY),
                                                                              EDGE(16, INFTY) })),
                                          std::make_pair(22, std::set<edge>({ EDGE(12, INFTY),
                                                                              EDGE(14, INFTY) }))));

TEST_P(HeuerNetworkTest, IncidentEdgesOfANode) {
  setupFlowNetwork();
  NodeID node = GetParam().first;
  std::set<edge> incidentEdges = GetParam().second;
  testIncidentEdges(node, incidentEdges);
}

TEST_F(HeuerNetworkTest, SourceAndSinkSetup) {
  setupFlowNetwork();
  testSourcesAndSinks({ INCOMING(0), INCOMING(6) },
                      { OUTGOING(0), OUTGOING(5) });
}

// ###################### Edge Size Flow Network Test ######################

using WongNetworkTest = FlowNetworkTest<WongNetwork>;

TEST_F(WongNetworkTest, NodesInFlowProblem) {
  setupFlowNetwork();
  std::set<NodeID> nodes = { 2, 3, 4, 5, 6, 7, 10, 14, 16, 17, 21, 22 };
  size_t num_nodes = 0;
  for (const NodeID node : flowNetwork.nodes()) {
    ASSERT_TRUE(nodes.find(node) != nodes.end());
    num_nodes++;
  }
  ASSERT_EQ(num_nodes, nodes.size());
}

INSTANTIATE_TEST_CASE_P(EdgeSizeNetworkStructure,
                        WongNetworkTest,
                        ::testing::Values(std::make_pair(4, std::set<edge>({ EDGE(5, 1),
                                                                             EDGE(10, INFTY) })),
                                          std::make_pair(5, std::set<edge>({ EDGE(4, 1),
                                                                             EDGE(6, 1),
                                                                             EDGE(7, 1),
                                                                             EDGE(14, INFTY) })),
                                          std::make_pair(16, std::set<edge>({ EDGE(7, 1) })),
                                          std::make_pair(17, std::set<edge>({ EDGE(2, INFTY),
                                                                              EDGE(3, INFTY),
                                                                              EDGE(4, INFTY) })),
                                          std::make_pair(21, std::set<edge>({ EDGE(5, INFTY),
                                                                              EDGE(6, INFTY),
                                                                              EDGE(7, INFTY) }))));

TEST_P(WongNetworkTest, IncidentEdgesOfANode) {
  setupFlowNetwork();
  NodeID node = GetParam().first;
  std::set<edge> incidentEdges = GetParam().second;
  testIncidentEdges(node, incidentEdges);
}

TEST_F(WongNetworkTest, SourceAndSinkSetup) {
  setupFlowNetwork();
  testSourcesAndSinks({ INCOMING(0), INCOMING(6) },
                      { OUTGOING(0), OUTGOING(5) });
}

// ###################### Hybrid Flow Network Test ######################

using HybridNetworkTest = FlowNetworkTest<HybridNetwork>;

TEST_F(HybridNetworkTest, NodesInFlowProblem) {
  setupFlowNetwork();
  std::set<NodeID> nodes = { 4, 5, 6, 7, 10, 14, 16, 17, 21, 22 };
  size_t num_nodes = 0;
  for (const NodeID node : flowNetwork.nodes()) {
    ASSERT_TRUE(nodes.find(node) != nodes.end());
    num_nodes++;
  }
  ASSERT_EQ(num_nodes, nodes.size());
}

INSTANTIATE_TEST_CASE_P(HybridNetworkStructure,
                        HybridNetworkTest,
                        ::testing::Values(std::make_pair(4, std::set<edge>({ EDGE(5, 1),
                                                                             EDGE(10, INFTY) })),
                                          std::make_pair(5, std::set<edge>({ EDGE(4, 1),
                                                                             EDGE(6, 1),
                                                                             EDGE(7, 1),
                                                                             EDGE(14, INFTY) })),
                                          std::make_pair(16, std::set<edge>({ EDGE(7, 1) })),
                                          std::make_pair(17, std::set<edge>({ EDGE(4, INFTY) })),
                                          std::make_pair(21, std::set<edge>({ EDGE(5, INFTY),
                                                                              EDGE(6, INFTY),
                                                                              EDGE(7, INFTY) }))));

TEST_P(HybridNetworkTest, IncidentEdgesOfANode) {
  setupFlowNetwork();
  NodeID node = GetParam().first;
  std::set<edge> incidentEdges = GetParam().second;
  testIncidentEdges(node, incidentEdges);
}

TEST_F(HybridNetworkTest, SourceAndSinkSetup) {
  setupFlowNetwork();
  testSourcesAndSinks({ INCOMING(0), INCOMING(6) },
                      { OUTGOING(0), OUTGOING(5) });
}
}  // namespace kahypar
