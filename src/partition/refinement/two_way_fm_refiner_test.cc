/***************************************************************************
 *  Copyright (C) 2014 Sebastian Schlag <sebastian.schlag@kit.edu>
 **************************************************************************/

#include "gmock/gmock.h"

#include "lib/definitions.h"
#include "partition/Metrics.h"
#include "partition/refinement/TwoWayFMRefiner.h"
#include "partition/refinement/policies/FMStopPolicies.h"

using::testing::Test;
using::testing::Eq;

using defs::Hypergraph;
using defs::HyperedgeIndexVector;
using defs::HyperedgeVector;
using defs::HyperedgeWeight;
using defs::HypernodeID;

namespace partition {
using TwoWayFMRefinerSimpleStopping = TwoWayFMRefiner<NumberOfFruitlessMovesStopsSearch>;

class ATwoWayFMRefiner : public Test {
 public:
  ATwoWayFMRefiner() :
    hypergraph(new Hypergraph(7, 4, HyperedgeIndexVector { 0, 2, 6, 9,  /*sentinel*/ 12 },
                              HyperedgeVector { 0, 2, 0, 1, 3, 4, 3, 4, 6, 2, 5, 6 })),
    config(),
    refiner(nullptr) {
    hypergraph->setNodePart(0, 0);
    hypergraph->setNodePart(1, 1);
    hypergraph->setNodePart(2, 1);
    hypergraph->setNodePart(3, 0);
    hypergraph->setNodePart(4, 0);
    hypergraph->setNodePart(5, 1);
    hypergraph->setNodePart(6, 1);
    config.two_way_fm.max_number_of_fruitless_moves = 50;
    refiner = std::make_unique<TwoWayFMRefinerSimpleStopping>(*hypergraph, config);
    refiner->initialize();
  }

  std::unique_ptr<Hypergraph> hypergraph;
  Configuration config;
  std::unique_ptr<TwoWayFMRefinerSimpleStopping> refiner;
};

class AGainUpdateMethod : public Test {
 public:
  AGainUpdateMethod() :
    config() {
    config.two_way_fm.max_number_of_fruitless_moves = 50;
  }

