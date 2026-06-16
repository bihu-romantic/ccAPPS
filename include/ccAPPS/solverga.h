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
#ifndef SOLVERGA_H
#define SOLVERGA_H

#include <random>
#include <unordered_map>
#include <vector>

#include "ccAPPS/solveraco.h"

namespace ccAPPS {

/* Configuration parameters for the Genetic Algorithm solver.
 *
 * The GA reuses ACO's candidate building, constraint-aware scheduling,
 * evaluation and solution application logic. Only the metaheuristic search
 * strategy is different.
 */
struct GAConfig {
  int population = 40;
  int generations = 120;
  int elite = 4;
  int tournament_size = 3;
  double crossover_rate = 0.85;
  double mutation_rate = 0.20;
  double inversion_rate = 0.08;
  double random_immigrant_rate = 0.05;
  int stagnation_limit = 35;
  bool runMRP = true;
  bool joint_optimization = true;
  int ga_mrp_iterations = 2;
  double ga_mrp_improvement = 0.01;
  int purchase_material_mode = 1;

  // Fitness weights are mirrored into the reused ACO evaluator.
  double weight_tardiness = 1.0;
  double weight_cost = 0.5;
  double weight_setup = 0.3;
  double weight_priority = 10.0;
  double weight_balance = 0.5;
};

struct GAChromosome {
  // Operation gene: ordered candidate ids. One candidate id represents one
  // unique operation plan, while alternate candidates share the same op.
  vector<int> order;
  // Resource-assignment gene: selected index in candidatesById[candId].
  unordered_map<int, size_t> assignment;
  double fitness = -numeric_limits<double>::max();
};

class SolverGA : public SolverACO {
 public:
  using SolverACO::solve;

  SolverGA() : rng_(random_device{}()) { initType(metadata); }
  ~SolverGA() override {}

  static int initialize();
  static PyObject* create(PyTypeObject*, PyObject*, PyObject*);
  const MetaClass& getType() const override { return *metadata; }
  static const MetaClass* metadata;

  void setConfig(const GAConfig& cfg);
  const GAConfig& getGAConfig() const { return config_; }
  void setRunMRP(bool b) { config_.runMRP = b; syncACOConfig(); }
  bool getRunMRP() const { return config_.runMRP; }
  void setPurchaseMaterialMode(int m) {
    config_.purchase_material_mode = m;
    syncACOConfig();
  }
  int getPurchaseMaterialMode() const { return config_.purchase_material_mode; }

  void solve(void* v = nullptr) override;
  void solve(const Resource* res, void* v = nullptr) override;
  void solveJoint(const vector<const Resource*>& resources);

  void solve(const ResourceInfinite* r, void* v = nullptr) override {
    SolverCreate::solve(r, v);
  }
  void solve(const ResourceBuckets* r, void* v = nullptr) override {
    SolverCreate::solve(r, v);
  }

 private:
  using CandidateBuckets = unordered_map<int, vector<const CandidateOp*>>;

  void syncACOConfig();
  vector<const Resource*> collectBottlenecks() const;
  vector<CandidateOp> buildSingleResourceCandidates(
      const Resource* res,
      const vector<OperationPlan*>& plans) const;

  CandidateBuckets groupCandidates(const vector<CandidateOp>& candidates) const;
  GAChromosome randomChromosome(const CandidateBuckets& buckets);
  GAChromosome seedPriorityChromosome(const CandidateBuckets& buckets);
  AntSolution decodeChromosome(
      const GAChromosome& chromosome,
      const vector<const Resource*>& resources,
      const CandidateBuckets& buckets,
      const unordered_map<const Resource*, Date>& resourceTimes);
  void evaluateChromosome(
      GAChromosome& chromosome,
      const vector<const Resource*>& resources,
      const CandidateBuckets& buckets,
      const unordered_map<const Resource*, Date>& resourceTimes);
  GAChromosome tournamentSelect(const vector<GAChromosome>& population);
  GAChromosome crossover(
      const GAChromosome& first,
      const GAChromosome& second,
      const CandidateBuckets& buckets);
  void mutate(GAChromosome& chromosome, const CandidateBuckets& buckets);
  AntSolution evolve(
      const vector<const Resource*>& resources,
      const vector<CandidateOp>& candidates,
      const unordered_map<const Resource*, Date>& resourceTimes);

  GAConfig config_;
  mt19937 rng_;
  double lastBestFitness_ = -numeric_limits<double>::max();
  bool stagnationOccurred_ = false;
};

PyObject* run_ga(PyObject*, PyObject*);

}  // namespace ccAPPS
#endif  // SOLVERGA_H
