/*******************************************************************************
 * This file is part of KaHyPar.
 *
 * Copyright (C) 2014-2016 Sebastian Schlag <sebastian.schlag@kit.edu>
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

#include <chrono>
#include <iostream>
#include <memory>
#include <string>

#include "kahypar/io/sql_plottools_serializer.h"
#include "kahypar/application/command_line_options.h"
#include "kahypar/definitions.h"
#include "kahypar/io/hypergraph_io.h"
#include "kahypar/io/partitioning_output.h"
#include "kahypar/kahypar.h"
#include "kahypar/macros.h"
#include "kahypar/utils/math.h"
#include "kahypar/utils/randomize.h"
#include "kahypar/utils/timer.h"

using kahypar::HighResClockTimepoint;
using kahypar::Partitioner;
using kahypar::Context;
using kahypar::Timer;
using kahypar::Timepoint;

int main(int argc, char* argv[]) {
  Context context;

  kahypar::processCommandLineInput(context, argc, argv);
  kahypar::sanityCheck(context);

  if (!context.partition.quiet_mode) {
    kahypar::io::printBanner();
  }

  if (context.partition.global_search_iterations != 0 &&
      context.partition.mode == kahypar::Mode::recursive_bisection) {
    std::cerr << "V-Cycles are not supported in recursive bisection mode." << std::endl;
    std::exit(-1);
  }

  kahypar::Randomize::instance().setSeed(context.partition.seed);

  kahypar::Hypergraph hypergraph(
    kahypar::io::createHypergraphFromFile(context.partition.graph_filename,
                                          context.partition.k));

  Partitioner partitioner;


 const HighResClockTimepoint complete_start = std::chrono::high_resolution_clock::now();
  while(Timer::instance().evolutionaryResult().total_evolutionary < context.evolutionary.time_limit_seconds) {

     const HighResClockTimepoint start = std::chrono::high_resolution_clock::now();
     partitioner.partition(hypergraph, context);
     const HighResClockTimepoint end = std::chrono::high_resolution_clock::now();
     Timer::instance().add(context, Timepoint::evolutionary,
                        std::chrono::duration<double>(end - start).count());
     kahypar::io::serializer::serializeEvolutionary(context, hypergraph);
     hypergraph.reset();
  }
  const HighResClockTimepoint complete_end = std::chrono::high_resolution_clock::now();


#ifdef GATHER_STATS
  LOG << "*******************************";
  LOG << "***** GATHER_STATS ACTIVE *****";
  LOG << "*******************************";
  kahypar::io::printPartitioningStatistics();
#endif

  if (!context.partition.quiet_mode) {
    kahypar::io::printPartitioningResults(hypergraph, context, std::chrono::duration<double>(complete_end - complete_start));
    LOG << "";
  }
  kahypar::io::writePartitionFile(hypergraph,
                                  context.partition.graph_partition_filename);

  if (context.partition.sp_process_output) {
    kahypar::io::serializer::serialize(context, hypergraph, std::chrono::duration<double>(complete_end - complete_start));
  }

  return 0;
}
