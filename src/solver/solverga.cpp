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
#include <unordered_set>

#include "ccAPPS/solverga.h"

namespace ccAPPS {

const MetaClass* SolverGA::metadata;

// ==========================================================================
// Python bindings
// ==========================================================================

static PyObject* solverGA_setRunMRP(PyObject* self, PyObject* args) {
  auto* solver = static_cast<SolverGA*>(self);
  int val;
  if (!PyArg_ParseTuple(args, "p", &val)) return nullptr;
  solver->setRunMRP(val != 0);
  Py_RETURN_NONE;
}

static PyObject* solverGA_getRunMRP(PyObject* self, PyObject*) {
  auto* solver = static_cast<SolverGA*>(self);
  return PyBool_FromLong(solver->getRunMRP() ? 1 : 0);
}

static PyObject* solverGA_setPurchaseMaterialMode(
    PyObject* self, PyObject* args) {
  auto* solver = static_cast<SolverGA*>(self);
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

static PyObject* solverGA_getPurchaseMaterialMode(PyObject* self, PyObject*) {
  auto* solver = static_cast<SolverGA*>(self);
  return PyLong_FromLong(solver->getPurchaseMaterialMode());
}

PyObject* run_ga(PyObject*, PyObject*) {
  SolverGA ga;
  ga.setRunMRP(false);
  void* v = nullptr;
  ga.solve(v);
  auto* cmdMgr = ga.getCommandManager();
  if (cmdMgr) cmdMgr->commit();
  Py_RETURN_NONE;
}

int SolverGA::initialize() {
  metadata = MetaClass::registerClass<SolverGA>("solver", "solver_ga",
                                                Object::create<SolverGA>);
  registerFields<SolverGA>(const_cast<MetaClass*>(metadata));
  auto& x = PythonExtension<SolverGA>::getPythonType();
  x.setName("solverGA");
  x.setDoc("ccAPPS genetic algorithm solver");
  x.supportgetattro();
  x.supportsetattro();
  x.supportcreate(SolverGA::create);

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

  x.addMethod("setRunMRP", solverGA_setRunMRP, METH_VARARGS,
              "Enable/disable MRP propagation after GA sequencing");
  x.addMethod("getRunMRP", solverGA_getRunMRP, METH_NOARGS,
              "Check if MRP propagation is enabled");
  x.addMethod("setPurchaseMaterialMode", solverGA_setPurchaseMaterialMode,
              METH_VARARGS,
              "Set GA purchase material mode: 0=infinite, 1=leadtime");
  x.addMethod("getPurchaseMaterialMode", solverGA_getPurchaseMaterialMode,
              METH_NOARGS, "Get GA purchase material mode");

  SolverGA::metadata->setPythonClass(x);
  return x.typeReady();
}

PyObject* SolverGA::create(PyTypeObject*, PyObject*, PyObject*) {
  return Object::create<SolverGA>();
}

// ==========================================================================
// Configuration bridge to reused ACO evaluator/helpers
// ==========================================================================

void SolverGA::setConfig(const GAConfig& cfg) {
  config_ = cfg;
  syncACOConfig();
}

void SolverGA::syncACOConfig() {
  ACOConfig acoCfg = getConfig();
  acoCfg.runMRP = config_.runMRP;
  acoCfg.joint_optimization = config_.joint_optimization;
  acoCfg.aco_mrp_iterations = config_.ga_mrp_iterations;
  acoCfg.aco_mrp_improvement = config_.ga_mrp_improvement;
  acoCfg.purchase_material_mode = config_.purchase_material_mode;
  acoCfg.weight_tardiness = config_.weight_tardiness;
  acoCfg.weight_cost = config_.weight_cost;
  acoCfg.weight_setup = config_.weight_setup;
  acoCfg.weight_priority = config_.weight_priority;
  acoCfg.weight_balance = config_.weight_balance;
  SolverACO::setConfig(acoCfg);
}

// ==========================================================================
// Candidate and chromosome helpers
// ==========================================================================

vector<const Resource*> SolverGA::collectBottlenecks() const {
  vector<const Resource*> result;
  unordered_set<const Resource*> activeGroups;

  auto collectResourcePlansLocal = [](const Resource* res) {
    vector<OperationPlan*> plans;
    auto loadplans = res->getLoadPlans();
    for (auto it = loadplans.begin(); it != loadplans.end(); ++it) {
      OperationPlan* op = it->getOperationPlan();
      if (op && op->getQuantity() > 0.0 &&
          find(plans.begin(), plans.end(), op) == plans.end())
        plans.push_back(op);
    }
    return plans;
  };

  for (auto res = Resource::begin(); res != Resource::end(); ++res) {
    if (!res->getConstrained()) continue;
    if (res->isGroup()) {
      for (auto m = res->getMembers(); m != Resource::end(); ++m) {
        if (!m->isGroup() && m->getConstrained() &&
            collectResourcePlansLocal(&*m).size() >= 2) {
          result.push_back(&*m);
          activeGroups.insert(&*res);
        }
      }
    } else if (collectResourcePlansLocal(&*res).size() >= 2) {
      result.push_back(&*res);
    }
  }

  for (auto* group : activeGroups) {
    for (auto m = group->getMembers(); m != Resource::end(); ++m) {
      if (m->isGroup() || !m->getConstrained()) continue;
      if (find(result.begin(), result.end(), &*m) == result.end())
        result.push_back(&*m);
    }
  }
  return result;
}

vector<CandidateOp> SolverGA::buildSingleResourceCandidates(
    const Resource* res,
    const vector<OperationPlan*>& plans) const {
  vector<CandidateOp> candidates;
  int id = 0;
  for (auto* op : plans) {
    CandidateOp c;
    c.op = op;
    c.res = res;
    c.earliestStart = earliestStart(op, res);
    c.candId = id++;
    c.allResources.push_back(res);
    candidates.push_back(c);
  }
  return candidates;
}

SolverGA::CandidateBuckets SolverGA::groupCandidates(
    const vector<CandidateOp>& candidates) const {
  CandidateBuckets buckets;
  for (const auto& candidate : candidates)
    if (candidate.op) buckets[candidate.candId].push_back(&candidate);
  return buckets;
}

GAChromosome SolverGA::randomChromosome(const CandidateBuckets& buckets) {
  GAChromosome chromosome;
  chromosome.order.reserve(buckets.size());
  for (const auto& kv : buckets) {
    chromosome.order.push_back(kv.first);
    size_t assignment = 0;
    if (kv.second.size() > 1)
      assignment = uniform_int_distribution<size_t>(
          0, kv.second.size() - 1)(rng_);
    chromosome.assignment[kv.first] = assignment;
  }
  shuffle(chromosome.order.begin(), chromosome.order.end(), rng_);
  return chromosome;
}

GAChromosome SolverGA::seedPriorityChromosome(const CandidateBuckets& buckets) {
  GAChromosome chromosome = randomChromosome(buckets);
  sort(chromosome.order.begin(), chromosome.order.end(),
       [&](int left, int right) {
         const auto* l = buckets.at(left).front()->op;
         const auto* r = buckets.at(right).front()->op;
         Demand* ld = l->getTopOwner()->getDemand();
         Demand* rd = r->getTopOwner()->getDemand();
         int lp = ld ? ld->getPriority() : 999;
         int rp = rd ? rd->getPriority() : 999;
         if (lp != rp) return lp < rp;
         Date ldue = ld ? ld->getDue() : Date::infiniteFuture;
         Date rdue = rd ? rd->getDue() : Date::infiniteFuture;
         return ldue < rdue;
       });
  return chromosome;
}

// ==========================================================================
// Decode chromosome into a concrete schedule
// ==========================================================================

AntSolution SolverGA::decodeChromosome(
    const GAChromosome& chromosome,
    const vector<const Resource*>& resources,
    const CandidateBuckets& buckets,
    const unordered_map<const Resource*, Date>& resourceTimes) {
  AntSolution solution;
  unordered_map<const Resource*, Date> curTime;
  unordered_map<const Resource*, const OperationPlan*> prevOp;
  unordered_set<const OperationPlan*> scheduledOps;

  for (auto* res : resources) {
    auto it = resourceTimes.find(res);
    curTime[res] = it != resourceTimes.end()
        ? it->second : Plan::instance().getCurrent();
    prevOp[res] = nullptr;
  }

  for (const auto& kv : buckets) {
    for (auto* c : kv.second) {
      for (auto* res : c->allResources) {
        if (!curTime.count(res)) {
          auto it = resourceTimes.find(res);
          curTime[res] = it != resourceTimes.end()
              ? it->second : Plan::instance().getCurrent();
        }
        if (!prevOp.count(res)) prevOp[res] = nullptr;
      }
    }
  }

  for (int candId : chromosome.order) {
    auto bucketIt = buckets.find(candId);
    if (bucketIt == buckets.end() || bucketIt->second.empty()) continue;
    size_t assignment = 0;
    auto assignIt = chromosome.assignment.find(candId);
    if (assignIt != chromosome.assignment.end())
      assignment = min(assignIt->second, bucketIt->second.size() - 1);
    const CandidateOp& chosen = *bucketIt->second[assignment];
    if (!chosen.op || scheduledOps.count(chosen.op) ||
        chosen.allResources.empty())
      continue;

    Date rawStart = chosen.earliestStart;
    for (auto* res : chosen.allResources) {
      Duration setup = computeSetupTime(prevOp[res], chosen.op);
      rawStart = max(rawStart, curTime[res] + setup);
    }
    for (auto* res : chosen.allResources)
      rawStart = max(rawStart, earliestStart(chosen.op, res));

    const Resource* durationResource = chosen.res ? chosen.res
        : chosen.allResources.front();
    Duration dur = estimateOperationDuration(chosen.op, durationResource);
    Date start;
    Date end;
    if (chosen.op->getOperation()) {
      DateRange range = chosen.op->getOperation()->calculateOperationTime(
          chosen.op, rawStart, dur, true);
      start = range.getStart();
      end = range.getEnd();
    } else {
      start = rawStart;
      end = rawStart + dur;
    }

    for (auto* res : chosen.allResources) {
      solution.sequences[res].push_back(chosen.op);
      solution.startDates[res].push_back(start);
      solution.endDates[res].push_back(end);
      prevOp[res] = chosen.op;
      curTime[res] = end;
    }
    solution.selectedResources[chosen.op] = chosen.allResources;
    solution.selectedLoadAssignments[chosen.op] = chosen.loadAssignments;
    scheduledOps.insert(chosen.op);
  }

  size_t totalOps = buckets.size();
  solution.unscheduledCount =
      totalOps > scheduledOps.size() ? totalOps - scheduledOps.size() : 0;
  if (solution.unscheduledCount > 0)
    solution.unscheduledPenalty = 500.0 * solution.unscheduledCount;
  return solution;
}

void SolverGA::evaluateChromosome(
    GAChromosome& chromosome,
    const vector<const Resource*>& resources,
    const CandidateBuckets& buckets,
    const unordered_map<const Resource*, Date>& resourceTimes) {
  AntSolution solution =
      decodeChromosome(chromosome, resources, buckets, resourceTimes);
  compactSchedule(solution, resources, resourceTimes);
  solution.fitness = evaluate(solution);
  chromosome.fitness = solution.fitness;
}

GAChromosome SolverGA::tournamentSelect(const vector<GAChromosome>& population) {
  int size = max(1, config_.tournament_size);
  uniform_int_distribution<size_t> pick(0, population.size() - 1);
  const GAChromosome* best = &population[pick(rng_)];
  for (int i = 1; i < size; ++i) {
    const GAChromosome* candidate = &population[pick(rng_)];
    if (candidate->fitness > best->fitness) best = candidate;
  }
  return *best;
}

GAChromosome SolverGA::crossover(
    const GAChromosome& first,
    const GAChromosome& second,
    const CandidateBuckets& buckets) {
  if (first.order.size() < 2 || second.order.size() != first.order.size())
    return first;

  GAChromosome child;
  child.order.assign(first.order.size(), -1);
  uniform_int_distribution<size_t> cutPick(0, first.order.size() - 1);
  size_t left = cutPick(rng_);
  size_t right = cutPick(rng_);
  if (left > right) swap(left, right);

  unordered_set<int> used;
  for (size_t i = left; i <= right; ++i) {
    child.order[i] = first.order[i];
    used.insert(first.order[i]);
  }

  size_t write = (right + 1) % child.order.size();
  for (size_t i = 0; i < second.order.size(); ++i) {
    int gene = second.order[(right + 1 + i) % second.order.size()];
    if (used.count(gene)) continue;
    child.order[write] = gene;
    used.insert(gene);
    write = (write + 1) % child.order.size();
  }

  for (int gene : child.order) {
    size_t choice = 0;
    auto firstIt = first.assignment.find(gene);
    auto secondIt = second.assignment.find(gene);
    if (firstIt != first.assignment.end() &&
        secondIt != second.assignment.end()) {
      choice = uniform_int_distribution<int>(0, 1)(rng_) == 0
          ? firstIt->second : secondIt->second;
    } else if (firstIt != first.assignment.end()) {
      choice = firstIt->second;
    } else if (secondIt != second.assignment.end()) {
      choice = secondIt->second;
    }
    auto bucketIt = buckets.find(gene);
    if (bucketIt != buckets.end() && !bucketIt->second.empty())
      choice = min(choice, bucketIt->second.size() - 1);
    child.assignment[gene] = choice;
  }
  return child;
}

void SolverGA::mutate(GAChromosome& chromosome, const CandidateBuckets& buckets) {
  uniform_real_distribution<double> chance(0.0, 1.0);
  if (chromosome.order.size() > 1 && chance(rng_) < config_.mutation_rate) {
    uniform_int_distribution<size_t> pick(0, chromosome.order.size() - 1);
    size_t a = pick(rng_);
    size_t b = pick(rng_);
    if (a != b) swap(chromosome.order[a], chromosome.order[b]);
  }
  if (chromosome.order.size() > 2 && chance(rng_) < config_.inversion_rate) {
    uniform_int_distribution<size_t> pick(0, chromosome.order.size() - 1);
    size_t a = pick(rng_);
    size_t b = pick(rng_);
    if (a > b) swap(a, b);
    reverse(chromosome.order.begin() + a, chromosome.order.begin() + b + 1);
  }
  if (chance(rng_) < config_.mutation_rate) {
    vector<int> genes;
    for (const auto& kv : buckets)
      if (kv.second.size() > 1) genes.push_back(kv.first);
    if (!genes.empty()) {
      int gene = genes[uniform_int_distribution<size_t>(
          0, genes.size() - 1)(rng_)];
      chromosome.assignment[gene] =
          uniform_int_distribution<size_t>(0, buckets.at(gene).size() - 1)(rng_);
    }
  }
}

AntSolution SolverGA::evolve(
    const vector<const Resource*>& resources,
    const vector<CandidateOp>& candidates,
    const unordered_map<const Resource*, Date>& resourceTimes) {
  CandidateBuckets buckets = groupCandidates(candidates);
  AntSolution bestSolution;
  if (buckets.empty()) return bestSolution;

  vector<GAChromosome> population;
  population.reserve(config_.population);
  population.push_back(seedPriorityChromosome(buckets));
  while (static_cast<int>(population.size()) < max(1, config_.population))
    population.push_back(randomChromosome(buckets));

  for (auto& chromosome : population)
    evaluateChromosome(chromosome, resources, buckets, resourceTimes);

  GAChromosome best = *max_element(
      population.begin(), population.end(),
      [](const auto& a, const auto& b) { return a.fitness < b.fitness; });
  int stagnation = 0;
  uniform_real_distribution<double> chance(0.0, 1.0);

  for (int generation = 0; generation < config_.generations; ++generation) {
    sort(population.begin(), population.end(),
         [](const auto& a, const auto& b) { return a.fitness > b.fitness; });
    if (population.front().fitness > best.fitness) {
      best = population.front();
      stagnation = 0;
    } else {
      ++stagnation;
    }

    vector<GAChromosome> next;
    int eliteCount = min(config_.elite, static_cast<int>(population.size()));
    for (int i = 0; i < eliteCount; ++i) next.push_back(population[i]);

    while (next.size() < population.size()) {
      GAChromosome child;
      if (chance(rng_) < config_.random_immigrant_rate) {
        child = randomChromosome(buckets);
      } else {
        GAChromosome parentA = tournamentSelect(population);
        GAChromosome parentB = tournamentSelect(population);
        child = chance(rng_) < config_.crossover_rate
            ? crossover(parentA, parentB, buckets) : parentA;
        mutate(child, buckets);
      }
      evaluateChromosome(child, resources, buckets, resourceTimes);
      next.push_back(move(child));
    }
    population = move(next);

    if (stagnation >= config_.stagnation_limit) {
      stagnationOccurred_ = true;
      if (getLogLevel() > 1)
        logger << indentlevel << "GA: stopped after " << (generation + 1)
               << " generations (stagnation)\n";
      break;
    }
  }

  bestSolution = decodeChromosome(best, resources, buckets, resourceTimes);
  compactSchedule(bestSolution, resources, resourceTimes);
  bestSolution.fitness = evaluate(bestSolution);
  lastBestFitness_ = bestSolution.fitness;
  return bestSolution;
}

// ==========================================================================
// Solver entry points
// ==========================================================================

void SolverGA::solve(const Resource* res, void* v) {
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
  unordered_map<const Resource*, Date> resourceTimes;
  resourceTimes[res] = Plan::instance().getCurrent();
  vector<CandidateOp> candidates = buildSingleResourceCandidates(res, plans);
  AntSolution best = evolve(resources, candidates, resourceTimes);
  applyBestSolution(best);

  if (getLogLevel() > 0)
    logger << indentlevel << "GA on '" << res->getName()
           << "': " << plans.size() << " plans, fitness="
           << best.fitness << "\n";
}

void SolverGA::solveJoint(const vector<const Resource*>& resources) {
  if (resources.empty()) return;
  vector<CandidateOp> candidates = buildCandidates(resources);
  if (candidates.size() < 2) return;

  unordered_map<const Resource*, Date> resourceTimes;
  for (auto* res : resources)
    resourceTimes[res] = Plan::instance().getCurrent();

  AntSolution best = evolve(resources, candidates, resourceTimes);
  applyBestSolution(best);

  if (getLogLevel() > 0)
    logger << indentlevel << "GA joint: " << resources.size()
           << " resources, " << candidates.size()
           << " candidates, fitness=" << best.fitness << "\n";
}

void SolverGA::solve(void* v) {
  syncACOConfig();
  vector<const Resource*> bottlenecks = collectBottlenecks();

  if (getLogLevel() >= 0)
    logger << indentlevel << "GA: entry, found " << bottlenecks.size()
           << " bottleneck resources\n";

  if (bottlenecks.empty()) {
    if (config_.runMRP) SolverCreate::solve(v);
    return;
  }

  double prevFitness = -numeric_limits<double>::max();
  for (int outerIter = 0; outerIter < config_.ga_mrp_iterations; ++outerIter) {
    if (getLogLevel() > 1)
      logger << indentlevel << "GA↔MRP pass " << (outerIter + 1)
             << " of " << config_.ga_mrp_iterations << " ("
             << bottlenecks.size() << " bottleneck resources)\n";

    if (bottlenecks.size() >= 2 && config_.joint_optimization)
      solveJoint(bottlenecks);
    else if (bottlenecks.size() == 1)
      solve(bottlenecks[0], v);
    else
      break;

    if (config_.runMRP) SolverCreate::solve(v);

    if (outerIter > 0 && outerIter < config_.ga_mrp_iterations - 1) {
      double absDenom = max(abs(prevFitness), 1.0);
      double improvement = (lastBestFitness_ - prevFitness) / absDenom;
      if (improvement < config_.ga_mrp_improvement) {
        if (getLogLevel() > 1)
          logger << indentlevel << "GA↔MRP converged after "
                 << (outerIter + 1) << " passes (Δf/|f|="
                 << improvement << " < " << config_.ga_mrp_improvement
                 << ")\n";
        break;
      }
    }
    prevFitness = lastBestFitness_;

    if (outerIter >= config_.ga_mrp_iterations - 1) break;

    vector<const Resource*> newBottlenecks = collectBottlenecks();
    if (newBottlenecks.size() == bottlenecks.size() && stagnationOccurred_) {
      bool same = true;
      for (size_t i = 0; i < newBottlenecks.size(); ++i) {
        if (newBottlenecks[i] != bottlenecks[i]) {
          same = false;
          break;
        }
      }
      if (same) break;
    }
    bottlenecks = move(newBottlenecks);
  }
}

}  // namespace ccAPPS
