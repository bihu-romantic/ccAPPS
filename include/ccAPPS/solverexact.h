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
#ifndef SOLVEREXACT_H
#define SOLVEREXACT_H

#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ccAPPS/solveraco.h"

namespace ccAPPS {

/* Configuration parameters for the exact resource scheduler.
 *
 * The exact solver optimizes the same candidate model and fitness function as
 * ACO, but searches the complete assignment/sequence space for small
 * bottleneck subproblems. Larger problems can fall back to ACO to keep runtime
 * bounded in production planning runs.
 */
struct ExactConfig {
  bool runMRP = true;
  bool joint_optimization = true;
  bool fallback_to_aco = true;
  int purchase_material_mode = 1;
  int max_operations = 10;
  long max_nodes = 200000;
  bool prefer_mip = true;
  int time_bucket_seconds = 3600;
  int mip_time_limit_seconds = 30;

  double weight_tardiness = 1.0;
  double weight_cost = 0.5;
  double weight_setup = 0.3;
  double weight_priority = 10.0;
  double weight_balance = 0.5;
};

class SolverExact : public SolverACO {
 public:
  using SolverACO::solve;

  SolverExact() { initType(metadata); }
  ~SolverExact() override {}

  static int initialize();
  static PyObject* create(PyTypeObject*, PyObject*, PyObject*);
  const MetaClass& getType() const override { return *metadata; }
  static const MetaClass* metadata;

  void setConfig(const ExactConfig& cfg);
  const ExactConfig& getExactConfig() const { return config_; }
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
  struct ExactSearchState {
    unordered_map<const Resource*, const OperationPlan*> prevOp;
    unordered_map<const Resource*, Date> curTime;
    unordered_set<const OperationPlan*> scheduled;
    AntSolution partial;
    AntSolution best;
    long nodes = 0;
    bool aborted = false;
  };

  void syncACOConfig();
  vector<const Resource*> collectBottlenecks() const;
  vector<CandidateOp> buildSingleResourceCandidates(
      const Resource* res,
      const vector<OperationPlan*>& plans) const;
  AntSolution exactSearch(
      const vector<const Resource*>& resources,
      const vector<CandidateOp>& candidates);
  AntSolution solveTimeIndexedMIP(
      const vector<const Resource*>& resources,
      const vector<CandidateOp>& candidates);
  void branch(
      const vector<const Resource*>& resources,
      const vector<CandidateOp>& candidates,
      size_t targetCount,
      ExactSearchState& state);
  bool appendCandidate(
      const CandidateOp& candidate,
      const ExactSearchState& state,
      AntSolution& nextPartial,
      unordered_map<const Resource*, const OperationPlan*>& nextPrev,
      unordered_map<const Resource*, Date>& nextTime) const;
  void solveWithAcoFallback(void* v);

  ExactConfig config_;
};

PyObject* run_exact(PyObject*, PyObject*);

}  // namespace ccAPPS
#endif  // SOLVEREXACT_H
