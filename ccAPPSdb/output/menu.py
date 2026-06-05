# Copyright (C) 2013 by ccAPPS bv
#
# Permission is hereby granted, free of charge, to any person obtaining
# a copy of this software and associated documentation files (the
# "Software"), to deal in the Software without restriction, including
# without limitation the rights to use, copy, modify, merge, publish,
# distribute, sublicense, and/or sell copies of the Software, and to
# permit persons to whom the Software is furnished to do so, subject to
# the following conditions:
#
# The above copyright notice and this permission notice shall be
# included in all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
# NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
# LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
# OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
# WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
#

from ccAPPSdb.menu import menu
from ccAPPSdb.input.models import OperationPlanMaterial, OperationPlanResource
from ccAPPSdb.input.models import (
    Item,
    Location,
    Customer,
    OperationPlan,
    Operation,
    Resource,
    DistributionOrder,
    PurchaseOrder,
)
from ccAPPSdb.input.models import ManufacturingOrder, ItemDistribution, ItemSupplier
import ccAPPSdb.output.views.buffer
import ccAPPSdb.output.views.demand
import ccAPPSdb.output.views.problem
import ccAPPSdb.output.views.constraint
import ccAPPSdb.output.views.resource
import ccAPPSdb.output.views.operation


# ── Sales reports ──
menu.addItem(
    "supply_chain",
    "demand report",
    url="/demand/",
    report=ccAPPSdb.output.views.demand.OverviewReport,
    index=200,
    dependencies=[Item, Location, Customer, OperationPlan],
)
menu.addItem(
    "supply_chain",
    "problem report",
    url="/problem/?entity__in=demand,forecast",
    report=ccAPPSdb.output.views.problem.Report,
    index=400,
    dependencies=[Item, Location, Customer, OperationPlan],
)
menu.addItem(
    "supply_chain",
    "constraint report",
    url="/constraint/",
    report=ccAPPSdb.output.views.constraint.BaseReport,
    index=500,
    dependencies=[Item, Location, Customer, OperationPlan],
)

# ── Inventory reports ──
menu.addItem(
    "supply_chain",
    "distribution order summary",
    url="/distribution/",
    report=ccAPPSdb.output.views.operation.DistributionReport,
    index=2190,
    dependencies=[DistributionOrder, ItemDistribution],
)
menu.addItem(
    "supply_chain",
    "inventory report",
    url="/buffer/",
    report=ccAPPSdb.output.views.buffer.OverviewReport,
    index=2210,
    dependencies=[OperationPlanMaterial],
)
menu.addItem(
    "supply_chain",
    "problem report inventory",
    url="/problem/?entity=material",
    report=ccAPPSdb.output.views.problem.Report,
    index=2300,
    dependencies=[OperationPlanMaterial],
)

# ── Capacity reports ──
menu.addItem(
    "supply_chain",
    "resource report",
    url="/resource/",
    report=ccAPPSdb.output.views.resource.OverviewReport,
    index=3100,
    dependencies=[OperationPlanResource],
)
menu.addItem(
    "supply_chain",
    "problem report capacity",
    url="/problem/?entity=capacity",
    report=ccAPPSdb.output.views.problem.Report,
    index=3300,
    dependencies=[Resource],
)

# ── Purchasing reports ──
menu.addItem(
    "supply_chain",
    "purchase order summary",
    url="/purchase/",
    report=ccAPPSdb.output.views.operation.PurchaseReport,
    index=4200,
    dependencies=[PurchaseOrder, ItemSupplier],
)

# ── Manufacturing reports ──
menu.addItem(
    "supply_chain",
    "manufacturing order summary",
    url="/operation/",
    report=ccAPPSdb.output.views.operation.OverviewReport,
    index=5160,
    dependencies=[ManufacturingOrder, Operation],
)
menu.addItem(
    "supply_chain",
    "problem report manufacturing",
    url="/problem/?entity=operation",
    report=ccAPPSdb.output.views.problem.Report,
    index=5200,
    dependencies=[Operation],
)

# Admin problem report
menu.addItem(
    "execute",
    "problem report",
    url="/problem/?name=invalid%20data",
    report=ccAPPSdb.output.views.problem.Report,
    index=400,
)
