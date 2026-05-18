"""
ccAPPS Python module shim.

Loads the C++ shared library, initializes the ccAPPS module, and re-exports
all types and functions so that "import ccAPPS" works transparently.
"""
import ctypes
import os
import sys

# Set ccAPPS_HOME so the engine can find init files
os.environ.setdefault("ccAPPS_HOME", os.path.join(os.path.dirname(__file__), "bin"))

_RTLD_LAZY = 0x1
_RTLD_GLOBAL = 0x8

# Load the shared library. Python symbols are resolved from the python
# binary (which exports them globally) rather than from a separate
# libpython shared library.

_lib = ctypes.CDLL(
    os.path.join(os.path.dirname(__file__), "bin", "libccAPPS.so"),
    mode=_RTLD_LAZY | _RTLD_GLOBAL,
)

# Call ccAPPSInitialize to set up the Python module
# C++ mangled name for ccAPPSInitialize(bool) in Itanium ABI
_lib._Z16ccAPPSInitializeb.argtypes = [ctypes.c_bool]
_lib._Z16ccAPPSInitializeb.restype = None
_lib._Z16ccAPPSInitializeb(False)

# Re-export everything from the C++-created module into this namespace
import ccAPPS as _cpp_module
__all__ = [x for x in dir(_cpp_module) if not x.startswith("_")]
for _name in __all__:
    globals()[_name] = getattr(_cpp_module, _name)

# The C++ cache type is immutable, but the Django code expects a writable
# "maximum" attribute. Wrap it in a mutable proxy.
class _CacheProxy:
    def __init__(self, _cache_type):
        self.maximum = 1000000  # default value
        self._type = _cache_type
    def clear(self):
        return self._type.clear()
    def flush(self):
        return self._type.flush()
    def printStatus(self):
        return self._type.printStatus()
# Wrap the singleton instance (not the class), so flush/clear/printStatus
# work as bound methods. Capture it before the proxy clobbers the name below.
_cache_proxy = _CacheProxy(getattr(_cpp_module, "cache"))

# Fix truncated attribute names in the C++ module.
# The C++ code strips a module prefix but removes one character too many,
# leaving singular names without their first letter (e.g. "uffer" for "buffer").
# This mapping restores the correct names expected by the Django application.
_truncated_to_correct = {}
for _attr in dir(_cpp_module):
    if _attr.startswith("_") or len(_attr) < 3:
        continue
    # Compound names: truncated itemsupplier -> itemsupplier, etc.
    # The truncation only affects the FIRST segment, so:
    #   "tem_mts" -> "item_mts", "emand_forecast" -> "demand_forecast"
    #   "peration_fixed_time" -> "operation_fixed_time"
    # Plural/correct names already have the first letter, skip them.
    _obj = getattr(_cpp_module, _attr)
    if not callable(_obj):
        continue

