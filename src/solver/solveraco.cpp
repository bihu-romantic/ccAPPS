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
#include <unordered_set>

#include "ccAPPS/solveraco.h"

namespace ccAPPS {

const MetaClass* SolverACO::metadata;

// ==========================================================================
// PheromoneMatrix
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

void PheromoneMatrix::deposit(const vector<OperationPlan*>& sequence,
                              double amount) {
  const OperationPlan* prev = nullptr;
  for (const auto& op : sequence) {
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
// SolverACO Python init
// ==========================================================================

static PyObject* solverACO_initPheromone(PyObject* self, PyObject* args) {
  auto* solver = static_cast<SolverACO*>(self);
  PyObject* py_resource = nullptr;
  if (!PyArg_ParseTuple(args, "O", &py_resource)) return nullptr;
  if (!py_resource || !PyObject_HasAttrString(py_resource, "getName")) {
    PyErr_SetString(PyExc_TypeError, "Expected a Resource object");
    return nullptr;
  }
  auto* res = static_cast<Resource*>(
      PyCapsule_GetPointer(py_resource, "ccAPPS.Resource"));
  if (!res) { PyErr_SetString(PyExc_TypeError, "Expected a Resource capsule"); return nullptr; }
  solver->initPheromone(res);
  Py_RETURN_NONE;
}

static PyObject* solverACO_setRunMRP(PyObject* self, PyObject* args) {
  auto* solver = static_cast<SolverACO*>(self);
  int val;
  if (!PyArg_ParseTuple(args, "p", &val)) return nullptr;
  solver->setRunMRP(val != 0);
  Py_RETURN_NONE;
}

static PyObject* solverACO_getRunMRP(PyObject* self, PyObject*) {
  auto* solver = static_cast<SolverACO*>(self);
  return PyBool_FromLong(solver->getRunMRP() ? 1 : 0);
}

static PyObject* solverACO_setPurchaseMaterialMode(
    PyObject* self, PyObject* args) {
  auto* solver = static_cast<SolverACO*>(self);
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

static PyObject* solverACO_getPurchaseMaterialMode(PyObject* self, PyObject*) {
  auto* solver = static_cast<SolverACO*>(self);
  return PyLong_FromLong(solver->getPurchaseMaterialMode());
}

/* Global function: run ACO on constrained resources.
 * Called from Python as ccAPPS.run_aco(). */
PyObject* run_aco(PyObject*, PyObject*) {
  SolverACO aco;
  aco.setRunMRP(false);
  void* v = nullptr;
  aco.solve(v);
  // Persist ACO's schedule changes
  auto* cmdMgr = aco.getCommandManager();
  if (cmdMgr) cmdMgr->commit();
  Py_RETURN_NONE;
}

int SolverACO::initialize() {
  metadata = MetaClass::registerClass<SolverACO>("solver", "solver_aco",
                                                  Object::create<SolverACO>);
  registerFields<SolverACO>(const_cast<MetaClass*>(metadata));
  auto& x = PythonExtension<SolverACO>::getPythonType();
  x.setName("solverACO");
  x.setDoc("ccAPPS ant colony optimization solver");
  x.supportgetattro(); x.supportsetattro(); x.supportcreate(SolverACO::create);

  // Inherit essential methods from SolverCreate (same as solver_mrp)
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

  // ACO-specific methods
  x.addMethod("initPheromone", solverACO_initPheromone, METH_VARARGS,
              "Initialize pheromone matrix for a resource");
  x.addMethod("setRunMRP", solverACO_setRunMRP, METH_VARARGS,
              "Enable/disable MRP propagation after ACO sequencing");
  x.addMethod("getRunMRP", solverACO_getRunMRP, METH_NOARGS,
              "Check if MRP propagation is enabled");
  x.addMethod("setPurchaseMaterialMode", solverACO_setPurchaseMaterialMode,
              METH_VARARGS,
              "Set ACO purchase material mode: 0=infinite, 1=leadtime");
  x.addMethod("getPurchaseMaterialMode", solverACO_getPurchaseMaterialMode,
              METH_NOARGS, "Get ACO purchase material mode");

  SolverACO::metadata->setPythonClass(x);
  return x.typeReady();
}

PyObject* SolverACO::create(PyTypeObject*, PyObject*, PyObject*) {
  return Object::create<SolverACO>();
}

// ==========================================================================
// Setup time helper
// ==========================================================================

Duration SolverACO::computeSetupTime(const OperationPlan* from,
                                     const OperationPlan* to) const {
  if (!from || !to) return Duration(0L);
  if (from->getOperation() == to->getOperation()) return Duration(0L);
  PooledString fromSetup, toSetup;
  const SetupMatrix* matrix = nullptr;
  for (auto fl = from->beginLoadPlans(); fl != from->endLoadPlans(); ++fl) {
    if (fl->isStart() && fl->getResource() && fl->getResource()->getConstrained()) {
      fromSetup = fl->getSetupLoad();
      matrix = fl->getResource()->getSetupMatrix();
      break;
    }
  }
  for (auto tl = to->beginLoadPlans(); tl != to->endLoadPlans(); ++tl) {
    if (tl->isStart() && tl->getResource() && tl->getResource()->getConstrained()) {
      toSetup = tl->getSetupLoad();
      break;
    }
  }
  if (fromSetup.empty() && toSetup.empty()) return Duration(0L);
  if (!matrix) return Duration(0L);
  SetupMatrixRule* rule = matrix->calculateSetup(fromSetup, toSetup);
  return rule ? rule->getDuration() : Duration(0L);
}

// ==========================================================================
// Heuristic (factors 2+3: setup time + due date)
// ==========================================================================

double SolverACO::heuristic(const OperationPlan* from,
                            const OperationPlan* to) const {
  if (!to) return 0.0;
  double h = 1.0;
  // Factor 2: Setup time — prefer less setup
  if (from) {
    Duration setup = computeSetupTime(from, to);
    if (setup > Duration(0L))
      h *= 1.0 / (1.0 + static_cast<double>(setup.getSeconds()) / 3600.0);
  }
  // Factor: Demand priority — prefer higher-priority orders
  Demand* dmd = to->getTopOwner()->getDemand();
  if (dmd) {
    int dmdPrio = dmd->getPriority();
    h *= 1.0 + config_.weight_priority / (1.0 + static_cast<double>(dmdPrio));
  } else {
    // Fallback: operation-level priority
    double prio = to->getOperation()->getPriority();
    if (prio > 0.0) h *= (1.0 + prio * 0.1);
  }
  // Factor 3: Due date urgency — prefer more urgent
  if (dmd && dmd->getDue() != Date::infiniteFuture) {
    auto urgency = static_cast<double>(
        (dmd->getDue() - Plan::instance().getCurrent()).getSeconds()) / 86400.0;
    if (urgency > 0.0)
      h *= max(1.0, 10.0 / (1.0 + urgency));
    else
      h *= 100.0;  // already overdue → very high priority
  }
  return max(h, 1e-6);
}

// ==========================================================================
// Duration estimation
// ==========================================================================

Duration estimateOperationDuration(const OperationPlan* op, const Resource* res) {
  if (op->getEnd() != Date::infiniteFuture && op->getStart() > Date::infinitePast) {
    Duration dur = op->getEnd() - op->getStart();
    if (dur > Duration(0L)) return dur;
  }
  Duration ld = Duration(0L);
  for (auto l = op->getOperation()->getLoads().begin();
       l != op->getOperation()->getLoads().end(); ++l) {
    if (l->getResource() == res && l->getQuantity() > 0.0) {
      ld = Duration(static_cast<long>(l->getQuantity() * op->getQuantity() * 3600.0));
      if (ld > Duration(0L)) break;
    }
  }
  if (ld > Duration(0L)) return ld;
  Date fence = op->getOperation()->getFence(op);
  if (fence != Date::infiniteFuture) {
    Duration d = fence - Plan::instance().getCurrent();
    if (d > Duration(0L)) return d;
  }
  return Duration(3600L);
}

// ==========================================================================
// Factor 5: Material availability
// ==========================================================================

Date SolverACO::purchaseMaterialAvailable(const Buffer* buf) const {
  if (!buf || !buf->getItem()) return Date::infiniteFuture;
  Date current = Plan::instance().getCurrent();
  Date earliest = Date::infiniteFuture;

  Item* item = buf->getItem();
  while (item) {
    Item::supplierlist::const_iterator supitem_iter =
        item->getSupplierIterator();
    while (ItemSupplier* supitem = supitem_iter.next()) {
      if (!supitem->getPriority()) continue;
      if (!supitem->getEffective().within(current)) continue;

      // Match the same location applicability rule used when buffers create
      // purchase operations from item suppliers.
      if (supitem->getLocation()) {
        if ((buf->getLocation() && buf->getLocation() != supitem->getLocation()) ||
            !buf->getLocation())
          continue;
      }

      Date available = config_.purchase_material_mode == 0
          ? current
          : current + supitem->getLeadTime();
      if (available < earliest) earliest = available;
    }
    item = item->getOwner();
  }

  return earliest;
}

Date SolverACO::earliestStart(const OperationPlan* op, const Resource* res) const {
  Date earliest = Plan::instance().getCurrent();

  // Check each consuming flowplan — find the latest upstream completion date
  for (auto fp = op->beginFlowPlans(); fp != op->endFlowPlans(); ++fp) {
    if (fp->getQuantity() >= 0.0) continue;  // skip producing flows
    Buffer* buf = fp->getBuffer();
    if (!buf) continue;
    // Find when material first becomes available. Current stock can be used
    // immediately; purchasable material follows the configured ACO purchase
    // material mode instead of waiting for an existing PO flowplan.
    double currentOnhand =
        buf->getOnHand(Plan::instance().getCurrent(), false);
    if (currentOnhand >= -ROUNDING_ERROR) continue;  // enough on hand now
    Date purchaseDate = purchaseMaterialAvailable(buf);
    if (purchaseDate != Date::infiniteFuture) {
      if (purchaseDate > earliest) earliest = purchaseDate;
      continue;
    }
    double onhand = buf->getOnHand(Date::infiniteFuture, false);
    if (onhand >= -ROUNDING_ERROR) continue;  // enough on hand eventually
    // Scan flowplans for the first incoming supply
    for (auto sfp = buf->getFlowPlans().begin();
         sfp != buf->getFlowPlans().end(); ++sfp) {
      if (sfp->getQuantity() > 0 && sfp->getDate() > earliest) {
        OperationPlan* supplier = sfp->getOperationPlan();
        if (supplier && supplier->getEnd() > earliest)
          earliest = supplier->getEnd();
      }
    }
  }

  // Factor 4: Upstream dependency — also check isUpstreamBlocked
  // This is handled more fully during construction via isUpstreamBlocked()

  return earliest;
}

// ==========================================================================
// Factor 5b: Dynamic material availability clock
// Computes earliest start based on the dynamic material clock updated during
// ant construction. As the ant places operations across resources, the
// material clock tracks when each buffer receives material from upstream
// operations already scheduled. This method extracts the constraint: an
// operation consuming from buffer B cannot start before materialAvailable[B].
// The static earliestStart snapshot still handles non-ACO constraints
// (PO arrivals, pre-existing stock) that the clock hasn't captured.
// ==========================================================================

Date SolverACO::dynamicEarliestStart(
    const OperationPlan* op,
    const unordered_map<const Buffer*, Date>& materialAvailable) const {
  Date earliest = Plan::instance().getCurrent();
  if (!op) return earliest;
  for (auto fp = op->beginFlowPlans(); fp != op->endFlowPlans(); ++fp) {
    if (fp->getQuantity() >= 0.0) continue;  // skip producing flows
    const Buffer* buf = fp->getBuffer();
    if (!buf) continue;
    auto it = materialAvailable.find(buf);
    if (it != materialAvailable.end() && it->second > earliest)
      earliest = it->second;
  }
  return earliest;
}

// ==========================================================================
// Factor 4: Cross-resource upstream blocking
// ==========================================================================

bool SolverACO::isUpstreamBlocked(
    const OperationPlan* op,
    const unordered_map<const Resource*, Date>& resourceTimes) const {
  if (!op) return false;
  for (auto fl = op->beginFlowPlans(); fl != op->endFlowPlans(); ++fl) {
    if (fl->getQuantity() >= 0.0) continue;
    Buffer* buf = fl->getBuffer();
    if (!buf) continue;
    for (auto pfp = buf->getFlowPlans().begin();
         pfp != buf->getFlowPlans().end(); ++pfp) {
      OperationPlan* producer = pfp->getOperationPlan();
      if (!producer || producer == op) continue;
      for (auto lp = producer->beginLoadPlans();
           lp != producer->endLoadPlans(); ++lp) {
        const Resource* prodRes = lp->getResource();
        if (!prodRes || !prodRes->getConstrained()) continue;
        Date refTime = Plan::instance().getCurrent();
        auto it = resourceTimes.find(prodRes);
        if (it != resourceTimes.end()) refTime = it->second;
        if (producer->getEnd() > refTime &&
            producer->getEnd() != Date::infiniteFuture)
          return true;
      }
    }
  }
  return false;
}

// ==========================================================================
// Factor 4b: Continuous upstream wait time
// Instead of a binary blocked/not-blocked signal, returns the cumulative
// wait duration until all upstream producers on constrained resources
// have completed. Used to compute a continuous readiness factor for
// smoother probability landscapes in ant construction.
// ==========================================================================

Duration SolverACO::computeUpstreamWait(
    const OperationPlan* op,
    const unordered_map<const Resource*, Date>& resourceTimes) const {
  if (!op) return Duration(0L);
  Duration totalWait(0L);
  for (auto fl = op->beginFlowPlans(); fl != op->endFlowPlans(); ++fl) {
    if (fl->getQuantity() >= 0.0) continue;
    Buffer* buf = fl->getBuffer();
    if (!buf) continue;
    for (auto pfp = buf->getFlowPlans().begin();
         pfp != buf->getFlowPlans().end(); ++pfp) {
      OperationPlan* producer = pfp->getOperationPlan();
      if (!producer || producer == op) continue;
      for (auto lp = producer->beginLoadPlans();
           lp != producer->endLoadPlans(); ++lp) {
        const Resource* prodRes = lp->getResource();
        if (!prodRes || !prodRes->getConstrained()) continue;
        // Use tracked ACO clock if available; otherwise fall back to plan
        // current time (resource not being optimized — MRP schedule untouched).
        Date refTime = Plan::instance().getCurrent();
        auto it = resourceTimes.find(prodRes);
        if (it != resourceTimes.end()) refTime = it->second;
        if (producer->getEnd() > refTime &&
            producer->getEnd() != Date::infiniteFuture) {
          Duration wait = producer->getEnd() - refTime;
          if (wait > totalWait) totalWait = wait;
        }
      }
    }
  }
  return totalWait;
}

// ==========================================================================
// Helper: get all constrained resources an operation needs
// ==========================================================================

vector<const Resource*> SolverACO::getConstrainedResources(
    const OperationPlan* op) const {
  vector<const Resource*> result;
  if (!op || !op->getOperation()) return result;
  for (const auto& ld : op->getOperation()->getLoads()) {
    const Resource* ldRes = ld.getResource();
    if (!ldRes || !ldRes->getConstrained()) continue;
    if (ldRes->isGroup()) {
      Skill* requiredSkill = ld.getSkill();
      for (auto m = ldRes->getMembers(); m != Resource::end(); ++m) {
        if (m->isGroup() || !m->getConstrained()) continue;
        // Check skill: if the Load requires a specific skill, only
        // include group members that possess it.
        if (requiredSkill) {
          bool hasSkill = false;
          for (auto rs = m->getSkills();; ++rs) {
            const ResourceSkill* rsk = &*rs;
            if (!rsk) break;
            if (rsk->getSkill() == requiredSkill) {
              hasSkill = true; break;
            }
          }
          if (!hasSkill) continue;
        }
        result.push_back(&*m);
      }
    } else {
      result.push_back(ldRes);
    }
  }
  return result;
}

// ==========================================================================
// Factor 1: Build candidates from manufacturing orders
// Each MO with ≥1 resource load generates candidates
// ==========================================================================

vector<CandidateOp> SolverACO::buildCandidates(
    const vector<const Resource*>& resources) const {
  vector<CandidateOp> candidates;
  unordered_set<OperationPlan*> processed;
  int nextId = 0;

  for (auto* res : resources) {
    auto loadplans = res->getLoadPlans();
    for (auto it = loadplans.begin(); it != loadplans.end(); ++it) {
      OperationPlan* op = it->getOperationPlan();
      if (!op || op->getQuantity() <= 0.0) continue;
      // Skip non-manufacturing orders (purchases, distributions)
      if (op->getOperation()->hasType<OperationItemSupplier,
                                       OperationItemDistribution>())
        continue;

      if (processed.count(op)) continue;
      processed.insert(op);

      // Collect all constrained resources this operation could run on.
      // Expand resource groups into individual child resources so ACO can
      // choose the best machine within a pool.
      unordered_set<const Resource*> altResources;
      for (const auto& ld : op->getOperation()->getLoads()) {
        const Resource* ldRes = ld.getResource();
        if (!ldRes || !ldRes->getConstrained()) continue;

        if (ldRes->isGroup()) {
          // Expand resource group up to 3 levels to find individual machines
          for (auto m1 = ldRes->getMembers(); m1 != Resource::end(); ++m1) {
            if (m1->isGroup()) {
              for (auto m2 = m1->getMembers(); m2 != Resource::end(); ++m2) {
                if (m2->isGroup()) {
                  for (auto m3 = m2->getMembers(); m3 != Resource::end(); ++m3)
                    altResources.insert(&*m3);
                } else {
                  altResources.insert(&*m2);
                }
              }
            } else {
              altResources.insert(&*m1);
            }
          }
        } else {
          altResources.insert(ldRes);
        }
      }

      // Separate machines from operators (group members).
      // Generate one candidate per operator so each has a fair chance.
      if (!altResources.empty()) {
        vector<const Resource*> machines, operators;
        for (auto* r : altResources) {
          // A resource is an operator if it has a parent (owner)
          if (r->getOwner() && r->getOwner() != r) operators.push_back(r);
          else machines.push_back(r);
        }
        if (!operators.empty()) {
          for (auto* oper : operators) {
            CandidateOp c;
            c.op = op;
            c.allResources = machines;
            c.allResources.push_back(oper);
            c.res = oper;
            Date eStart = Plan::instance().getCurrent();
            for (auto* r : c.allResources) {
              Date rStart = earliestStart(op, r);
              if (rStart > eStart) eStart = rStart;
            }
            c.earliestStart = eStart;
            c.candId = nextId++;
            candidates.push_back(c);
          }
          processed.insert(op); continue; // skip outer ++nextId
        } else {
          CandidateOp c;
          c.op = op;
          c.allResources = machines;
          c.res = c.allResources[0];
          Date eStart = Plan::instance().getCurrent();
          for (auto* r : c.allResources) {
            Date rStart = earliestStart(op, r);
            if (rStart > eStart) eStart = rStart;
          }
          c.earliestStart = eStart;
          c.candId = nextId;
          candidates.push_back(c);
        }
      } else {
        CandidateOp c;
        c.op = op;
        c.res = res;
        c.allResources.push_back(res);
        c.earliestStart = earliestStart(op, res);
        c.candId = nextId;
        candidates.push_back(c);
      }
      ++nextId;
    }
  }
  return candidates;
}

// ==========================================================================
// Construct solution (single-resource legacy)
// ==========================================================================

AntSolution SolverACO::constructSolution(
    const Resource* res, const vector<OperationPlan*>& plans) {
  AntSolution ant;
  if (plans.empty()) return ant;

  vector<OperationPlan*> unvisited = plans;
  sort(unvisited.begin(), unvisited.end(), [](OperationPlan* a, OperationPlan* b) {
    return a->getOperation()->getPriority() < b->getOperation()->getPriority();
  });

  const OperationPlan* current = nullptr;
  Date cur = Plan::instance().getCurrent();

  // Build a minimal resource-times map so computeUpstreamWait can check
  // upstream dependencies on other constrained resources.
  unordered_map<const Resource*, Date> rt;
  rt[res] = cur;

  while (!unvisited.empty()) {
    vector<double> probs(unvisited.size(), 0.0);
    double total = 0.0;
    for (size_t i = 0; i < unvisited.size(); ++i) {
      // Continuous readiness: 1/(1+waitHours) for upstream dependencies
      Duration upWait = computeUpstreamWait(unvisited[i], rt);
      double waitH = upWait > Duration(0L)
          ? static_cast<double>(upWait.getSeconds()) / 3600.0 : 0.0;
      double readiness = 1.0 / (1.0 + waitH);
      double tau = pheromones_[res].get(current, unvisited[i]);
      if (tau <= 0.0) tau = config_.tau0;
      double eta = heuristic(current, unvisited[i]) * readiness;
      probs[i] = pow(tau, config_.alpha) * pow(eta, config_.beta);
      total += probs[i];
    }
    double r = uniform_real_distribution<double>(0.0, total)(rng_);
    double cum = 0.0;
    size_t sel = unvisited.size() - 1;
    for (size_t i = 0; i < unvisited.size(); ++i) {
      cum += probs[i];
      if (cum >= r) { sel = i; break; }
    }
    OperationPlan* next = unvisited[sel];
    Duration setup = computeSetupTime(current, next);
    Date rawStart = max(cur + setup, earliestStart(next, res));
    Duration dur = estimateOperationDuration(next, res);
    // Apply calendar constraints: map start+duration through
    // operation/resource/location availability calendars
    Date start, end;
    if (next->getOperation()) {
      DateRange range = next->getOperation()->calculateOperationTime(
          next, rawStart, dur, true);
      start = range.getStart();
      end = range.getEnd();
    } else {
      start = rawStart;
      end = rawStart + dur;
    }

    ant.sequences[res].push_back(next);
    ant.startDates[res].push_back(start);
    ant.endDates[res].push_back(end);

    unvisited.erase(unvisited.begin() + sel);
    current = next;
    cur = end;
    rt[res] = cur;  // keep resource clock in sync for upstream wait checks
  }
  return ant;
}

// ==========================================================================
// Construct joint solution using candidate pool
// ==========================================================================

AntSolution SolverACO::constructJointSolution(
    const vector<const Resource*>& resources,
    const vector<CandidateOp>& allCandidates,
    const unordered_map<const Resource*, Date>& resourceTimes) {

  AntSolution ant;
  if (allCandidates.empty()) return ant;

  // Per-resource state
  unordered_map<const Resource*, const OperationPlan*> prevOp;
  unordered_map<const Resource*, Date> curTime;
  for (auto* r : resources) {
    auto it = resourceTimes.find(r);
    curTime[r] = (it != resourceTimes.end()) ? it->second
                  : Plan::instance().getCurrent();
    prevOp[r] = nullptr;
  }
  for (auto& c : allCandidates) {
    for (auto* r : c.allResources) {
      if (!curTime.count(r)) {
        auto it = resourceTimes.find(r);
        curTime[r] = (it != resourceTimes.end()) ? it->second
                    : Plan::instance().getCurrent();
      }
      if (!prevOp.count(r)) prevOp[r] = nullptr;
    }
  }

  // Upstream priority map: operations that produce buffers consumed by
  // other candidates get a scheduling bonus. This helps respect process
  // routing order (e.g., Saw before Assemble).
  unordered_map<const OperationPlan*, int> upstreamScore;
  {
    // Collect buffers consumed by candidates
    unordered_map<const Buffer*, int> consumerCount;
    for (auto& c : allCandidates) {
      if (!c.op) continue;
      for (auto fp = c.op->beginFlowPlans(); fp != c.op->endFlowPlans(); ++fp) {
        if (fp->getQuantity() >= 0.0) continue;  // consuming flows
        const Buffer* buf = fp->getBuffer();
        if (buf) consumerCount[buf]++;
      }
    }
    // Score each candidate by how many downstream ops depend on its output
    for (auto& c : allCandidates) {
      if (!c.op) continue;
      int score = 0;
      for (auto fp = c.op->beginFlowPlans(); fp != c.op->endFlowPlans(); ++fp) {
        if (fp->getQuantity() <= 0.0) continue;  // producing flows
        const Buffer* buf = fp->getBuffer();
        if (buf) {
          auto it = consumerCount.find(buf);
          if (it != consumerCount.end()) score = max(score, it->second);
        }
      }
      if (score > 0) upstreamScore[c.op] = score;
    }
  }

  // Dynamic material availability clock: tracks when each buffer receives
  // material from operations already scheduled during this ant's construction.
  // Initialized empty — the static earliestStart snapshot handles pre-ACO
  // material constraints (PO arrivals, on-hand stock). As operations are
  // placed, producing flowplans update this clock so downstream operations
  // on other resources see the updated material timing.
  unordered_map<const Buffer*, Date> materialAvailable;

  // Track unique operation plans. A single operation can have multiple
  // candidate resource assignments; once one candidate is chosen, all other
  // candidates for the same operation are implicitly excluded.
  unordered_set<const OperationPlan*> candidateOps;
  for (auto& c : allCandidates)
    if (c.op) candidateOps.insert(c.op);
  unordered_set<const OperationPlan*> scheduledOps;

  auto isReady = [&](const OperationPlan* op) -> bool {
    if (!op) return false;
    for (auto fl = op->beginFlowPlans(); fl != op->endFlowPlans(); ++fl) {
      if (fl->getQuantity() >= 0.0) continue;
      Buffer* buf = fl->getBuffer();
      if (!buf) continue;
      double requiredQty = -fl->getQuantity();
      double currentOnHand = buf->getOnHand(Plan::instance().getCurrent(), false);
      bool currentStockCoversDemand =
          currentOnHand + ROUNDING_ERROR >= requiredQty;

      bool hasCandidateProducer = false;
      for (auto pfp = buf->getFlowPlans().begin();
           pfp != buf->getFlowPlans().end(); ++pfp) {
        if (pfp->getQuantity() <= 0.0) continue;
        OperationPlan* producer = pfp->getOperationPlan();
        if (!producer || producer == op) continue;

        // Hard precedence: if the upstream producer is part of this ACO
        // candidate set, downstream operations can only be scheduled after
        // that producer has been selected and placed by this ant, unless
        // enough semi-finished stock is already available at the current time.
        if (candidateOps.count(producer)) {
          hasCandidateProducer = true;
          if (!scheduledOps.count(producer) && !currentStockCoversDemand)
            return false;
        }
      }

      if (hasCandidateProducer)
        continue;

      // Existing stock can satisfy this consuming flow when no ACO candidate
      // producer still needs to be scheduled first.
      if (buf->getOnHand(Date::infiniteFuture, false) >= -ROUNDING_ERROR)
        continue;

      // Purchased materials are considered unlimited by ACO according to the
      // configured purchase material mode. earliestStart will enforce either
      // immediate availability or current time plus supplier lead time.
      if (purchaseMaterialAvailable(buf) != Date::infiniteFuture)
        continue;

      for (auto pfp = buf->getFlowPlans().begin();
           pfp != buf->getFlowPlans().end(); ++pfp) {
        if (pfp->getQuantity() <= 0.0) continue;
        OperationPlan* producer = pfp->getOperationPlan();
        if (!producer || producer == op) continue;

        // External upstream supply must have a known completion date.
        if (producer->getEnd() == Date::infiniteFuture)
          return false;
      }
    }
    return true;
  };

  // Total remaining unique operations.
  size_t remaining = candidateOps.size();
  while (remaining > 0) {
    vector<const CandidateOp*> readyCandidates;
    vector<double> probs;
    double total = 0.0;

    for (auto& c : allCandidates) {
      if (!c.op || scheduledOps.count(c.op) || c.allResources.empty())
        continue;
      if (!isReady(c.op)) continue;

      // Continuous readiness still helps rank candidates that depend on
      // external producers with an existing completion date.
      Duration upWait = computeUpstreamWait(c.op, curTime);
      double waitH = upWait > Duration(0L)
          ? static_cast<double>(upWait.getSeconds()) / 3600.0 : 0.0;
      double readiness = 1.0 / (1.0 + waitH);
      Date candidateStart = c.earliestStart;
      for (auto* r : c.allResources) {
        Duration setup = computeSetupTime(prevOp[r], c.op);
        candidateStart = max(candidateStart, curTime[r] + setup);
      }
      candidateStart = max(
          candidateStart, dynamicEarliestStart(c.op, materialAvailable));
      double startWaitH = candidateStart > Plan::instance().getCurrent()
          ? static_cast<double>(
                (candidateStart - Plan::instance().getCurrent()).getSeconds()) /
                3600.0
          : 0.0;
      double startFactor = 1.0 / (1.0 + startWaitH);

      double tau = 0.0;
      double eta = 0.0;
      for (auto* r : c.allResources) {
        double t = pheromones_[r].get(prevOp[r], c.op);
        if (t <= 0.0) t = config_.tau0;
        tau += t;

        double curLoadH = static_cast<double>(
            (curTime[r] - Plan::instance().getCurrent()).getSeconds()) / 3600.0;
        double loadBalance = 1.0 / (1.0 + curLoadH * 2.0);
        eta += heuristic(prevOp[r], c.op) * loadBalance;
      }
      tau /= static_cast<double>(c.allResources.size());
      eta /= static_cast<double>(c.allResources.size());

      double upstreamFactor = 1.0;
      auto usIt = upstreamScore.find(c.op);
      if (usIt != upstreamScore.end())
        upstreamFactor = 1.0 + 0.3 * usIt->second;

      double probability = pow(tau, config_.alpha) *
          pow(max(eta * readiness * upstreamFactor * startFactor, 1e-9),
              config_.beta);
      if (probability <= 0.0) continue;

      readyCandidates.push_back(&c);
      probs.push_back(probability);
      total += probability;
    }

    if (readyCandidates.empty() || total <= 0.0)
      break;  // Remaining operations are blocked by unscheduled upstream work.

    // Roulette selection over ready operation candidates. This makes the
    // operation choose its resource assignment, instead of a resource choosing
    // the next operation from its own pool.
    double r = uniform_real_distribution<double>(0.0, total)(rng_);
    double cum = 0.0;
    size_t sel = readyCandidates.size() - 1;
    for (size_t i = 0; i < readyCandidates.size(); ++i) {
      cum += probs[i];
      if (cum >= r) { sel = i; break; }
    }

    CandidateOp chosen = *readyCandidates[sel];

    // Compute rawStart across ALL required resources. The operation can only
    // begin when every required resource is free and all scheduled upstream
    // producers have made their material available.
    Date rawStart = chosen.earliestStart;
    for (auto* r : chosen.allResources) {
      Duration setup = computeSetupTime(prevOp[r], chosen.op);
      rawStart = max(rawStart, curTime[r] + setup);
    }
    Date matEarliest = dynamicEarliestStart(chosen.op, materialAvailable);
    rawStart = max(rawStart, matEarliest);

    const Resource* durationResource = chosen.res ? chosen.res
        : chosen.allResources.front();
    Duration dur = estimateOperationDuration(chosen.op, durationResource);
    Date start, end;
    if (chosen.op->getOperation()) {
      DateRange range = chosen.op->getOperation()->calculateOperationTime(
          chosen.op, rawStart, dur, true);
      start = range.getStart();
      end = range.getEnd();
    } else {
      start = rawStart;
      end = rawStart + dur;
    }

    // Write to ALL resources this operation occupies.
    for (auto* r : chosen.allResources) {
      ant.sequences[r].push_back(chosen.op);
      ant.startDates[r].push_back(start);
      ant.endDates[r].push_back(end);
      ant.assignedResources[r].push_back(r);
      prevOp[r] = chosen.op;
      curTime[r] = end;
    }

    // Update material availability clock for downstream operations.
    for (auto fp = chosen.op->beginFlowPlans();
         fp != chosen.op->endFlowPlans(); ++fp) {
      if (fp->getQuantity() < 0.0) continue;
      const Buffer* buf = fp->getBuffer();
      if (!buf) continue;
      auto it = materialAvailable.find(buf);
      if (it == materialAvailable.end() || end > it->second)
        materialAvailable[buf] = end;
    }

    scheduledOps.insert(chosen.op);
    --remaining;
  }

  return ant;
}

// ==========================================================================
// Single-resource local search (legacy)
// ==========================================================================

void SolverACO::localSearch(const Resource*, AntSolution& sol) {
  if (sol.sequences.empty()) return;
  auto& kv = *sol.sequences.begin();
  const Resource* res = kv.first;
  auto& seq = kv.second;
  if (seq.size() < 2) return;

  bool improved = true;
  while (improved) {
    improved = false;
    for (size_t i = 0; i < seq.size() - 1; ++i) {
      for (size_t j = i + 1; j < seq.size(); ++j) {
        auto cseq = seq; swap(cseq[i], cseq[j]);
        vector<Date> cs, ce;
        Date ct = Plan::instance().getCurrent();
        const OperationPlan* prev = nullptr;
        for (auto* op : cseq) {
          Date rawStart = max(ct + computeSetupTime(prev, op), earliestStart(op, res));
          Duration dur = estimateOperationDuration(op, res);
          Date start, end;
          if (op->getOperation()) {
            DateRange range = op->getOperation()->calculateOperationTime(
                op, rawStart, dur, true);
            start = range.getStart();
            end = range.getEnd();
          } else {
            start = rawStart;
            end = rawStart + dur;
          }
          cs.push_back(start); ce.push_back(end);
          prev = op; ct = end;
        }
        AntSolution cand;
        cand.sequences[res] = move(cseq);
        cand.startDates[res] = move(cs);
        cand.endDates[res] = move(ce);
        cand.fitness = evaluate(cand);
        if (cand.fitness > sol.fitness) { sol = move(cand); improved = true; }
      }
    }
  }
}

// ==========================================================================
// Schedule compaction: left-shift operations to fill idle gaps
// After constructive scheduling, resources may have idle periods because
// an operation was delayed waiting for another resource. This pass replays
// operations in start-time order, recalculating the earliest feasible
// start on all required resources simultaneously.
// ==========================================================================

void SolverACO::compactSchedule(
    AntSolution& ant,
    const vector<const Resource*>& resources,
    const unordered_map<const Resource*, Date>& resourceTimes) {
  // Collect all ops with their resources and current times
  struct OpEntry {
    OperationPlan* op;
    vector<const Resource*> resList;
    Date start, end;
  };
  vector<OpEntry> allOps;

  // Gather unique operations from all resources, deduplicate by pointer
  unordered_set<OperationPlan*> seen;
  for (auto* r : resources) {
    auto& seq = ant.sequences[r];
    auto& starts = ant.startDates[r];
    auto& ends = ant.endDates[r];
    for (size_t i = 0; i < seq.size(); ++i) {
      if (seen.count(seq[i])) continue;
      seen.insert(seq[i]);
      OpEntry e;
      e.op = seq[i];
      e.resList = getConstrainedResources(seq[i]);
      if (e.resList.empty()) e.resList.push_back(r);
      e.start = starts[i];
      e.end = ends[i];
      allOps.push_back(e);
    }
  }

  if (allOps.empty()) return;

  // Sort by current start time
  sort(allOps.begin(), allOps.end(),
       [](const OpEntry& a, const OpEntry& b) { return a.start < b.start; });

  // Reset resource clocks and per-resource sequences
  unordered_map<const Resource*, Date> curTime;
  unordered_map<const Resource*, const OperationPlan*> prevOp;
  unordered_map<const Resource*, vector<OperationPlan*>> newSeq;
  unordered_map<const Resource*, vector<Date>> newStarts, newEnds;

  for (auto* r : resources) {
    auto it = resourceTimes.find(r);
    curTime[r] = (it != resourceTimes.end()) ? it->second
                  : Plan::instance().getCurrent();
    prevOp[r] = nullptr;
  }

  // Replay operations in current order, but compacted
  for (auto& e : allOps) {
    // Compute earliest feasible start across all required resources
    Date rawStart = earliestStart(e.op, e.resList[0]);
    for (auto* r : e.resList) {
      Duration setup = computeSetupTime(prevOp[r], e.op);
      rawStart = max(rawStart, curTime[r] + setup);
    }

    Duration dur = estimateOperationDuration(e.op, e.resList[0]);
    Date start, end;
    if (e.op->getOperation()) {
      DateRange range = e.op->getOperation()->calculateOperationTime(
          e.op, rawStart, dur, true);
      start = range.getStart();
      end = range.getEnd();
    } else {
      start = rawStart;
      end = rawStart + dur;
    }

    // Commit to all resources
    for (auto* r : e.resList) {
      newSeq[r].push_back(e.op);
      newStarts[r].push_back(start);
      newEnds[r].push_back(end);
      prevOp[r] = e.op;
      curTime[r] = end;
    }
  }

  // Replace ant's sequences with compacted versions
  for (auto* r : resources) {
    ant.sequences[r] = move(newSeq[r]);
    ant.startDates[r] = move(newStarts[r]);
    ant.endDates[r] = move(newEnds[r]);
  }
}

// ==========================================================================
// Joint local search
// ==========================================================================

void SolverACO::localSearchJoint(AntSolution& sol) {
  // Build resource list from sol.sequences keys
  vector<const Resource*> resources;
  unordered_map<const Resource*, Date> resTimes;
  for (auto& kv : sol.sequences) {
    resources.push_back(kv.first);
    resTimes[kv.first] = Plan::instance().getCurrent();
  }
  if (resources.empty()) return;

  bool improved = true;
  while (improved) {
    improved = false;
    for (auto& kv : sol.sequences) {
      const Resource* res = kv.first;
      auto& seq = kv.second;
      if (seq.size() < 2) continue;
      for (size_t i = 0; i < seq.size() - 1; ++i) {
        for (size_t j = i + 1; j < seq.size(); ++j) {
          AntSolution cand = sol;
          auto& cseq = cand.sequences[res];
          swap(cseq[i], cseq[j]);
          // After swap, run compaction to rebuild all resource timelines
          compactSchedule(cand, resources, resTimes);
          cand.fitness = evaluate(cand);
          if (cand.fitness > sol.fitness) {
            sol = move(cand);
            improved = true;
            goto restart;
          }
        }
      }
      restart:;
    }
  }
}

// ==========================================================================
// Evaluate solution (factors 1-5 all contribute)
// ==========================================================================

double SolverACO::evaluate(const AntSolution& sol) {
  double tardiness = 0.0, cost = 0.0, setup = 0.0;
  const double MATERIAL_PENALTY = 1000.0;  // heavy penalty for violating material constraint

  // Track seen operations to avoid double-counting multi-resource ops
  unordered_set<const OperationPlan*> seen;

  for (const auto& kv : sol.sequences) {
    const Resource* res = kv.first;
    const auto& seq = kv.second;
    const auto& starts = sol.startDates.at(res);
    const auto& ends = sol.endDates.at(res);

    const OperationPlan* prev = nullptr;
    for (size_t i = 0; i < seq.size(); ++i) {
      const OperationPlan* op = seq[i];

      // Deduplicate: multi-resource ops appear on every resource they occupy
      if (seen.count(op)) { prev = op; continue; }
      seen.insert(op);

      // Compute priority factor: lower priority number = higher importance.
      // dmdPrio=0 → factor≈11x, dmdPrio=999 → factor≈1.0x
      Demand* dmd = op->getTopOwner()->getDemand();
      int dmdPrio = dmd ? dmd->getPriority() : 999;
      double priorityFactor = 1.0
          + config_.weight_priority / (1.0 + static_cast<double>(dmdPrio));

      // Factor 5: Material penalty if starting before material is available
      Date matAvail = earliestStart(op, res);
      if (starts[i] < matAvail)
        tardiness += MATERIAL_PENALTY * priorityFactor;

      // Factor 3: Due date tardiness (priority-weighted)
      if (dmd && dmd->getDue() != Date::infiniteFuture && ends[i] > dmd->getDue())
        tardiness += static_cast<double>(
            (ends[i] - dmd->getDue()).getSeconds()) / 3600.0 * priorityFactor;

      // Factor: Operation cost (operation base + resource hourly)
      cost += op->getOperation()->getCost() * op->getQuantity();
      // Add resource usage cost per hour for the actual scheduled duration
      Duration dur = ends[i] - starts[i];
      if (dur > Duration(0L))
        cost += res->getCost() *
            static_cast<double>(dur.getSeconds()) / 3600.0;

      // Factor 2: Setup time
      if (prev)
        setup += static_cast<double>(
            computeSetupTime(prev, op).getSeconds()) / 3600.0;

      prev = op;
    }
  }

  // Factor: Load balancing — penalize uneven resource utilization.
  // Sum of squared load-hours per resource: 5+5 → 25+25=50, 10+0 → 100+0=100
  double loadBalance = 0.0;
  for (const auto& kv : sol.sequences) {
    double resLoad = 0.0;
    const auto& ss = sol.startDates.at(kv.first);
    const auto& es = sol.endDates.at(kv.first);
    for (size_t i = 0; i < kv.second.size(); ++i)
      resLoad += static_cast<double>((es[i] - ss[i]).getSeconds()) / 3600.0;
    loadBalance += resLoad * resLoad;
  }

  return -(config_.weight_tardiness * tardiness +
           config_.weight_cost * cost +
           config_.weight_setup * setup +
           config_.weight_balance * loadBalance);
}

// ==========================================================================
// Pheromone management
// ==========================================================================

void SolverACO::initPheromone(const Resource* res) {
  pheromones_[res].reset(config_.tau0);
}

const PheromoneMatrix* SolverACO::getPheromone(const Resource* res) const {
  auto it = pheromones_.find(res);
  return (it != pheromones_.end()) ? &it->second : nullptr;
}

// ==========================================================================
// Apply solution
// ==========================================================================

void SolverACO::applyBestSolution(const AntSolution& best) {
  auto* cmdMgr = getCommandManager();
  for (const auto& kv : best.sequences) {
    const Resource* seqRes = kv.first;
    const auto& seq = kv.second;
    const auto& starts = best.startDates.at(seqRes);
    const auto& ends = best.endDates.at(seqRes);
    const auto& assigned = best.assignedResources.count(seqRes)
        ? best.assignedResources.at(seqRes)
        : vector<const Resource*>();

    for (size_t i = 0; i < seq.size(); ++i) {
      if (starts[i] != Date::infiniteFuture && ends[i] != Date::infiniteFuture) {
        OperationPlan* op = seq[i];

        // If ACO chose a different resource than MRP, apply the change
        if (i < assigned.size() && assigned[i] &&
            assigned[i] != seqRes) {
          // Find the first start loadplan and switch to the chosen resource.
          // Look for the Load on the operation that owns the target resource.
          for (auto lp = op->beginLoadPlans(); lp != op->endLoadPlans(); ++lp) {
            if (!lp->isStart()) continue;
            // Find which Load on the operation owns the target resource
            const Load* targetLoad = nullptr;
            for (const auto& ld : op->getOperation()->getLoads()) {
              // Match by top-level resource (for grouped resources) or directly
              if (ld.getResource() == assigned[i] ||
                  (ld.getResource()->isGroup() &&
                   assigned[i]->getTop() == ld.getResource())) {
                targetLoad = &ld;
                break;
              }
            }
            if (targetLoad)
              lp->setLoad(const_cast<Load*>(targetLoad));
            else
              lp->setResource(const_cast<Resource*>(assigned[i]), false, false);
            break;
          }
        }

        if (cmdMgr)
          cmdMgr->add(new CommandMoveOperationPlan(op, starts[i], ends[i]));
        else
          op->setStart(starts[i], false, false);
      }
    }
  }
}

// ==========================================================================
// Collect operationplans
// ==========================================================================

static void collectResourcePlans(const Resource* res,
                                 vector<OperationPlan*>& plans) {
  auto loadplans = res->getLoadPlans();
  for (auto it = loadplans.begin(); it != loadplans.end(); ++it) {
    OperationPlan* op = it->getOperationPlan();
    if (op && op->getQuantity() > 0.0) {
      bool found = false;
      for (const auto& e : plans) if (e == op) { found = true; break; }
      if (!found) plans.push_back(op);
    }
  }
}

// ==========================================================================
// Single-resource ACO (legacy fallback)
// ==========================================================================

void SolverACO::solve(const Resource* res, void* v) {
  vector<OperationPlan*> plans;
  collectResourcePlans(res, plans);
  if (plans.size() < 2) { SolverCreate::solve(res, v); return; }
  if (pheromones_.find(res) == pheromones_.end()) initPheromone(res);

  PheromoneMatrix& phero = pheromones_[res];
  AntSolution best;
  best.fitness = -numeric_limits<double>::max();
  int stag = 0;

  for (int iter = 0; iter < config_.iterations; ++iter) {
    vector<AntSolution> ants(config_.ants);
    for (int a = 0; a < config_.ants; ++a) {
      ants[a] = constructSolution(res, plans);
      ants[a].fitness = evaluate(ants[a]);
      localSearch(res, ants[a]);
    }
    sort(ants.begin(), ants.end(), [](auto& a, auto& b) { return a.fitness > b.fitness; });
    if (ants[0].fitness > best.fitness) { best = move(ants[0]); stag = 0; }
    else ++stag;

    phero.evaporate(config_.evaporation);
    phero.deposit(best.sequences[res], config_.Q * (1.0 + max(0.0, best.fitness) * 0.01));
    for (int e = 0; e < config_.elite_ants && e < config_.ants; ++e)
      phero.deposit(ants[e].sequences[res],
                    config_.Q * 0.5 * (1.0 + max(0.0, ants[e].fitness) * 0.01));

    if (stag >= config_.stagnation_limit) {
      if (getLogLevel() > 1)
        logger << indentlevel << "ACO: stopped after " << (iter+1) << " iter (stagnation)\n";
      break;
    }
  }
  applyBestSolution(best);
  lastBestFitness_ = best.fitness;
  if (getLogLevel() > 0)
    logger << indentlevel << "ACO on '" << res->getName()
           << "': " << plans.size() << " plans, fitness=" << best.fitness << "\n";
}

// ==========================================================================
// Joint ACO
// ==========================================================================

void SolverACO::solveJoint(const vector<const Resource*>& resources) {
  if (resources.empty()) return;

  // Build candidate pool (factor 1: multi-resource)
  vector<CandidateOp> candidates = buildCandidates(resources);
  if (candidates.size() < 2) return;

  for (auto* res : resources)
    if (pheromones_.find(res) == pheromones_.end()) initPheromone(res);

  unordered_map<const Resource*, Date> resTimes;
  for (auto* r : resources) resTimes[r] = Plan::instance().getCurrent();

  AntSolution best;
  best.fitness = -numeric_limits<double>::max();
  int stag = 0;

  for (int iter = 0; iter < config_.iterations; ++iter) {
    vector<AntSolution> ants(config_.ants);
    for (int a = 0; a < config_.ants; ++a) {
      ants[a] = constructJointSolution(resources, candidates, resTimes);
      compactSchedule(ants[a], resources, resTimes);
      ants[a].fitness = evaluate(ants[a]);
      localSearchJoint(ants[a]);
    }
    sort(ants.begin(), ants.end(), [](auto& a, auto& b) { return a.fitness > b.fitness; });
    if (ants[0].fitness > best.fitness) { best = move(ants[0]); stag = 0; }
    else ++stag;

    for (auto* r : resources) pheromones_[r].evaporate(config_.evaporation);
    for (auto& kv : best.sequences)
      pheromones_[kv.first].deposit(kv.second,
          config_.Q * (1.0 + max(0.0, best.fitness) * 0.01));
    for (int e = 0; e < config_.elite_ants && e < config_.ants; ++e)
      for (auto& kv : ants[e].sequences)
        pheromones_[kv.first].deposit(kv.second,
            config_.Q * 0.5 * (1.0 + max(0.0, ants[e].fitness) * 0.01));

    if (stag >= config_.stagnation_limit) {
      if (getLogLevel() > 1)
        logger << indentlevel << "ACO joint: stopped after " << (iter+1) << " iter\n";
      break;
    }
  }

  applyBestSolution(best);
  lastBestFitness_ = best.fitness;
  stagnationOccurred_ = (stag >= config_.stagnation_limit);
  if (getLogLevel() > 0)
    logger << indentlevel << "ACO joint: " << resources.size() << " resources, "
           << candidates.size() << " candidates, fitness=" << best.fitness << "\n";
}

// ==========================================================================
// Top-level entry
// ==========================================================================

void SolverACO::solve(void* v) {
  // Collect all constrained resources with ≥2 operation plans.
  // Extracted as a lambda so the bottleneck set can be refreshed after each
  // MRP pass (MRP may create new supply ops, change resource assignments,
  // or update material availability dates).
    auto collectBottlenecks = [&]() -> vector<const Resource*> {
    vector<const Resource*> bn;
    unordered_set<const Resource*> activeGroups;
    for (auto res = Resource::begin(); res != Resource::end(); ++res) {
      if (!res->getConstrained()) continue;
      if (res->isGroup()) {
        for (auto m = res->getMembers(); m != Resource::end(); ++m) {
          if (!m->isGroup() && m->getConstrained()) {
            vector<OperationPlan*> plans;
            collectResourcePlans(&*m, plans);
            if (plans.size() >= 2) {
              bn.push_back(&*m);
              activeGroups.insert(&*res);
            }
          }
        }
      } else {
        vector<OperationPlan*> plans;
        collectResourcePlans(&*res, plans);
        if (plans.size() >= 2) bn.push_back(&*res);
      }
    }
    // Include ALL constrained members of active groups
    for (auto* grp : activeGroups) {
      for (auto m = grp->getMembers(); m != Resource::end(); ++m) {
        if (!m->isGroup() && m->getConstrained()) {
          bool alreadyIn = false;
          for (auto* r : bn) if (r == &*m) { alreadyIn = true; break; }
          if (!alreadyIn) bn.push_back(&*m);
        }
      }
    }
    return bn;
  };

vector<const Resource*> bottleneck = collectBottlenecks();

  // Diagnostic: always log that ACO entry was reached
  if (getLogLevel() >= 0)
    logger << indentlevel << "ACO: entry, found " << bottleneck.size()
           << " bottleneck resources\n";

  // No bottleneck resources → nothing for ACO to optimize.
  if (bottleneck.empty()) {
    if (config_.runMRP) SolverCreate::solve(v);
    return;
  }

  double prevFitness = -numeric_limits<double>::max();

  // Outer loop: ACO → MRP → re-collect → ACO → ... until convergence.
  // Each MRP pass propagates ACO's schedule changes through material flows
  // and resolves shortages. The next ACO pass then sees updated material
  // availability (via earliestStart / dynamicEarliestStart) and can
  // adjust the schedule accordingly.
  for (int outerIter = 0; outerIter < config_.aco_mrp_iterations; ++outerIter) {
    if (getLogLevel() > 1)
      logger << indentlevel << "ACO↔MRP pass " << (outerIter + 1)
             << " of " << config_.aco_mrp_iterations << " ("
             << bottleneck.size() << " bottleneck resources)\n";

    // ---- Phase 1: ACO optimization ----
    if (bottleneck.size() >= 2 && config_.joint_optimization)
      solveJoint(bottleneck);
    else if (bottleneck.size() == 1)
      solve(bottleneck[0], v);
    else
      break;

    // ---- Phase 2: Material propagation ----
    // ACO has set new start/end dates on operation plans (via
    // applyBestSolution → CommandMoveOperationPlan). We now need to:
    //   1. Synchronise flow plan dates with their operation plan dates
    //   2. Recalculate buffer on-hand profiles
    //   3. Resolve any material shortages introduced by ACO's resequencing
    //
    // SolverCreate::solve(void*) runs the full MRP pipeline: it re-evaluates
    // all demands and propagates changes through the supply chain. This is
    // heavier than a material-only propagation but is functionally correct:
    // MRP respects the operation plan dates already set by ACO and only
    // creates new supply where shortages exist. A dedicated
    // propagateMaterialChanges() would be lighter but requires refactoring
    // the solver infrastructure (TODO for future optimisation).
    if (config_.runMRP)
      SolverCreate::solve(v);

    // ---- Convergence check (skip on first and last iteration) ----
    if (outerIter > 0 && outerIter < config_.aco_mrp_iterations - 1) {
      double absDenom = max(abs(prevFitness), 1.0);
      double improvement = (lastBestFitness_ - prevFitness) / absDenom;
      if (improvement < config_.aco_mrp_improvement) {
        if (getLogLevel() > 1)
          logger << indentlevel << "ACO↔MRP converged after "
                 << (outerIter + 1) << " passes (Δf/|f|="
                 << improvement << " < " << config_.aco_mrp_improvement
                 << ")\n";
        break;
      }
    }
    prevFitness = lastBestFitness_;

    // Last iteration — don't re-collect, we're done.
    if (outerIter >= config_.aco_mrp_iterations - 1) break;

    // ---- Phase 3: Re-collect bottleneck resources ----
    // MRP may have changed the landscape: new supply operations created,
    // resource assignments altered, or dates updated. Re-scan so the
    // next ACO pass works with fresh data.
    vector<const Resource*> newBottleneck = collectBottlenecks();

    // Early termination: if the bottleneck set is identical AND the
    // previous ACO pass stagnated (no improvement within its own
    // iterations), further passes are unlikely to help.
    if (newBottleneck.size() == bottleneck.size() && stagnationOccurred_) {
      bool same = true;
      for (size_t i = 0; i < newBottleneck.size(); ++i) {
        if (newBottleneck[i] != bottleneck[i]) { same = false; break; }
      }
      if (same) {
        if (getLogLevel() > 1)
          logger << indentlevel << "ACO↔MRP: bottleneck set unchanged + "
                 << "ACO stagnated → stopping after " << (outerIter + 1)
                 << " passes\n";
        break;
      }
    }
    bottleneck = move(newBottleneck);
  }
}

}  // namespace ccAPPS
