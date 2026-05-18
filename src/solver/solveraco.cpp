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
#include <cfloat>

#include "ccAPPS/solveraco.h"

namespace ccAPPS {

const MetaClass* SolverACO::metadata;

// ==========================================================================
// PheromoneMatrix implementation
// ==========================================================================

double PheromoneMatrix::get(const OperationPlan* from,
                            const OperationPlan* to) const {
  auto it = matrix_.find({from, to});
  if (it != matrix_.end()) return it->second;
  return 0.0;
}

void PheromoneMatrix::set(const OperationPlan* from, const OperationPlan* to,
                          double value) {
  matrix_[{from, to}] = value;
}

void PheromoneMatrix::evaporate(double rho) {
  for (auto& kv : matrix_) kv.second *= (1.0 - rho);
}

void PheromoneMatrix::deposit(const AntSolution& ant, double amount) {
  const OperationPlan* prev = nullptr;
  for (const auto& op : ant.sequence) {
    auto key = make_pair(prev, op);
    auto it = matrix_.find(key);
    if (it != matrix_.end())
      it->second += amount;
    else
      matrix_[key] = amount;
    prev = op;
  }
}

void PheromoneMatrix::reset(double /* tau0 */) { matrix_.clear(); }

// ==========================================================================
// SolverACO initialization
// ==========================================================================

static PyObject* solverACO_initPheromone(PyObject* self, PyObject* args) {
  auto* solver = static_cast<SolverACO*>(self);
  PyObject* py_resource = nullptr;
  if (!PyArg_ParseTuple(args, "O", &py_resource)) return nullptr;

  // Validate that it's a Resource by checking its Python type name
  if (!py_resource || !PyObject_HasAttrString(py_resource, "getName")) {
    PyErr_SetString(PyExc_TypeError, "Expected a Resource object");
    return nullptr;
  }
  // We trust the user; cast and use
  auto* res = static_cast<Resource*>(
      PyCapsule_GetPointer(py_resource, "ccAPPS.Resource"));
  if (!res) {
    PyErr_SetString(PyExc_TypeError, "Expected a Resource capsule");
    return nullptr;
  }
  solver->initPheromone(res);
  Py_RETURN_NONE;
}

int SolverACO::initialize() {
  metadata = MetaClass::registerClass<SolverACO>("solver", "solver_aco",
                                                  Object::create<SolverACO>);
  registerFields<SolverACO>(const_cast<MetaClass*>(metadata));

  auto& x = PythonExtension<SolverACO>::getPythonType();
  x.setName("solverACO");
  x.setDoc("ccAPPS ant colony optimization solver");
  x.supportgetattro();
  x.supportsetattro();
  x.supportcreate(SolverACO::create);
  x.addMethod("initPheromone", solverACO_initPheromone, METH_VARARGS,
              "Initialize pheromone matrix for a resource");

  SolverACO::metadata->setPythonClass(x);
  return x.typeReady();
}

PyObject* SolverACO::create(PyTypeObject*, PyObject*, PyObject*) {
  return Object::create<SolverACO>();
}

// ==========================================================================
// Helper: compute setup time between two operationplans
//
// Note: Setup time is determined by the operation's setup matrix on the
// resource they share. This implementation uses the setup value strings
// from the operationplans' loadplans to look up the matrix.
// ==========================================================================

Duration SolverACO::computeSetupTime(const OperationPlan* from,
                                     const OperationPlan* to) const {
  if (!from || !to) return Duration(0L);
  if (from->getOperation() == to->getOperation()) return Duration(0L);

  // Get setup values from the loadplans for the first load pointing to
  // the same constrained resource
  PooledString fromSetup;
  PooledString toSetup;
  const SetupMatrix* matrix = nullptr;

  for (auto fl = from->beginLoadPlans(); fl != from->endLoadPlans(); ++fl) {
    if (fl->isStart() && fl->getResource() &&
        fl->getResource()->getConstrained()) {
      fromSetup = fl->getSetupLoad();
      matrix = fl->getResource()->getSetupMatrix();
      break;
    }
  }
  // Find matching setup on 'to'
  for (auto tl = to->beginLoadPlans(); tl != to->endLoadPlans(); ++tl) {
    if (tl->isStart() && tl->getResource() &&
        tl->getResource()->getConstrained()) {
      toSetup = tl->getSetupLoad();
      break;
    }
  }

  if (fromSetup.empty() && toSetup.empty()) return Duration(0L);
  if (!matrix) return Duration(0L);

  // Use the matrix's calculateSetup method
  SetupMatrixRule* rule = matrix->calculateSetup(fromSetup, toSetup);
  if (rule) return rule->getDuration();

  return Duration(0L);
}

// ==========================================================================
// Heuristic: attractiveness of transition from -> to
// ==========================================================================

double SolverACO::heuristic(const OperationPlan* from,
                            const OperationPlan* to) const {
  if (!to) return 0.0;

  double h = 1.0;

  // Factor 1: Setup time penalty (shorter setup = higher heuristic)
  if (from) {
    Duration setup = computeSetupTime(from, to);
    if (setup > Duration(0L))
      h *= 1.0 / (1.0 + static_cast<double>(setup.getSeconds()) / 3600.0);
  }

  // Factor 2: Priority alignment
  double priority = to->getOperation()->getPriority();
  if (priority > 0.0) h *= (1.0 + priority * 0.1);

  // Factor 3: Due date urgency
  Demand* dmd = to->getTopOwner()->getDemand();
  if (dmd && dmd->getDue() != Date::infiniteFuture) {
    auto urgency =
        static_cast<double>(
            (dmd->getDue() - Plan::instance().getCurrent()).getSeconds()) /
        86400.0;
    if (urgency > 0.0)
      h *= 1.0 / (1.0 + urgency);
    else
      h *= 10.0;
  }

  return max(h, 1e-6);
}

// ==========================================================================
// Estimate duration of an operationplan on a resource
// ==========================================================================

Duration estimateOperationDuration(const OperationPlan* op) {
  Date fence = op->getOperation()->getFence(op);
  if (fence != Date::infiniteFuture) {
    Duration d = fence - Plan::instance().getCurrent();
    if (d > Duration(0L)) return d;
  }

  return Duration(static_cast<long>(max(3600.0, op->getQuantity() * 3600.0)));
}

// ==========================================================================
// Construct a single ant's solution
// ==========================================================================

AntSolution SolverACO::constructSolution(
    const Resource* res, const vector<OperationPlan*>& plans) {
  AntSolution ant;
  if (plans.empty()) return ant;

  vector<OperationPlan*> unvisited = plans;
  sort(unvisited.begin(), unvisited.end(),
       [](OperationPlan* a, OperationPlan* b) {
         return a->getOperation()->getPriority() <
                b->getOperation()->getPriority();
       });

  const OperationPlan* current = nullptr;
  Date current_time = Plan::instance().getCurrent();

  while (!unvisited.empty()) {
    vector<double> probabilities(unvisited.size(), 0.0);
    double total_prob = 0.0;

    for (size_t i = 0; i < unvisited.size(); ++i) {
      double tau = pheromones_[res].get(current, unvisited[i]);
      if (tau <= 0.0) tau = config_.tau0;
      double eta = heuristic(current, unvisited[i]);

      probabilities[i] = pow(tau, config_.alpha) * pow(eta, config_.beta);
      total_prob += probabilities[i];
    }

    // Roulette wheel selection
    double rand_val =
        uniform_real_distribution<double>(0.0, total_prob)(rng_);
    double cumulative = 0.0;
    size_t selected = unvisited.size() - 1;

    for (size_t i = 0; i < unvisited.size(); ++i) {
      cumulative += probabilities[i];
      if (cumulative >= rand_val) {
        selected = i;
        break;
      }
    }

    OperationPlan* next_op = unvisited[selected];

    Duration setup = computeSetupTime(current, next_op);
    Duration opplan_duration = estimateOperationDuration(next_op);
    Date start_date = current_time + setup;
    Date end_date = start_date + opplan_duration;

    ant.sequence.push_back(next_op);
    ant.startDates.push_back(start_date);
    ant.endDates.push_back(end_date);

    unvisited.erase(unvisited.begin() + selected);

    current = next_op;
    current_time = end_date;
  }

  return ant;
}

// ==========================================================================
// Local search: 2-opt improvement
// ==========================================================================

void SolverACO::localSearch(const Resource* res, AntSolution& solution) {
  if (solution.sequence.size() < 2) return;

  bool improved = true;
  while (improved) {
    improved = false;
    for (size_t i = 0; i < solution.sequence.size() - 1; ++i) {
      for (size_t j = i + 1; j < solution.sequence.size(); ++j) {
        AntSolution candidate = solution;
        swap(candidate.sequence[i], candidate.sequence[j]);

        Date current_time = Plan::instance().getCurrent();
        const OperationPlan* prev = nullptr;
        for (size_t k = 0; k < candidate.sequence.size(); ++k) {
          Duration setup = computeSetupTime(prev, candidate.sequence[k]);
          Duration dur = estimateOperationDuration(candidate.sequence[k]);
          candidate.startDates[k] = current_time + setup;
          candidate.endDates[k] = candidate.startDates[k] + dur;
          prev = candidate.sequence[k];
          current_time = candidate.endDates[k];
        }

        double new_fitness = evaluate(res, candidate);
        if (new_fitness > solution.fitness) {
          solution = candidate;
          solution.fitness = new_fitness;
          improved = true;
        }
      }
    }
  }
}

// ==========================================================================
// Evaluate a solution's quality
// ==========================================================================

double SolverACO::evaluate(const Resource* /* res */,
                           const AntSolution& solution) {
  if (solution.sequence.empty()) return 0.0;

  double total_tardiness = 0.0;
  double total_cost = 0.0;
  double total_setup = 0.0;

  const OperationPlan* prev = nullptr;
  for (size_t i = 0; i < solution.sequence.size(); ++i) {
    const OperationPlan* op = solution.sequence[i];

    Demand* dmd = op->getTopOwner()->getDemand();
    if (dmd && dmd->getDue() != Date::infiniteFuture) {
      if (solution.endDates[i] > dmd->getDue())
        total_tardiness +=
            static_cast<double>(
                (solution.endDates[i] - dmd->getDue()).getSeconds()) /
            3600.0;
    }

    total_cost += op->getOperation()->getCost() * op->getQuantity();

    if (prev) {
      total_setup +=
          static_cast<double>(computeSetupTime(prev, op).getSeconds()) /
          3600.0;
    }

    prev = op;
  }

  return -(config_.weight_tardiness * total_tardiness +
           config_.weight_cost * total_cost +
           config_.weight_setup * total_setup);
}

// ==========================================================================
// Pheromone management
// ==========================================================================

void SolverACO::initPheromone(const Resource* res) {
  pheromones_[res].reset(config_.tau0);
}

const PheromoneMatrix* SolverACO::getPheromone(const Resource* res) const {
  auto it = pheromones_.find(res);
  if (it != pheromones_.end()) return &it->second;
  return nullptr;
}

// ==========================================================================
// Apply the best solution to the actual plan
// ==========================================================================

void SolverACO::applyBestSolution(const Resource* /* res */,
                                  const AntSolution& best) {
  if (best.sequence.empty()) return;

  for (size_t i = 0; i < best.sequence.size(); ++i) {
    OperationPlan* op = best.sequence[i];
    if (best.startDates[i] != Date::infiniteFuture &&
        best.endDates[i] != Date::infiniteFuture) {
      op->setStart(best.startDates[i], false, false);
    }
  }
}

// ==========================================================================
// Collect operationplans assigned to a resource
// ==========================================================================

static void collectResourcePlans(const Resource* res,
                                 vector<OperationPlan*>& plans) {
  auto loadplans = res->getLoadPlans();
  for (auto it = loadplans.begin(); it != loadplans.end(); ++it) {
    OperationPlan* op = it->getOperationPlan();
    if (op && op->getQuantity() > 0.0) {
      bool found = false;
      for (const auto& existing : plans)
        if (existing == op) {
          found = true;
          break;
        }
      if (!found) plans.push_back(op);
    }
  }
}

// ==========================================================================
// Resource-level ACO solver
// ==========================================================================

void SolverACO::solve(const Resource* res, void* v) {
  // Check if this resource is ACO-enabled
  bool acoEnabled = false;
  for (const auto& load : res->getLoads()) {
    if (load.getOperation() && load.getSearch() == SearchMode::ACO) {
      acoEnabled = true;
      break;
    }
  }

  if (!acoEnabled) {
    SolverCreate::solve(res, v);
    return;
  }

  vector<OperationPlan*> plans;
  collectResourcePlans(res, plans);

  if (plans.size() < 2) {
    SolverCreate::solve(res, v);
    return;
  }

  if (pheromones_.find(res) == pheromones_.end()) initPheromone(res);

  PheromoneMatrix& phero = pheromones_[res];

  AntSolution globalBest;
  globalBest.fitness = -numeric_limits<double>::max();
  int stagnationCounter = 0;

  for (int iter = 0; iter < config_.iterations; ++iter) {
    vector<AntSolution> antSolutions(config_.ants);

    for (int a = 0; a < config_.ants; ++a) {
      antSolutions[a] = constructSolution(res, plans);
      antSolutions[a].fitness = evaluate(res, antSolutions[a]);
      localSearch(res, antSolutions[a]);
    }

    sort(antSolutions.begin(), antSolutions.end(),
         [](const AntSolution& a, const AntSolution& b) {
           return a.fitness > b.fitness;
         });

    if (antSolutions[0].fitness > globalBest.fitness) {
      globalBest = antSolutions[0];
      stagnationCounter = 0;
    } else {
      ++stagnationCounter;
    }

    phero.evaporate(config_.evaporation);

    phero.deposit(antSolutions[0],
                  config_.Q * (1.0 + max(0.0, antSolutions[0].fitness) * 0.01));

    for (int e = 0; e < config_.elite_ants && e < config_.ants; ++e) {
      phero.deposit(
          antSolutions[e],
          config_.Q * 0.5 * (1.0 + max(0.0, antSolutions[e].fitness) * 0.01));
    }

    if (stagnationCounter >= config_.stagnation_limit) {
      if (getLogLevel() > 1)
        logger << indentlevel << "ACO: Stopped early after " << (iter + 1)
               << " iterations (stagnation)\n";
      break;
    }
  }

  bestSolutions_[res] = globalBest;
  applyBestSolution(res, globalBest);

  if (getLogLevel() > 0) {
    logger << indentlevel << "ACO on resource '" << res->getName()
           << "': " << plans.size() << " plans sequenced, fitness="
           << globalBest.fitness << "\n";
  }
}

// ==========================================================================
// Top-level solve: process all resources
// ==========================================================================

void SolverACO::solve(void* v) {
  for (auto& res : Resource::all()) {
    if (res.getConstrained()) solve(&res, v);
  }
  SolverCreate::solve(v);
}

}  // namespace ccAPPS