# Map of known correct singular names to their truncated counterparts
_corrections = {
    # === Singular entity constructors (first letter truncated) ===
    "buffer": "uffer",
    "bucket": "ucket",
    "calendar": "alendar",
    "customer": "ustomer",
    "demand": "emand",
    "flow": "low",
    "interruption": "nterruption",
    "item": "tem",
    "load": "oad",
    "location": "ocation",
    "operation": "peration",
    "operationplan": "perationplan",
    "problem": "roblem",
    "resource": "esource",
    "setupmatrix": "etupmatrix",
    "skill": "kill",
    "solver": "olver",
    "suboperation": "uboperation",
    "supplier": "upplier",
    # === Default/alternate constructors ===
    "buffer_default": "uffer_default",
    "buffer_infinite": "uffer_infinite",
    "calendar_default": "alendar_default",
    "calendarEventIterator": "alendarEventIterator",
    "calendarIterator": "alendarIterator",
    "customer_default": "ustomer_default",
    "customerIterator": "ustomerIterator",
    "demand_default": "emand_default",
    "demand_forecast": "emand_forecast",
    "demand_forecastbucket": "emand_forecastbucket",
    "demand_forecastbucketIterator": "emand_forecastbucketIterator",
    "demand_group": "emand_group",
    "demandIterator": "emandIterator",
    "flow_transfer_batch": "low_transfer_batch",
    "flowIterator": "lowIterator",
    "flowplan": "lowplan",
    "flowplanIterator": "lowplanIterator",
    "item_mto": "tem_mto",
    "item_mts": "tem_mts",
    "itemIterator": "temIterator",
    "itemdistribution": "temdistribution",
    "itemdistributionIterator": "temdistributionIterator",
    "itemsupplier": "temsupplier",
    "itemsupplierIterator": "temsupplierIterator",
    "loadIterator": "oadIterator",
    "loadplan": "oadplan",
    "loadplanIterator": "oadplanIterator",
    "location_default": "ocation_default",
    "locationIterator": "ocationIterator",
    "measure": "easure",
    "measure_aggregated": "easure_aggregated",
    "measure_aggregatedplanned": "easure_aggregatedplanned",
    "measure_computed": "easure_computed",
    "measure_computedplanned": "easure_computedplanned",
    "measure_local": "easure_local",
    "measure_temp": "easure_temp",
    "operation_alternate": "peration_alternate",
    "operation_delivery": "peration_delivery",
    "operation_fixed_time": "peration_fixed_time",
    "operation_inventory": "peration_inventory",
    "operation_itemdistribution": "peration_itemdistribution",
    "operation_itemdistributionIterator": "peration_itemdistributionIterator",
    "operation_itemsupplier": "peration_itemsupplier",
    "operation_routing": "peration_routing",
    "operation_split": "peration_split",
    "operation_time_per": "peration_time_per",
    "operationIterator": "perationIterator",
    "operationdependency": "perationdependency",
    "operationdependencyIterator": "perationdependencyIterator",
    "operationplanIterator": "perationplanIterator",
    "operationplan_interruptionIterator": "perationplan interruptionIterator",
    "operationplandependency": "perationplandependency",
    "operationplandependencyIterator": "perationplandependencyIterator",
    "parameters": "arameters",
    "resource_buckets": "esource_buckets",
    "resource_default": "esource_default",
    "resource_infinite": "esource_infinite",
    "resourceIterator": "esourceIterator",
    "resourceplanIterator": "esourceplanIterator",
    "resourceskill": "esourceskill",
    "resourceskillIterator": "esourceskillIterator",
    "setupmatrix_default": "etupmatrix_default",
    "setupmatrixIterator": "etupmatrixIterator",
    "setupmatrixrule": "etupmatrixrule",
    "setupmatrixrule_default": "etupmatrixrule_default",
    "setupmatrixruleIterator": "etupmatrixruleIterator",
    "skill_default": "kill_default",
    "skillIterator": "killIterator",
    "solverACO": "olverACO",
    "solver_delete": "olver_delete",
    "solver_forecast": "olver_forecast",
    "solver_mrp": "olver_mrp",
    "solver_propagateStatus": "olver_propagateStatus",
    "supplier_default": "upplier_default",
    "supplierIterator": "upplierIterator",
    # === Other ===
    "cache": "ache",
    "commandmanager": "ommandmanager",
    "delete": "erase",
    "loggingDemandIterator": "eggingDemandIterator",
    "loggingIterator": "eggingIterator",
    "problemIterator": "roblemIterator",
    "setupevent": "etupevent",
    # === Alternate names ===
    "alternateOperationIterator": "lternateOperationIterator",
    "alternateResourceIterator": "lternateResourceIterator",
}

for _correct, _truncated in _corrections.items():
    if hasattr(_cpp_module, _truncated):
        # Set on both the C++ module (so import ccAPPS finds it) and
        # the current namespace (for internal use during this import)
        setattr(_cpp_module, _correct, getattr(_cpp_module, _truncated))
        globals()[_correct] = getattr(_cpp_module, _truncated)
        if _correct not in __all__:
            __all__.append(_correct)

# Replace the immutable cache type with the mutable proxy
setattr(_cpp_module, "cache", _cache_proxy)
globals()["cache"] = _cache_proxy
if "cache" not in __all__:
    __all__.append("cache")
