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

#include <algorithm>
#include <limits>
#include <unordered_set>

#include "ccAPPS/solverexact.h"

#ifdef HAVE_ORTOOLS
#include "absl/time/time.h"
#include "ortools/linear_solver/linear_solver.h"
#endif

namespace ccAPPS {

#ifdef HAVE_ORTOOLS
using operations_research::MPConstraint;
using operations_research::MPObjective;
using operations_research::MPSolver;
using operations_research::MPVariable;
#endif

const MetaClass* SolverExact::metadata;

static PyObject* solverExact_setRunMRP(PyObject* self, PyObject* args) {
  auto* solver = static_cast<SolverExact*>(self);
  int val;
  if (!PyArg_ParseTuple(args, "p", &val)) return nullptr;
  solver->setRunMRP(val != 0);
  Py_RETURN_NONE;
}

static PyObject* solverExact_getRunMRP(PyObject* self, PyObject*) {
  auto* solver = static_cast<SolverExact*>(self);
  return PyBool_FromLong(solver->getRunMRP() ? 1 : 0);
}

static PyObject* solverExact_setPurchaseMaterialMode(
    PyObject* self, PyObject* args) {
  auto* solver = static_cast<SolverExact*>(self);
  int val;
  if (!PyArg_ParseTuple(args, "i", &val)) return nullptr;
  if (val < 0 || val > 1) {
    PyErr_SetString(PyExc_ValueError,
                    "Expected 0 (infinite) or 1 (leadtime)");
    return nullptr;
  }
  solver->setPurchaseMaterialMode(val);
  Py_RETURN_NONE;
}

static PyObject* solverExact_getPurchaseMaterialMode(PyObject* self, PyObject*) {
  auto* solver = static_cast<SolverExact*>(self);
  return PyLong_FromLong(solver->getPurchaseMaterialMode());
}

PyObject* run_exact(PyObject*, PyObject*) {
  SolverExact exact;
  exact.setRunMRP(false);
  void* v = nullptr;
  exact.solve(v);
  auto* cmdMgr = exact.getCommandManager();
  if (cmdMgr) cmdMgr->commit();
  Py_RETURN_NONE;
}

int SolverExact::initialize() {
  metadata = MetaClass::registerClass<SolverExact>(
      "solver", "solver_exact", Object::create<SolverExact>);
  registerFields<SolverExact>(const_cast<MetaClass*>(metadata));

  auto& x = PythonExtension<SolverExact>::getPythonType();
  x.setName("solverExact");
  x.setDoc("ccAPPS exact solver for bottleneck resource subproblems");
  x.supportgetattro();
  x.supportsetattro();
  x.supportcreate(SolverExact::create);
  x.addMethod("solve",
              static_cast<PyObject* (*)(PyObject*, PyObject*, PyObject*)>(
                  SolverCreate::solve),
              METH_VARARGS, "run the solver");
  x.addMethod("commit", SolverCreate::commit, METH_NOARGS,
              "commit the plan changes");
  x.addMethod("rollback", SolverCreate::rollback, METH_NOARGS,
              "rollback the plan changes");
  x.addMethod("markAutofence", SolverCreate::markAutofence, METH_NOARGS,
              "mark the autofence of buffers");
  x.addMethod("setRunMRP", solverExact_setRunMRP, METH_VARARGS,
              "Enable/disable MRP propagation after exact solving");
  x.addMethod("getRunMRP", solverExact_getRunMRP, METH_NOARGS,
              "Check if MRP propagation is enabled");
  x.addMethod("setPurchaseMaterialMode", solverExact_setPurchaseMaterialMode,
              METH_VARARGS, "Set exact purchase material mode");
  x.addMethod("getPurchaseMaterialMode", solverExact_getPurchaseMaterialMode,
              METH_NOARGS, "Get exact purchase material mode");

  SolverExact::metadata->setPythonClass(x);
  return x.typeReady();
}

PyObject* SolverExact::create(PyTypeObject*, PyObject*, PyObject*) {
  return Object::create<SolverExact>();
}

void SolverExact::setConfig(const ExactConfig& cfg) {
  config_ = cfg;
  syncACOConfig();
}

void SolverExact::syncACOConfig() {
  ACOConfig acoCfg = getConfig();
  acoCfg.runMRP = config_.runMRP;
  acoCfg.joint_optimization = config_.joint_optimization;
  acoCfg.purchase_material_mode = config_.purchase_material_mode;
  acoCfg.weight_tardiness = config_.weight_tardiness;
  acoCfg.weight_cost = config_.weight_cost;
  acoCfg.weight_setup = config_.weight_setup;
  acoCfg.weight_priority = config_.weight_priority;
  acoCfg.weight_balance = config_.weight_balance;
  SolverACO::setConfig(acoCfg);
}

vector<const Resource*> SolverExact::collectBottlenecks() const {
  vector<const Resource*> result;
  for (auto res = Resource::begin(); res != Resource::end(); ++res) {
    if (!res->getConstrained()) continue;
    vector<OperationPlan*> plans;
    auto loadplans = res->getLoadPlans();
    for (auto it = loadplans.begin(); it != loadplans.end(); ++it) {
      OperationPlan* op = it->getOperationPlan();
      if (op && op->getQuantity() > 0.0 &&
          find(plans.begin(), plans.end(), op) == plans.end())
        plans.push_back(op);
    }
    if (plans.size() >= 2) result.push_back(&*res);
  }
  return result;
}

vector<CandidateOp> SolverExact::buildSingleResourceCandidates(
    const Resource* res,
    const vector<OperationPlan*>& plans) const {
  vector<CandidateOp> candidates;
  int id = 0;
  for (auto* op : plans) {
    CandidateOp c;
    c.op = op;
    c.res = res;
    c.candId = id++;
    c.earliestStart = earliestStart(op, res);
    c.allResources.push_back(res);
    candidates.push_back(c);
  }
  return candidates;
}

void SolverExact::branch(const vector<const Resource*>& resources,
                         const vector<CandidateOp>& candidates,
                         size_t targetCount,
                         ExactSearchState& state) {
  if (state.aborted) return;
  state.nodes++;
  if (state.nodes > config_.max_nodes) {
    state.aborted = true;
    return;
  }

  if (state.scheduled.size() == targetCount) {
    state.partial.fitness = evaluate(state.partial);
    if (state.partial.fitness > state.best.fitness) state.best = state.partial;
    return;
  }

  vector<const CandidateOp*> ready;
  for (const auto& c : candidates)
    if (c.op && !state.scheduled.count(c.op) && !c.allResources.empty())
      ready.push_back(&c);

  if (ready.empty()) return;

  sort(ready.begin(), ready.end(), [&](const CandidateOp* a,
                                       const CandidateOp* b) {
    double pa = heuristic(nullptr, a->op);
    double pb = heuristic(nullptr, b->op);
    return pa > pb;
  });

  for (const CandidateOp* cand : ready) {
    AntSolution nextPartial = state.partial;
    unordered_map<const Resource*, const OperationPlan*> nextPrev = state.prevOp;
    unordered_map<const Resource*, Date> nextTime = state.curTime;
    ExactSearchState preview = state;
    if (!appendCandidate(*cand, preview, nextPartial, nextPrev, nextTime))
      continue;

    ExactSearchState child = state;
    child.partial = move(nextPartial);
    child.prevOp = move(nextPrev);
    child.curTime = move(nextTime);
    child.scheduled.insert(cand->op);
    child.best = state.best;
    branch(resources, candidates, targetCount, child);
    state.nodes = child.nodes;
    state.aborted = child.aborted;
    if (child.best.fitness > state.best.fitness) state.best = child.best;
    if (state.aborted) return;
  }
}

bool SolverExact::appendCandidate(
    const CandidateOp& candidate,
    const ExactSearchState& state,
    AntSolution& nextPartial,
    unordered_map<const Resource*, const OperationPlan*>& nextPrev,
    unordered_map<const Resource*, Date>& nextTime) const {
  if (!candidate.op) return false;

  Date start = candidate.earliestStart;
  for (auto* r : candidate.allResources) {
    Duration setup = computeSetupTime(nextPrev[r], candidate.op);
    start = max(start, nextTime[r] + setup);
  }
  const Resource* durationResource = candidate.res ? candidate.res
                                                  : candidate.allResources[0];
  Duration dur = estimateOperationDuration(candidate.op, durationResource);
  Date end;
  if (candidate.op->getOperation()) {
    DateRange range = candidate.op->getOperation()->calculateOperationTime(
        candidate.op, start, dur, true);
    start = range.getStart();
    end = range.getEnd();
  } else {
    end = start + dur;
  }

  for (auto* r : candidate.allResources) {
    nextPartial.sequences[r].push_back(candidate.op);
    nextPartial.startDates[r].push_back(start);
    nextPartial.endDates[r].push_back(end);
    nextPrev[r] = candidate.op;
    nextTime[r] = end;
  }
  nextPartial.selectedResources[candidate.op] = candidate.allResources;
  nextPartial.selectedLoadAssignments[candidate.op] = candidate.loadAssignments;

  if (state.scheduled.count(candidate.op)) return false;
  return true;
}

AntSolution SolverExact::exactSearch(
    const vector<const Resource*>& resources,
    const vector<CandidateOp>& candidates) {
  ExactSearchState state;
  for (auto* r : resources) {
    state.prevOp[r] = nullptr;
    state.curTime[r] = Plan::instance().getCurrent();
  }
  state.partial.fitness = -numeric_limits<double>::max();
  state.best.fitness = -numeric_limits<double>::max();

  if (candidates.size() > static_cast<size_t>(config_.max_operations)) {
    state.aborted = true;
    return state.best;
  }

  unordered_set<const OperationPlan*> uniqueOps;
  for (const auto& c : candidates)
    if (c.op) uniqueOps.insert(c.op);
  size_t targetCount = uniqueOps.size();
  branch(resources, candidates, targetCount, state);
  if (state.aborted || state.best.fitness == -numeric_limits<double>::max())
    return state.best;
  return state.best;
}

AntSolution SolverExact::solveTimeIndexedMIP(
    const vector<const Resource*>& resources,
    const vector<CandidateOp>& candidates) {
  AntSolution empty;
  empty.fitness = -numeric_limits<double>::max();

#ifndef HAVE_ORTOOLS
  return empty;
#else
  if (resources.empty() || candidates.empty()) return empty;
  if (candidates.size() > static_cast<size_t>(config_.max_operations))
    return empty;
  if (getLogLevel() > 0)
    logger << indentlevel << "Exact MIP: solving " << candidates.size()
           << " candidates on " << resources.size() << " resources\n";
  int bucketSeconds = max(1, config_.time_bucket_seconds);
  Date current = Plan::instance().getCurrent();

  unordered_set<const OperationPlan*> uniqueOps;
  Date horizonEnd = current;
  for (const auto& c : candidates) {
    if (!c.op || c.allResources.empty()) continue;
    uniqueOps.insert(c.op);
    const Resource* durationResource = c.res ? c.res : c.allResources.front();
    Duration dur = estimateOperationDuration(c.op, durationResource);
    Date due = Date::infiniteFuture;
    Demand* dmd = c.op->getTopOwner()->getDemand();
    if (dmd) due = dmd->getDue();
    Date candidateEnd = c.earliestStart + dur;
    if (due != Date::infiniteFuture && due > candidateEnd) candidateEnd = due;
    candidateEnd += Duration(bucketSeconds * 4L);
    if (candidateEnd > horizonEnd) horizonEnd = candidateEnd;
  }
  if (uniqueOps.empty() || horizonEnd <= current) return empty;

  long horizonSeconds = (horizonEnd - current).getSeconds();
  int bucketCount = static_cast<int>(
      min<long>(max<long>(1, horizonSeconds / bucketSeconds + 1), 240L));

  MPSolver solver("ccapps_exact_time_indexed",
                  MPSolver::SCIP_MIXED_INTEGER_PROGRAMMING);
  if (config_.mip_time_limit_seconds > 0)
    solver.SetTimeLimit(absl::Seconds(config_.mip_time_limit_seconds));
  const double infinity = solver.infinity();

  struct VarSlot {
    const CandidateOp* candidate = nullptr;
    int startBucket = 0;
    int durationBuckets = 1;
    Date start;
    Date end;
    MPVariable* var = nullptr;
  };
  vector<VarSlot> slots;
  unordered_map<const OperationPlan*, vector<int>> opSlots;
  unordered_map<const Resource*, vector<vector<int>>> resourceBuckets;
  for (auto* r : resources) resourceBuckets[r].resize(bucketCount);

  for (const auto& c : candidates) {
    if (!c.op || c.allResources.empty()) continue;
    const Resource* durationResource = c.res ? c.res : c.allResources.front();
    Duration dur = estimateOperationDuration(c.op, durationResource);
    int durationBuckets = max(
        1, static_cast<int>((dur.getSeconds() + bucketSeconds - 1) /
                            bucketSeconds));
    long earliestOffset = max<long>(0, (c.earliestStart - current).getSeconds());
    int earliestBucket = static_cast<int>(
        min<long>(bucketCount - 1, earliestOffset / bucketSeconds));

    for (int b = earliestBucket; b + durationBuckets <= bucketCount; ++b) {
      Date rawStart = current + Duration(static_cast<long>(b) * bucketSeconds);
      Date start = rawStart;
      Date end = rawStart + dur;
      if (c.op->getOperation()) {
        DateRange range = c.op->getOperation()->calculateOperationTime(
            c.op, rawStart, dur, true);
        start = range.getStart();
        end = range.getEnd();
      }
      if (start == Date::infiniteFuture || end == Date::infiniteFuture)
        continue;
      VarSlot slot;
      slot.candidate = &c;
      slot.startBucket = b;
      slot.durationBuckets = durationBuckets;
      slot.start = start;
      slot.end = end;
      slot.var = solver.MakeBoolVar("x");
      int idx = static_cast<int>(slots.size());
      slots.push_back(slot);
      opSlots[c.op].push_back(idx);
      for (auto* r : c.allResources) {
        if (!resourceBuckets.count(r)) resourceBuckets[r].resize(bucketCount);
        for (int k = b; k < b + durationBuckets && k < bucketCount; ++k)
          resourceBuckets[r][k].push_back(idx);
      }
    }
  }
  if (slots.empty()) return empty;

  for (auto* op : uniqueOps) {
    auto it = opSlots.find(op);
    if (it == opSlots.end() || it->second.empty()) return empty;
    MPConstraint* once = solver.MakeRowConstraint(1.0, 1.0);
    for (int idx : it->second) once->SetCoefficient(slots[idx].var, 1.0);
  }

  for (const auto& kv : resourceBuckets) {
    for (int k = 0; k < bucketCount; ++k) {
      if (kv.second[k].empty()) continue;
      MPConstraint* cap = solver.MakeRowConstraint(0.0, 1.0);
      for (int idx : kv.second[k]) cap->SetCoefficient(slots[idx].var, 1.0);
    }
  }

  MPObjective* objective = solver.MutableObjective();
  objective->SetMinimization();
  for (size_t i = 0; i < slots.size(); ++i) {
    const CandidateOp* c = slots[i].candidate;
    const Resource* res = c->res ? c->res : c->allResources.front();
    double priorityFactor = 1.0;
    Demand* dmd = c->op->getTopOwner()->getDemand();
    if (dmd) {
      priorityFactor += config_.weight_priority /
          (1.0 + static_cast<double>(dmd->getPriority()));
    }
    double tardiness = 0.0;
    if (dmd && dmd->getDue() != Date::infiniteFuture &&
        slots[i].end > dmd->getDue())
      tardiness = static_cast<double>(
          (slots[i].end - dmd->getDue()).getSeconds()) / 3600.0;
    double durationHours = static_cast<double>(
        (slots[i].end - slots[i].start).getSeconds()) / 3600.0;
    double cost = c->op->getOperation()->getCost() * c->op->getQuantity() +
                  res->getCost() * max(0.0, durationHours);
    objective->SetCoefficient(
        slots[i].var,
        config_.weight_tardiness * tardiness * priorityFactor +
            config_.weight_cost * cost);
  }

  MPSolver::ResultStatus status = solver.Solve();
  if (status != MPSolver::OPTIMAL && status != MPSolver::FEASIBLE)
    return empty;

  AntSolution result;
  unordered_set<const OperationPlan*> scheduled;
  for (const auto& slot : slots) {
    if (slot.var->solution_value() < 0.5) continue;
    const CandidateOp* c = slot.candidate;
    if (!c || !c->op || scheduled.count(c->op)) continue;
    for (auto* r : c->allResources) {
      result.sequences[r].push_back(c->op);
      result.startDates[r].push_back(slot.start);
      result.endDates[r].push_back(slot.end);
    }
    result.selectedResources[c->op] = c->allResources;
    result.selectedLoadAssignments[c->op] = c->loadAssignments;
    scheduled.insert(c->op);
  }
  result.fitness = evaluate(result);
  return result;
#endif
}

void SolverExact::solveWithAcoFallback(void* v) {
  if (!config_.fallback_to_aco) return;
  SolverACO::solve(v);
}

void SolverExact::solve(const Resource* res, void* v) {
  vector<OperationPlan*> plans;
  auto loadplans = res->getLoadPlans();
  for (auto it = loadplans.begin(); it != loadplans.end(); ++it) {
    OperationPlan* op = it->getOperationPlan();
    if (op && op->getQuantity() > 0.0 &&
        find(plans.begin(), plans.end(), op) == plans.end())
      plans.push_back(op);
  }
  if (plans.size() < 2) {
    SolverCreate::solve(res, v);
    return;
  }

  vector<const Resource*> resources{res};
  vector<CandidateOp> candidates = buildSingleResourceCandidates(res, plans);
  if (candidates.size() > static_cast<size_t>(config_.max_operations)) {
    solveWithAcoFallback(v);
    return;
  }
  AntSolution best;
  best.fitness = -numeric_limits<double>::max();
  if (config_.prefer_mip) best = solveTimeIndexedMIP(resources, candidates);
  if (best.fitness == -numeric_limits<double>::max())
    best = exactSearch(resources, candidates);
  if (best.fitness == -numeric_limits<double>::max()) {
    solveWithAcoFallback(v);
    return;
  }
  applyBestSolution(best);
  if (getLogLevel() > 0)
    logger << indentlevel << "Exact on '" << res->getName()
           << "': " << plans.size() << " plans, fitness=" << best.fitness
           << "\n";
}

void SolverExact::solveJoint(const vector<const Resource*>& resources) {
  if (resources.empty()) return;
  vector<CandidateOp> candidates = buildCandidates(resources);
  if (candidates.size() < 2) return;
  if (candidates.size() > static_cast<size_t>(config_.max_operations)) {
    solveWithAcoFallback(nullptr);
    return;
  }
  AntSolution best;
  best.fitness = -numeric_limits<double>::max();
  if (config_.prefer_mip) best = solveTimeIndexedMIP(resources, candidates);
  if (best.fitness == -numeric_limits<double>::max())
    best = exactSearch(resources, candidates);
  if (best.fitness == -numeric_limits<double>::max()) {
    solveWithAcoFallback(nullptr);
    return;
  }
  applyBestSolution(best);
  if (getLogLevel() > 0)
    logger << indentlevel << "Exact joint: " << resources.size()
           << " resources, " << candidates.size()
           << " candidates, fitness=" << best.fitness << "\n";
}

void SolverExact::solve(void* v) {
  syncACOConfig();
  vector<const Resource*> bottlenecks = collectBottlenecks();
  if (getLogLevel() >= 0)
    logger << indentlevel << "Exact: entry, found " << bottlenecks.size()
           << " bottleneck resources\n";

  if (bottlenecks.empty()) {
    if (config_.runMRP) SolverCreate::solve(v);
    return;
  }

  if (bottlenecks.size() >= 2 && config_.joint_optimization)
    solveJoint(bottlenecks);
  else if (bottlenecks.size() == 1)
    solve(bottlenecks[0], v);

  if (config_.runMRP) SolverCreate::solve(v);
  for (auto op = OperationPlan::begin(); op != OperationPlan::end(); ++op)
    op->setAcoLocked(false);
}

}  // namespace ccAPPS
