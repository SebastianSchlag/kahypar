/*******************************************************************************
 * This file is part of KaHyPar.
 *
 * Copyright (C) 2018 Sebastian Schlag <sebastian.schlag@kit.edu>
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

#include <memory>

#include "gmock/gmock.h"

#include "include/libkahypar.h"

#include "kahypar/macros.h"

namespace kahypar {
TEST(KaHyPar, CanBeCalledViaInterface) {
  kahypar_context_t* context = kahypar_context_new();

  kahypar_configure_context_from_file(context, "../../../config/km1_direct_kway_sea18.ini");

  const kahypar_hypernode_id_t num_vertices = 7;
  const kahypar_hyperedge_id_t num_hyperedges = 4;

  std::unique_ptr<kahypar_hyperedge_weight_t[]> hyperedge_weights =
    std::make_unique<kahypar_hyperedge_weight_t[]>(4);

  // force the cut to contain hyperedge 0 and 2
  hyperedge_weights[0] = 1;
  hyperedge_weights[1] = 1000;
  hyperedge_weights[2] = 1;
  hyperedge_weights[3] = 1000;

  std::unique_ptr<size_t[]> hyperedge_index_vector =
    std::make_unique<size_t[]>(5);

  hyperedge_index_vector[0] = 0;
  hyperedge_index_vector[1] = 2;
  hyperedge_index_vector[2] = 6;
  hyperedge_index_vector[3] = 9;
  hyperedge_index_vector[4] = 12;

  std::unique_ptr<kahypar_hyperedge_id_t[]> hyperedge_vector =
    std::make_unique<kahypar_hyperedge_id_t[]>(12);

  // hypergraph from hMetis manual page 14
  hyperedge_vector[0] = 0;
  hyperedge_vector[1] = 2;
  hyperedge_vector[2] = 0;
  hyperedge_vector[3] = 1;
  hyperedge_vector[4] = 3;
  hyperedge_vector[5] = 4;
  hyperedge_vector[6] = 3;
  hyperedge_vector[7] = 4;
  hyperedge_vector[8] = 6;
  hyperedge_vector[9] = 2;
  hyperedge_vector[10] = 5;
  hyperedge_vector[11] = 6;

  const double imbalance = 0.03;
  const kahypar_partition_id_t k = 2;
  kahypar_hyperedge_weight_t objective = 0;

  std::vector<kahypar_partition_id_t> partition(num_vertices, -1);

  kahypar_partition(num_vertices, num_hyperedges,
                    imbalance, k,
                    /*vertex_weights */ nullptr, hyperedge_weights.get(),
                    hyperedge_index_vector.get(), hyperedge_vector.get(),
                    &objective, context, partition.data());


  std::vector<kahypar_partition_id_t> correct_solution({ 0, 0, 1, 0, 0, 1, 1 });
  ASSERT_THAT(partition, ::testing::ContainerEq(correct_solution));
  ASSERT_TRUE(objective == 2);

  kahypar_context_free(context);
}
}  // namespace kahypar
