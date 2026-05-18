/***************************************************************************
 *                                                                         *
 * Copyright (C) 2007-2015 by ccAPPS bv                                   *
 *                                                                         *
 * Permission is hereby granted, free of charge, to any person obtaining   *
 * a copy of this software and associated documentation files (the         *
 * "Software"), to deal in the Software without restriction, including     *
 * without limitation the rights to use, copy, modify, merge, publish,     *
 * distribute, sublicense, and/or sell copies of the Software, and to      *
 * permit persons to whom the Software is furnished to do so, subject to   *
 * the following conditions:                                               *
 *                                                                         *
 * The above copyright notice and this permission notice shall be          *
 * included in all copies or substantial portions of the Software.         *
 *                                                                         *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,         *
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF      *
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND                   *
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE  *
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION  *
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION   *
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.         *
 *                                                                         *
 ***************************************************************************/

#pragma once
#ifndef SOLVERACO_H
#define SOLVERACO_H

#include <cmath>
#include <random>
#include <unordered_map>
#include <vector>

#include "ccAPPS/solver.h"

namespace ccAPPS {

/* Configuration parameters for the Ant Colony Optimization solver. */
struct ACOConfig {
  // Number of ants per iteration
  int ants = 20;

  // Maximum number of iterations
  int iterations = 100;

  // Pheromone weight (alpha)
  double alpha = 1.0;

  // Heuristic weight (beta)
  double beta = 2.0;

  // Evaporation rate [0, 1]
  double evaporation = 0.1;

  // Pheromone deposit factor
  double Q = 100.0;

  // Initial pheromone value
  double tau0 = 1.0;

  // Stagnation threshold: stop if no improvement for N iterations
  int stagnation_limit = 30;

  // Weight for tardiness in the evaluation function
  double weight_tardiness = 1.0;

  // Weight for cost in the evaluation function
  double weight_cost = 0.5;

  // Weight for setup time in the evaluation function
  double weight_setup = 0.3;

  // Number of elite ants whose solutions get extra pheromone
  int elite_ants = 3;
};

/* A single ant's solution for resource scheduling.
 *
 * Each ant builds a sequence of operationplans on a specific resource.
 * The sequence determines the order in which operationplans are executed.
 */
struct AntSolution {
  // Ordered sequence of operationplans on the resource
  vector<OperationPlan*> sequence;

  // Start dates for each operationplan in the sequence
  vector<Date> startDates;

  // End dates for each operationplan in the sequence
  vector<Date> endDates;

  // Fitness value (higher is better)
  double fitness = -numeric_limits<double>::max();
};

/* Pheromone matrix for transitions between operationplans on a resource.
 *
 * The key is a pair (from_op, to_op), where from_op can be nullptr
 * (meaning "start of sequence"). The value is the pheromone level.
 */
class PheromoneMatrix {
 public:
  PheromoneMatrix() = default;

  /* Get pheromone level for a transition. */
  double get(const OperationPlan* from, const OperationPlan* to) const;

  /* Set pheromone level for a transition. */
  void set(const OperationPlan* from, const OperationPlan* to, double value);

  /* Evaporate all pheromones by factor (1 - rho). */
  void evaporate(double rho);

  /* Deposit pheromone on a path (sequence of operationplans). */
  void deposit(const AntSolution& ant, double amount);

  /* Reset all pheromones to tau0. */
  void reset(double tau0);

 private:
  // Hash for unordered_map with pair key
  struct PairHash {
    size_t operator()(
        const pair<const OperationPlan*, const OperationPlan*>& p) const {
      return hash<const void*>()(p.first) ^
             (hash<const void*>()(p.second) << 1);
    }
  };

  unordered_map<pair<const OperationPlan*, const OperationPlan*>, double,
                PairHash>
      matrix_;
};

/* Ant Colony Optimization solver for resource scheduling.
 *
 * This solver augments the existing SolverCreate with ACO-based resource
 * scheduling. When enabled on a resource (via SearchMode::ACO on its Load),
 * the solver uses ACO to find an optimal sequence of operationplans on
 * that resource, minimizing a weighted combination of tardiness, cost,
 * and setup time.
 *
 * Usage:
 *   SolverACO solver;
 *   solver.setConfig(ACOConfig());
 *   solver.initPheromone(resource);
 *   solver.solve();  // runs ACO iterations
 */
class SolverACO : public SolverCreate {
 public:
  using SolverCreate::solve;

  SolverACO() : rng_(random_device{}()) { initType(metadata); }

  ~SolverACO() override {}

  /* Initialize the Python type and metadata. */
  static int initialize();

  /* Python factory method. */
  static PyObject* create(PyTypeObject*, PyObject*, PyObject*);

  const MetaClass& getType() const override { return *metadata; }
  static const MetaClass* metadata;

  /* Set ACO configuration. */
  void setConfig(const ACOConfig& cfg) { config_ = cfg; }
  const ACOConfig& getConfig() const { return config_; }

  /* Run the ACO solver for all ACO-enabled resources. */
  void solve(void* v = nullptr) override;

  /* Resource-level ACO scheduling.
   * Collects all operationplans on the given resource and finds the
   * optimal sequence using ant colony optimization.
   */
  void solve(const Resource* res, void* v = nullptr) override;

  /* Fall through: non-ACO resources use the standard SolverCreate logic. */
  void solve(const ResourceInfinite* r, void* v = nullptr) override {
    SolverCreate::solve(r, v);
  }
  void solve(const ResourceBuckets* r, void* v = nullptr) override {
    SolverCreate::solve(r, v);
  }

  /* Initialize pheromone matrix for a specific resource. */
  void initPheromone(const Resource* res);

  /* Get the pheromone matrix for a resource (const). */
  const PheromoneMatrix* getPheromone(const Resource* res) const;

 private:
  /* Construct a single ant's solution for the given resource. */
  AntSolution constructSolution(
      const Resource* res, const vector<OperationPlan*>& plans);

  /* Evaluate the quality of an ant's solution.
   * Returns a fitness value (higher is better).
   */
  double evaluate(const Resource* res, const AntSolution& solution);

  /* Compute the heuristic value for transitioning from op1 to op2. */
  double heuristic(const OperationPlan* from, const OperationPlan* to) const;

  /* Run local search (2-opt) to improve a solution. */
  void localSearch(const Resource* res, AntSolution& solution);

  /* Roll back all commands created during an ant's solution evaluation. */
  void rollbackSolution(const AntSolution& solution);

  /* Apply the best solution found so far to the actual plan. */
  void applyBestSolution(const Resource* res, const AntSolution& best);

  /* Compute setup time between two operationplans on the same resource. */
  Duration computeSetupTime(const OperationPlan* from,
                            const OperationPlan* to) const;

 private:
  ACOConfig config_;

  // One pheromone matrix per resource (keyed by resource pointer)
  unordered_map<const Resource*, PheromoneMatrix> pheromones_;

  // Random number generator
  mt19937 rng_;

  // Best solution found per resource during the current solve
  unordered_map<const Resource*, AntSolution> bestSolutions_;
};

}  // namespace ccAPPS

#endif  // SOLVERACO_H