  Configuration config;
};

TEST_F(ATwoWayFMRefiner, IdentifiesBorderHypernodes) {
  ASSERT_THAT(refiner->isBorderNode(0), Eq(true));
  ASSERT_THAT(refiner->isBorderNode(1), Eq(true));
  ASSERT_THAT(refiner->isBorderNode(5), Eq(false));
}

TEST_F(ATwoWayFMRefiner, ComputesGainOfHypernodeMovement) {
  ASSERT_THAT(refiner->computeGain(6), Eq(0));
  ASSERT_THAT(refiner->computeGain(1), Eq(1));
  ASSERT_THAT(refiner->computeGain(5), Eq(-1));
}

TEST_F(ATwoWayFMRefiner, ActivatesBorderNodes) {
  refiner->activate(1,  /* dummy max-part-weight */ 42);
  HypernodeID hn;
  HyperedgeWeight gain;
  PartitionID to_part;
  refiner->_pq.deleteMax(hn, gain, to_part);
  ASSERT_THAT(hn, Eq(1));
  ASSERT_THAT(gain, Eq(1));
}

TEST_F(ATwoWayFMRefiner, CalculatesNodeCountsInBothPartitions) {
  ASSERT_THAT(refiner->_hg.partWeight(0), Eq(3));
  ASSERT_THAT(refiner->_hg.partWeight(1), Eq(4));
}

TEST_F(ATwoWayFMRefiner, DoesNotViolateTheBalanceConstraint) {
  double old_imbalance = metrics::imbalance(*hypergraph);
  HyperedgeWeight old_cut = metrics::hyperedgeCut(*hypergraph);
  std::vector<HypernodeID> refinement_nodes = { 1, 6 };

  config.partition.epsilon = 0.15;
  refiner->refine(refinement_nodes, 2, 42, old_cut, old_imbalance);

  EXPECT_PRED_FORMAT2(::testing::DoubleLE, metrics::imbalance(*hypergraph),
                      old_imbalance);
}


TEST_F(ATwoWayFMRefiner, UpdatesNodeCountsOnNodeMovements) {
  ASSERT_THAT(refiner->_hg.partWeight(0), Eq(3));
  ASSERT_THAT(refiner->_hg.partWeight(1), Eq(4));

  refiner->moveHypernode(1, 1, 0);

  ASSERT_THAT(refiner->_hg.partWeight(0), Eq(4));
  ASSERT_THAT(refiner->_hg.partWeight(1), Eq(3));
}

TEST_F(ATwoWayFMRefiner, UpdatesPartitionWeightsOnRollBack) {
  ASSERT_THAT(refiner->_hg.partWeight(0), Eq(3));
  ASSERT_THAT(refiner->_hg.partWeight(1), Eq(4));
  double old_imbalance = metrics::imbalance(*hypergraph);
  HyperedgeWeight old_cut = metrics::hyperedgeCut(*hypergraph);
  std::vector<HypernodeID> refinement_nodes = { 1, 6 };

  config.partition.epsilon = 0.15;
  refiner->refine(refinement_nodes, 2, 42, old_cut, old_imbalance);

  ASSERT_THAT(refiner->_hg.partWeight(0), Eq(4));
  ASSERT_THAT(refiner->_hg.partWeight(1), Eq(3));
}

TEST_F(ATwoWayFMRefiner, PerformsCompleteRollBackIfNoImprovementCouldBeFound) {
  hypergraph->changeNodePart(1, 1, 0);
  refiner.reset(new TwoWayFMRefinerSimpleStopping(*hypergraph, config));
  refiner->initialize();
  ASSERT_THAT(hypergraph->partID(6), Eq(1));
  ASSERT_THAT(hypergraph->partID(2), Eq(1));
  double old_imbalance = metrics::imbalance(*hypergraph);
  HyperedgeWeight old_cut = metrics::hyperedgeCut(*hypergraph);
  std::vector<HypernodeID> refinement_nodes = { 1, 6 };

  config.partition.epsilon = 0.15;
  refiner->refine(refinement_nodes, 2, 42, old_cut, old_imbalance);

  ASSERT_THAT(hypergraph->partID(6), Eq(1));
  ASSERT_THAT(hypergraph->partID(2), Eq(1));
}

TEST_F(ATwoWayFMRefiner, RollsBackAllNodeMovementsIfCutCouldNotBeImproved) {
  double old_imbalance = metrics::imbalance(*hypergraph);
  HyperedgeWeight cut = metrics::hyperedgeCut(*hypergraph);
  std::vector<HypernodeID> refinement_nodes = { 1, 6 };

  config.partition.epsilon = 0.15;
  refiner->refine(refinement_nodes, 2, 42, cut, old_imbalance);

  ASSERT_THAT(cut, Eq(metrics::hyperedgeCut(*hypergraph)));
  ASSERT_THAT(hypergraph->partID(1), Eq(0));
  ASSERT_THAT(hypergraph->partID(5), Eq(1));
}

// Ugly: We could seriously need Mocks here!
TEST_F(AGainUpdateMethod, RespectsPositiveGainUpdateSpecialCaseForHyperedgesOfSize2) {
  Hypergraph hypergraph(2, 1, HyperedgeIndexVector { 0, 2 }, HyperedgeVector { 0, 1 });
  hypergraph.setNodePart(0, 0);
  hypergraph.setNodePart(1, 0);

  TwoWayFMRefinerSimpleStopping refiner(hypergraph, config);
  refiner.initialize();
  // bypassing activate since neither 0 nor 1 is actually a border node
  refiner._pq.insert(0, 1, refiner.computeGain(0));
  refiner._pq.insert(1, 1, refiner.computeGain(1));
  refiner._pq.enablePart(1);
  ASSERT_THAT(refiner._pq.key(0, 1), Eq(-1));
  ASSERT_THAT(refiner._pq.key(1, 1), Eq(-1));

  hypergraph.changeNodePart(1, 0, 1);
  refiner._marked[1] = true;

  refiner.updateNeighbours(1, 0, 1,  /* dummy max-part-weight */ 42);

  ASSERT_THAT(refiner._pq.key(0, 1), Eq(1));
  // updateNeighbours does not delete the current max_gain node, neither does it update its gain
  ASSERT_THAT(refiner._pq.key(1, 1), Eq(-1));
}

TEST_F(AGainUpdateMethod, RespectsNegativeGainUpdateSpecialCaseForHyperedgesOfSize2) {
  Hypergraph hypergraph(3, 2, HyperedgeIndexVector { 0, 2, 4 }, HyperedgeVector { 0, 1, 0, 2 });
  hypergraph.setNodePart(0, 0);
  hypergraph.setNodePart(1, 1);
  hypergraph.setNodePart(2, 1);

  TwoWayFMRefinerSimpleStopping refiner(hypergraph, config);
  refiner.initialize();
  refiner.activate(0,  /* dummy max-part-weight */ 42);
  refiner.activate(1,  /* dummy max-part-weight */ 42);
  ASSERT_THAT(refiner._pq.key(0, 1), Eq(2));
  ASSERT_THAT(refiner._pq.key(1, 0), Eq(1));
  refiner._pq.enablePart(0);
  refiner._pq.enablePart(1);

  hypergraph.changeNodePart(1, 1, 0);
  refiner._marked[1] = true;
  refiner.updateNeighbours(1, 1, 0,  /* dummy max-part-weight */ 42);

  ASSERT_THAT(refiner._pq.key(0, 1), Eq(0));
}

TEST_F(AGainUpdateMethod, HandlesCase0To1) {
  Hypergraph hypergraph(4, 1, HyperedgeIndexVector { 0, 4 }, HyperedgeVector { 0, 1, 2, 3 });
  hypergraph.setNodePart(0, 0);
  hypergraph.setNodePart(1, 0);
  hypergraph.setNodePart(2, 0);
  hypergraph.setNodePart(3, 0);

  TwoWayFMRefinerSimpleStopping refiner(hypergraph, config);
  refiner.initialize();
  // bypassing activate since neither 0 nor 1 is actually a border node
  refiner._pq.insert(0, 1, refiner.computeGain(0));
  refiner._pq.insert(1, 1, refiner.computeGain(1));
  refiner._pq.insert(2, 1, refiner.computeGain(2));
  refiner._pq.insert(3, 1, refiner.computeGain(3));
  refiner._pq.enablePart(1);

  ASSERT_THAT(refiner._pq.key(0, 1), Eq(-1));
  ASSERT_THAT(refiner._pq.key(1, 1), Eq(-1));
  ASSERT_THAT(refiner._pq.key(2, 1), Eq(-1));
  ASSERT_THAT(refiner._pq.key(3, 1), Eq(-1));

  hypergraph.changeNodePart(3, 0, 1);
  refiner._marked[3] = true;
  refiner.updateNeighbours(3, 0, 1,  /* dummy max-part-weight */ 42);

  ASSERT_THAT(refiner._pq.key(0, 1), Eq(0));
  ASSERT_THAT(refiner._pq.key(1, 1), Eq(0));
  ASSERT_THAT(refiner._pq.key(2, 1), Eq(0));
}

TEST_F(AGainUpdateMethod, HandlesCase1To0) {
  Hypergraph hypergraph(5, 2, HyperedgeIndexVector { 0, 4, 8 }, HyperedgeVector { 0, 1, 2, 3, 0, 1, 2, 4 });
  hypergraph.setNodePart(0, 0);
  hypergraph.setNodePart(1, 0);
  hypergraph.setNodePart(2, 0);
  hypergraph.setNodePart(3, 1);
  hypergraph.setNodePart(4, 1);

  TwoWayFMRefinerSimpleStopping refiner(hypergraph, config);
  refiner.initialize();
  // bypassing activate since neither 0 nor 1 is actually a border node
  refiner.activate(0,  /* dummy max-part-weight */ 42);
  refiner.activate(1,  /* dummy max-part-weight */ 42);
  refiner.activate(2,  /* dummy max-part-weight */ 42);
  refiner.activate(3,  /* dummy max-part-weight */ 42);
  refiner.activate(4,  /* dummy max-part-weight */ 42);
  ASSERT_THAT(refiner._pq.key(0, 1), Eq(0));
  ASSERT_THAT(refiner._pq.key(1, 1), Eq(0));
  ASSERT_THAT(refiner._pq.key(2, 1), Eq(0));
  ASSERT_THAT(refiner._pq.key(3, 0), Eq(1));

  hypergraph.changeNodePart(3, 1, 0);
  refiner._marked[3] = true;
  refiner.updateNeighbours(3, 1, 0,  /* dummy max-part-weight */ 42);

  ASSERT_THAT(refiner._pq.key(0, 1), Eq(-1));
  ASSERT_THAT(refiner._pq.key(1, 1), Eq(-1));
  ASSERT_THAT(refiner._pq.key(2, 1), Eq(-1));
}

TEST_F(AGainUpdateMethod, HandlesCase2To1) {
  Hypergraph hypergraph(4, 1, HyperedgeIndexVector { 0, 4 }, HyperedgeVector { 0, 1, 2, 3 });
  hypergraph.setNodePart(0, 0);
  hypergraph.setNodePart(1, 0);
  hypergraph.setNodePart(2, 1);
  hypergraph.setNodePart(3, 1);

  TwoWayFMRefinerSimpleStopping refiner(hypergraph, config);
  refiner.initialize();
  refiner.activate(0,  /* dummy max-part-weight */ 42);
  refiner.activate(1,  /* dummy max-part-weight */ 42);
  refiner.activate(2,  /* dummy max-part-weight */ 42);
  refiner.activate(3,  /* dummy max-part-weight */ 42);
  ASSERT_THAT(refiner._pq.key(0, 1), Eq(0));
  ASSERT_THAT(refiner._pq.key(1, 1), Eq(0));
  ASSERT_THAT(refiner._pq.key(2, 0), Eq(0));
  ASSERT_THAT(refiner._pq.key(3, 0), Eq(0));

  hypergraph.changeNodePart(3, 1, 0);
  refiner._marked[3] = true;
  refiner.updateNeighbours(3, 1, 0,  /* dummy max-part-weight */ 42);

  ASSERT_THAT(refiner._pq.key(0, 1), Eq(0));
  ASSERT_THAT(refiner._pq.key(1, 1), Eq(0));
  ASSERT_THAT(refiner._pq.key(2, 0), Eq(1));
}

TEST_F(AGainUpdateMethod, HandlesCase1To2) {
  Hypergraph hypergraph(4, 1, HyperedgeIndexVector { 0, 4 }, HyperedgeVector { 0, 1, 2, 3 });
  hypergraph.setNodePart(0, 0);
  hypergraph.setNodePart(1, 0);
  hypergraph.setNodePart(2, 0);
  hypergraph.setNodePart(3, 1);

  TwoWayFMRefinerSimpleStopping refiner(hypergraph, config);
  refiner.initialize();
  refiner.activate(0,  /* dummy max-part-weight */ 42);
  refiner.activate(1,  /* dummy max-part-weight */ 42);
  refiner.activate(2,  /* dummy max-part-weight */ 42);
  refiner.activate(3,  /* dummy max-part-weight */ 42);
  ASSERT_THAT(refiner._pq.key(0, 1), Eq(0));
  ASSERT_THAT(refiner._pq.key(1, 1), Eq(0));
  ASSERT_THAT(refiner._pq.key(2, 1), Eq(0));
  ASSERT_THAT(refiner._pq.key(3, 0), Eq(1));

  hypergraph.changeNodePart(2, 0, 1);
  refiner._marked[2] = true;
  refiner.updateNeighbours(2, 0, 1,  /* dummy max-part-weight */ 42);

  ASSERT_THAT(refiner._pq.key(0, 1), Eq(0));
  ASSERT_THAT(refiner._pq.key(1, 1), Eq(0));
  ASSERT_THAT(refiner._pq.key(3, 0), Eq(0));
}

TEST_F(AGainUpdateMethod, HandlesSpecialCaseOfHyperedgeWith3Pins) {
  Hypergraph hypergraph(3, 1, HyperedgeIndexVector { 0, 3 }, HyperedgeVector { 0, 1, 2 });
  hypergraph.setNodePart(0, 0);
  hypergraph.setNodePart(1, 0);
  hypergraph.setNodePart(2, 1);

  TwoWayFMRefinerSimpleStopping refiner(hypergraph, config);
  refiner.initialize();
  refiner.activate(0,  /* dummy max-part-weight */ 42);
  refiner.activate(1,  /* dummy max-part-weight */ 42);
  refiner.activate(2,  /* dummy max-part-weight */ 42);
  ASSERT_THAT(refiner._pq.key(0, 1), Eq(0));
  ASSERT_THAT(refiner._pq.key(1, 1), Eq(0));
  ASSERT_THAT(refiner._pq.key(2, 0), Eq(1));

  hypergraph.changeNodePart(1, 0, 1);
  refiner._marked[1] = true;
  refiner.updateNeighbours(1, 0, 1,  /* dummy max-part-weight */ 42);

  ASSERT_THAT(refiner._pq.key(0, 1), Eq(1));
  ASSERT_THAT(refiner._pq.key(2, 0), Eq(0));
}

TEST_F(AGainUpdateMethod, RemovesNonBorderNodesFromPQ) {
  Hypergraph hypergraph(3, 1, HyperedgeIndexVector { 0, 3 }, HyperedgeVector { 0, 1, 2 });
  hypergraph.setNodePart(0, 0);
  hypergraph.setNodePart(1, 1);
  hypergraph.setNodePart(2, 0);

  TwoWayFMRefinerSimpleStopping refiner(hypergraph, config);
  refiner.initialize();
  refiner.activate(0,  /* dummy max-part-weight */ 42);
  refiner.activate(1,  /* dummy max-part-weight */ 42);
  ASSERT_THAT(refiner._pq.key(0, 1), Eq(0));
  ASSERT_THAT(refiner._pq.key(1, 0), Eq(1));
  ASSERT_THAT(refiner._pq.contains(2, 1), Eq(false));
  ASSERT_THAT(refiner._pq.contains(0, 1), Eq(true));

  hypergraph.changeNodePart(1, 1, 0);
  refiner._marked[1] = true;
  refiner.updateNeighbours(1, 1, 0,  /* dummy max-part-weight */ 42);

  ASSERT_THAT(refiner._pq.key(1, 0), Eq(1));
  ASSERT_THAT(refiner._pq.contains(0), Eq(false));
  ASSERT_THAT(refiner._pq.contains(2), Eq(false));
}

TEST_F(AGainUpdateMethod, ActivatesUnmarkedNeighbors) {
  Hypergraph hypergraph(3, 1, HyperedgeIndexVector { 0, 3 }, HyperedgeVector { 0, 1, 2 });
  hypergraph.setNodePart(0, 0);
  hypergraph.setNodePart(1, 0);
  hypergraph.setNodePart(2, 0);

  TwoWayFMRefinerSimpleStopping refiner(hypergraph, config);
  refiner.initialize();
  // bypassing activate since neither 0 nor 1 is actually a border node
  refiner._pq.insert(0, 1, refiner.computeGain(0));
  refiner._pq.insert(1, 1, refiner.computeGain(1));
  refiner._pq.enablePart(1);
  ASSERT_THAT(refiner._pq.key(0, 1), Eq(-1));
  ASSERT_THAT(refiner._pq.key(1, 1), Eq(-1));
  ASSERT_THAT(refiner._pq.contains(2), Eq(false));

  hypergraph.changeNodePart(1, 0, 1);
  refiner._marked[1] = true;
  refiner.updateNeighbours(1, 0, 1,  /* dummy max-part-weight */ 42);

  ASSERT_THAT(refiner._pq.key(0, 1), Eq(0));
  ASSERT_THAT(refiner._pq.key(1, 1), Eq(-1));
  ASSERT_THAT(refiner._pq.contains(2, 1), Eq(true));
  ASSERT_THAT(refiner._pq.key(2, 1), Eq(0));
}

TEST_F(AGainUpdateMethod, DoesNotDeleteJustActivatedNodes) {
  Hypergraph hypergraph(5, 3, HyperedgeIndexVector { 0, 2, 5, 8 },
                        HyperedgeVector { 0, 1, 2, 3, 4, 2, 3, 4 });
  hypergraph.setNodePart(0, 0);
  hypergraph.setNodePart(1, 0);
  hypergraph.setNodePart(2, 0);
  hypergraph.setNodePart(3, 1);
  hypergraph.setNodePart(4, 0);
  TwoWayFMRefinerSimpleStopping refiner(hypergraph, config);
  refiner.initialize();

  // bypassing activate
  refiner._pq.insert(2, 1, refiner.computeGain(2));
  refiner._pq.enablePart(1);
  refiner.moveHypernode(2, 0, 1);
  refiner._marked[2] = true;
  refiner.updateNeighbours(2, 0, 1,  /* dummy max-part-weight */ 42);

  ASSERT_THAT(refiner._pq.contains(4, 1), Eq(true));
  ASSERT_THAT(refiner._pq.contains(3, 0), Eq(true));
}

TEST(ARefiner, ChecksIfMovePreservesBalanceConstraint) {
  Hypergraph hypergraph(4, 1, HyperedgeIndexVector { 0, 4 },
                        HyperedgeVector { 0, 1, 2, 3 });
  hypergraph.setNodePart(0, 0);
  hypergraph.setNodePart(1, 0);
  hypergraph.setNodePart(2, 0);
  hypergraph.setNodePart(3, 1);

  Configuration config;
  config.partition.epsilon = 0.02;
  config.partition.max_part_weight = (1 + config.partition.epsilon)
                                     * ceil(hypergraph.initialNumNodes()
                                            / static_cast<double>(config.partition.k));

  TwoWayFMRefinerSimpleStopping refiner(hypergraph, config);
  refiner.initialize();
  ASSERT_THAT(refiner.moveIsFeasible(1, 0, 1), Eq(true));
  ASSERT_THAT(refiner.moveIsFeasible(3, 1, 0), Eq(false));
}

TEST_F(ATwoWayFMRefiner, ConsidersSingleNodeHEsDuringInitialGainComputation) {
  hypergraph.reset(new Hypergraph(2, 2, HyperedgeIndexVector { 0, 2,  /*sentinel*/ 3 },
                                  HyperedgeVector { 0, 1, 0 }, 2));

  config.two_way_fm.max_number_of_fruitless_moves = 50;
  config.partition.total_graph_weight = 2;
  config.partition.k = 2;
  config.partition.epsilon = 1.0;
  config.partition.max_part_weight =
    (1 + config.partition.epsilon)
    * ceil(hypergraph->numNodes() / static_cast<double>(config.partition.k));

  hypergraph->setNodePart(0, 0);
  hypergraph->setNodePart(1, 1);

  refiner.reset(new TwoWayFMRefinerSimpleStopping(*hypergraph, config));
  refiner->initialize();

  ASSERT_THAT(refiner->computeGain(0), Eq(1));
}
}                // namespace partition
