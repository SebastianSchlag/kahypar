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

#include <vector>

#include "kahypar/meta/policy_registry.h"
#include "kahypar/partition/context.h"
#include "kahypar/meta/typelist.h"

namespace kahypar {
class FlowExecutionPolicy : public meta::PolicyBase {
 public:
  FlowExecutionPolicy() :
    _flowExecutionLevels() { }

  virtual void initialize(const Hypergraph& hg, const Context& context) = 0;

  bool executeFlow(const Hypergraph& hg) {
    if (_flowExecutionLevels.size() == 0) {
      return false;
    }
    size_t cur_idx = _flowExecutionLevels.size()-1;
    if (hg.currentNumNodes() >= _flowExecutionLevels[cur_idx]) {
      _flowExecutionLevels.pop_back();
      return true;
    } else {
      return false;
    }
  }

 protected:
  std::vector<size_t> _flowExecutionLevels;
};

class ConstantFlowExecution : public FlowExecutionPolicy {
 public:
  ConstantFlowExecution() :
    FlowExecutionPolicy() { }

  void initialize(const Hypergraph& hg, const Context& context) {
    std::vector<size_t> tmpFlowExecutionLevels;
    size_t cur_execution_level = hg.currentNumNodes() + 1;
    for (; cur_execution_level < hg.initialNumNodes();
           cur_execution_level = cur_execution_level +
           context.local_search.flow.beta) {
      tmpFlowExecutionLevels.push_back(cur_execution_level);
    }
    tmpFlowExecutionLevels.push_back(hg.initialNumNodes());
    std::reverse(tmpFlowExecutionLevels.begin(), tmpFlowExecutionLevels.end());
    _flowExecutionLevels.insert(_flowExecutionLevels.end(),
                                tmpFlowExecutionLevels.begin(),
                                tmpFlowExecutionLevels.end());
  }

 private:
  using FlowExecutionPolicy::_flowExecutionLevels;
};

class MultilevelFlowExecution : public FlowExecutionPolicy {
 public:
  MultilevelFlowExecution() :
    FlowExecutionPolicy() { }

  void initialize(const Hypergraph& hg, const Context&) {
    std::vector<size_t> tmpFlowExecutionLevels;
    for (size_t i = 0; hg.initialNumNodes() / std::pow(2, i) >= hg.currentNumNodes(); ++i) {
      tmpFlowExecutionLevels.push_back(hg.initialNumNodes() / std::pow(2, i));
    }
    _flowExecutionLevels.insert(_flowExecutionLevels.end(),
                                tmpFlowExecutionLevels.begin(),
                                tmpFlowExecutionLevels.end());
  }

 private:
  using FlowExecutionPolicy::_flowExecutionLevels;
};

class ExponentialFlowExecution : public FlowExecutionPolicy {
 public:
  ExponentialFlowExecution() :
    FlowExecutionPolicy() { }

  void initialize(const Hypergraph& hg, const Context&) {
    std::vector<size_t> tmpFlowExecutionLevels;
    for (size_t i = 0; hg.currentNumNodes() + std::pow(2, i) < hg.initialNumNodes(); ++i) {
      tmpFlowExecutionLevels.push_back(hg.currentNumNodes() + std::pow(2, i));
    }
    tmpFlowExecutionLevels.push_back(hg.initialNumNodes());
    std::reverse(tmpFlowExecutionLevels.begin(), tmpFlowExecutionLevels.end());
    _flowExecutionLevels.insert(_flowExecutionLevels.end(),
                                tmpFlowExecutionLevels.begin(),
                                tmpFlowExecutionLevels.end());
  }

 private:
  using FlowExecutionPolicy::_flowExecutionLevels;
};


using FlowExecutionPolicyClasses = meta::Typelist<ConstantFlowExecution,
                                                  MultilevelFlowExecution,
                                                  ExponentialFlowExecution>;

}  // namespace kahypar
