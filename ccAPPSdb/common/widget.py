#
# Copyright (C) 2016 by ccAPPS bv
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

from django.conf import settings
from django.db import DEFAULT_DB_ALIAS
from django.http import HttpResponse
from django.utils import formats
from django.utils.encoding import force_str
from django.utils.html import escape
from django.utils.text import capfirst
from django.utils.timesince import timesince
from django.utils.translation import gettext_lazy as _

from ccAPPSdb import __version__
from ccAPPSdb.common.dashboard import Dashboard, Widget
from ccAPPSdb.common.models import Notification


class WelcomeWidget(Widget):
    name = "welcome"
    title = _("Welcome")
    tooltip = _("Some links to get started")
    asynchronous = False
    size = 'xl'

    @staticmethod
    def render(request):
        try:
            versionnumber = __version__.split(".", 2)
            docurl = "%s/docs/%s.%s/index.html" % (
                settings.DOCUMENTATION_URL,
                versionnumber[0],
                versionnumber[1],
            )
        except Exception:
            docurl = "%s/docs/current/index.html" % (settings.DOCUMENTATION_URL,)

        try:
            db = request.database
            if not db or db == DEFAULT_DB_ALIAS:
                prefix = ""
            else:
                prefix = "/%s" % db
        except Exception:
            prefix = ""
        return (
            _(
                """Welcome to the world's leading open source production planning tool!<br><br>
How to get started?
<ol>
<li>Check out the <span class="text-decoration-underline"><a href="%(docurl)s" target="_blank" rel="noopener">documentation</a></span></li>
<li>Visit and join the <span class="text-decoration-underline"><a href="https://github.com/ccAPPS/ccAPPS/discussions" target="_blank" rel="noopener">user community</a></span></li>
<li><span class="text-decoration-underline"><a href="https://ccAPPS.com/company/#contact" target="_blank" rel="noopener">Contact us</a></span></li>
</ol>
"""
            )
            % {"docurl": docurl, "prefix": prefix}
        )


Dashboard.register(WelcomeWidget)


class NewsWidget(Widget):
    name = "news"
    title = _("News")
    tooltip = _("Show the latest news items from the ccAPPS website")
    asynchronous = False
    size = 'md'

    @staticmethod
    def render(request):
        return '<iframe style="width:100%; border:none;" src="https://ccAPPS.com/news-summary/"></iframe>'


Dashboard.register(NewsWidget)


class InboxWidget(Widget):
    name = "inbox"
    title = _("inbox")
    tooltip = _("Unread messages from your inbox")
    url = "/inbox/"
    asynchronous = False
    size = 'md'
    limit = 10

    @classmethod
    def render(cls, request):
        notifs = list(
            Notification.objects.using(request.database)
            .filter(user=request.user)
            .order_by("-id")
            .select_related("comment", "user")[: cls.limit]
        )
        if not notifs:
            return """
              <div class="pull-left"><i class="fa fa-5x fa-trophy" style="color: gold"></i>&nbsp;&nbsp;</div>
              <h2>Congrats!</h2>
              Your inbox is empty.
              """
        result = []
        result.append(
            '<div class="table-responsive"><table class="table table-sm table-hover"><tbody>'
        )
        for notif in notifs:
            result.append(
                """<tr><td>
                <a class="text-decoration-underline" href="%s%s">%s</a>&nbsp;<span class="small">%s</span>
                <div class="small pull-right" data-bs-toggle="tooltip" data-bs-title="%s %s">%s%s&nbsp;&nbsp;%s</div>
                <br><p style="padding-left: 10px; display: inline-block">%s</p>"""
                % (
                    request.prefix,
                    notif.comment.getURL(),
                    notif.comment.object_repr,
                    escape(
                        capfirst(force_str(_(notif.comment.content_type.name)))
                        if notif.comment.content_type
                        else ""
                    ),
                    escape(notif.comment.user.get_full_name()),
                    formats.date_format(notif.comment.lastmodified, "DATETIME_FORMAT"),
                    (
                        '<img class="avatar-sm" src="/uploads/%s">&nbsp;'
                        % notif.comment.user.avatar
                        if notif.comment.user.avatar
                        else ""
                    ),
                    escape(notif.comment.user.username),
                    timesince(notif.comment.lastmodified),
                    (
                        notif.comment.comment
                        if notif.comment.safe()
                        else escape(notif.comment.comment)
                    ),
                )
                + "</td></tr>"
            )
        result.append("</tbody></table></div>")
        return "\n".join(result)

    javascript = """
    var hasForecast = %s;
    var hasIP = %s;
    """ % (
        "true" if "ccAPPSdb.forecast" in settings.INSTALLED_APPS else "false",
        "true" if "ccAPPSdb.inventoryplanning" in settings.INSTALLED_APPS else "false",
    )


Dashboard.register(InboxWidget)


class SupplyChainGraphWidget(Widget):
    name = "supplychaingraph"
    title = _("Supply Chain Network")
    tooltip = _("Interactive graph of supply chain buffers, operations, and flows")
    asynchronous = True
    size = "xl"

    javascript = r"""
var container = document.getElementById('supplychaingraph');
if (!container) return;
var jsonEl = document.getElementById('supplychaingraph-json');
if (!jsonEl) return;

var graphData;
try { graphData = JSON.parse(jsonEl.textContent); }
catch(e) { container.innerHTML = '<div class="alert alert-warning p-3">Failed to load graph data</div>'; return; }

var typeColors = {
    item:        {background:'#D2E5FF',border:'#2B7CE9',highlight:{background:'#A8D0FF',border:'#1A5CB8'}},
    location:    {background:'#D5F5E3',border:'#27AE60',highlight:{background:'#ABEBC6',border:'#1E8449'}},
    operation:   {background:'#FDEBD0',border:'#E67E22',highlight:{background:'#FAD7A0',border:'#CA6F1E'}},
    resource:    {background:'#FADBD8',border:'#E74C3C',highlight:{background:'#F5B7B1',border:'#CB4335'}},
    supplier:    {background:'#E8DAEF',border:'#8E44AD',highlight:{background:'#D7BDE2',border:'#7D3C98'}},
    customer:    {background:'#D1F2EB',border:'#17A589',highlight:{background:'#A3E4D7',border:'#148F77'}},
    demand:      {background:'#FCF3CF',border:'#F1C40F',highlight:{background:'#F9E79F',border:'#D4AC0D'}}
};
var typeShapes = {
    item:'box', location:'hexagon', operation:'box', resource:'diamond',
    supplier:'triangle', customer:'triangleDown', demand:'star'
};
var typeLabels = {
    item:'Item', location:'Location', operation:'Operation',
    resource:'Resource', supplier:'Supplier', customer:'Customer', demand:'Demand'
};

// Compute node degrees from edges
var nodeDegrees = {};
graphData.edges.forEach(function(e) {
    nodeDegrees[e.from] = (nodeDegrees[e.from]||0) + 1;
    nodeDegrees[e.to] = (nodeDegrees[e.to]||0) + 1;
});

var nodes = new vis.DataSet(graphData.nodes.map(function(n) {
    var deg = nodeDegrees[n.id] || 0;
    var c = typeColors[n.type] || typeColors.item;
    // Size: min 8, max 50, scaled by log(degree+1)
    var sz = Math.round(8 + Math.log(deg + 1) * 12);
    return {id:n.id, label:n.label, shape:typeShapes[n.type]||'dot',
            color:c, title:n.title||'', group:n.type, url:n.url||null,
            value:sz, font:{size:Math.max(9, Math.min(14, sz-2))}};
}));

var edges = new vis.DataSet(graphData.edges.map(function(e) {
    var style = {};
    if (e.dashes) style.dashes = e.dashes;
    if (e.color) style.color = e.color;
    return {id:e.id, from:e.from, to:e.to, label:e.label||'',
            arrows:e.arrows||'', dashes:style.dashes, color:style.color,
            width:Math.max(0.5, Math.min(3, e.width||1)), title:e.title||''};
}));

var layoutMode = 'force'; // 'force' | 'hierarchical'
var physicsOpts = {
    barnesHut:{gravitationalConstant:-2000,centralGravity:0.2,springLength:180,
             springConstant:0.02,damping:0.5,avoidOverlap:0.3},
    minVelocity:0.5,maxVelocity:25,solver:'barnesHut',
    stabilization:{iterations:300,fit:true}
};
var hierarchicalOpts = {
    enabled:false, direction:'LR',
    sortMethod:'directed',nodeSpacing:120,levelSeparation:200,
    blockShifting:true,edgeMinimization:true,parentCentralization:false
};

var network = new vis.Network(container, {nodes:nodes, edges:edges}, {
    physics:physicsOpts,
    interaction:{hover:true,tooltipDelay:200,navigationButtons:false,keyboard:true},
    nodes:{borderWidth:2,margin:8,scaling:{min:8,max:50,label:{enabled:true,min:9,max:14}}},
    edges:{font:{size:9,align:'middle'},smooth:{type:'continuous'},selectionWidth:2,
           arrows:{to:{scaleFactor:0.6}}},
    layout:{improvedLayout:true, hierarchical:hierarchicalOpts}
});

// Per-type dropdowns + type filter checkboxes
var toolbar = document.getElementById('supplychaingraph-toolbar');
var selStyle = 'font-size:11px;padding:2px 3px;border:1px solid #ccc;border-radius:3px;max-width:160px;background:#fff';

function focusNode(nid) {
    // Unhide all first
    nodes.forEach(function(n) { n.hidden = false; nodes.update(n); });
    edges.forEach(function(e) { e.hidden = false; edges.update(e); });
    // Focus on selected node and its neighbors
    var connected = network.getConnectedNodes(nid);
    connected.push(nid);
    var allIds = {};
    connected.forEach(function(id) { allIds[id] = true; });
    nodes.forEach(function(n) {
        n.hidden = !allIds[n.id];
        nodes.update(n);
    });
    edges.forEach(function(e) {
        e.hidden = !(allIds[e.from] && allIds[e.to]);
        edges.update(e);
    });
    network.fit({animation:{duration:500, easingFunction:'easeInOutQuad'}});
}

function resetAll() {
    nodes.forEach(function(n) { n.hidden=false; nodes.update(n); });
    edges.forEach(function(e) { e.hidden=false; edges.update(e); });
    network.fit({animation:{duration:300, easingFunction:'easeInOutQuad'}});
    // Reset all dropdowns to blank
    if (toolbar) {
        toolbar.querySelectorAll('select.node-filter').forEach(function(s) { s.value = ''; });
    }
}

function toggleLayout() {
    if (layoutMode === 'force') {
        layoutMode = 'hierarchical';
        network.setOptions({physics:false, layout:{hierarchical:{enabled:true, direction:'LR',
            sortMethod:'directed',nodeSpacing:120,levelSeparation:200,
            blockShifting:true,edgeMinimization:true,parentCentralization:false}}});
        // Re-show all nodes for full picture
        resetAll();
    } else {
        layoutMode = 'force';
        network.setOptions({physics:physicsOpts, layout:{hierarchical:{enabled:false}}});
    }
}

function filterByDegree(minDeg) {
    nodes.forEach(function(n) {
        var deg = nodeDegrees[n.id] || 0;
        if (deg < minDeg) { n.hidden = true; nodes.update(n); }
        else { n.hidden = false; nodes.update(n); }
    });
    edges.forEach(function(e) {
        var fn = nodes.get(e.from), tn = nodes.get(e.to);
        e.hidden = (fn && fn.hidden) || (tn && tn.hidden);
        edges.update(e);
    });
    if (minDeg === 0) resetAll();
}

// Toolbar buttons (add before dropdowns/checkboxes)
if (toolbar) {
    toolbar.innerHTML = '';
    var btnStyle = 'padding:2px 8px;font-size:12px;border:1px solid #ccc;background:#fff;border-radius:3px;cursor:pointer;margin-right:4px';

    // Layout toggle
    var layoutBtn = document.createElement('button');
    layoutBtn.textContent = '⇋ hierarchy';
    layoutBtn.style.cssText = btnStyle;
    layoutBtn.title = 'Toggle between force-directed and hierarchical layout';
    layoutBtn.addEventListener('click', function() {
        toggleLayout();
        this.textContent = layoutMode==='force' ? '⇋ hierarchy' : '⇋ force';
    });
    toolbar.appendChild(layoutBtn);

    // Min degree filter
    var degSel = document.createElement('select');
    degSel.style.cssText = 'font-size:11px;padding:2px 3px;border:1px solid #ccc;border-radius:3px;background:#fff';
    degSel.title = 'Hide nodes with fewer connections';
    [{v:0,l:'all nodes'},{v:1,l:'≥ 1 connection'},{v:2,l:'≥ 2 connections'},{v:3,l:'≥ 3 connections'},{v:5,l:'≥ 5 connections'}].forEach(function(o) {
        var opt = document.createElement('option');
        opt.value = o.v; opt.textContent = o.l;
        degSel.appendChild(opt);
    });
    degSel.addEventListener('change', function() { filterByDegree(parseInt(this.value)); });
    toolbar.appendChild(degSel);

    var sep = document.createElement('span'); sep.style.cssText = 'margin:0 6px;color:#ccc'; sep.textContent = '|';
    toolbar.appendChild(sep);

    // Per-type dropdowns + checkboxes
    var typeOrder = ['item','location','operation','resource','supplier','customer','demand'];
    var selStyle = 'font-size:11px;padding:2px 3px;border:1px solid #ccc;border-radius:3px;max-width:160px;background:#fff';

    typeOrder.forEach(function(t) {
        var nodeList = nodes.get({filter:function(n){return n.group===t;}, order:function(a,b){return a.label<b.label?-1:a.label>b.label?1:0;}});
        if (nodeList.length === 0) return;

        // Dropdown
        var sel = document.createElement('select');
        sel.className = 'node-filter';
        sel.style.cssText = selStyle;
        sel.setAttribute('data-type', t);
        var opt = document.createElement('option');
        opt.value = ''; opt.textContent = '-- ' + typeLabels[t] + ' (' + nodeList.length + ') --';
        sel.appendChild(opt);
        nodeList.forEach(function(n) {
            var o = document.createElement('option');
            o.value = n.id; o.textContent = n.label;
            sel.appendChild(o);
        });
        sel.addEventListener('change', function() {
            if (this.value) {
                focusNode(this.value);
                toolbar.querySelectorAll('select.node-filter').forEach(function(s) { if (s !== this) s.value = ''; }, this);
            } else {
                resetAll();
            }
        });
        toolbar.appendChild(sel);

        // Checkbox
        var label = document.createElement('label');
        label.style.cssText = 'margin-right:10px;font-size:11px;cursor:pointer;user-select:none;white-space:nowrap';
        var cb = document.createElement('input');
        cb.type = 'checkbox'; cb.checked = true; cb.value = t;
        cb.style.cssText = 'margin-right:2px;vertical-align:middle';
        cb.addEventListener('change', function() {
            var show = this.checked ? true : false;
            var tp = this.value;
            nodes.forEach(function(n) { if (n.group===tp) { n.hidden = !show; nodes.update(n); } });
            edges.forEach(function(e) {
                var fn = nodes.get(e.from), tn = nodes.get(e.to);
                if (fn && tn) { e.hidden = fn.hidden || tn.hidden; edges.update(e); }
            });
        });
        var dot = document.createElement('span');
        dot.style.cssText = 'display:inline-block;width:10px;height:10px;border-radius:2px;margin-right:3px;vertical-align:middle;background:'+(typeColors[t]?typeColors[t].background:'#ccc')+';border:1px solid '+(typeColors[t]?typeColors[t].border:'#999');
        label.appendChild(cb); label.appendChild(dot);
        label.appendChild(document.createTextNode(typeLabels[t]));
        toolbar.appendChild(label);
    });

    if (graphData.truncated) {
        var note = document.createElement('span');
        note.style.cssText = 'font-size:11px;color:#999;margin-left:8px';
        note.textContent = '(truncated)';
        toolbar.appendChild(note);
    }

    // Zoom buttons
    var sep2 = document.createElement('span'); sep2.style.cssText = 'margin:0 4px;color:#ccc'; sep2.textContent = '|';
    toolbar.appendChild(sep2);
    ['+', '-', '⤡'].forEach(function(act) {
        var b = document.createElement('button'); b.textContent = act; b.style.cssText = btnStyle;
        b.addEventListener('click', function() {
            if (act==='+') network.moveTo({scale:network.getScale()*1.3});
            else if (act==='-') network.moveTo({scale:network.getScale()*0.7});
            else network.fit({animation:true});
        });
        toolbar.appendChild(b);
    });
    var rstBtn = document.createElement('button'); rstBtn.textContent = 'reset'; rstBtn.style.cssText = btnStyle;
    rstBtn.addEventListener('click', resetAll);
    toolbar.appendChild(rstBtn);
}

// Double-click: focus on node neighborhood, Ctrl+Click to navigate
var navClickTimer = null;
network.on('click', function(params) {
    if (params.nodes.length === 0) {
        resetAll();
        return;
    }
    if (params.nodes.length === 1 && params.event && (params.event.ctrlKey || params.event.metaKey)) {
        var n = nodes.get(params.nodes[0]);
        if (n && n.url) window.open(n.url, '_blank');
    }
});

network.on('doubleClick', function(params) {
    if (navClickTimer) { clearTimeout(navClickTimer); navClickTimer = null; }
    if (params.nodes.length === 0) return;
    focusNode(params.nodes[0]);
});

// Resize observer
if (window.ResizeObserver) {
    new ResizeObserver(function() { network.redraw(); }).observe(container);
}
"""

    @classmethod
    def render(cls, request):
        import json
        from django.db import connections

        nodes = []
        node_set = {}
        edges = []
        truncated = False
        max_nodes = 600

        with connections[request.database].cursor() as cursor:
            # --- Nodes ---
            node_queries = [
                ("item", "SELECT name FROM item ORDER BY name LIMIT 200", None),
                ("location", "SELECT name FROM location ORDER BY name LIMIT 80", None),
                ("operation", "SELECT name, type FROM operation ORDER BY name LIMIT 150", None),
                ("resource", "SELECT name FROM resource ORDER BY name LIMIT 80", None),
                ("supplier", "SELECT name FROM supplier ORDER BY name LIMIT 50", None),
                ("customer", "SELECT name FROM customer ORDER BY name LIMIT 50", None),
                ("demand", "SELECT name, item_id, customer_id, location_id, quantity FROM demand ORDER BY name LIMIT 150", None),
            ]

            for ntype, sql, _params in node_queries:
                cursor.execute(sql)
                for row in cursor.fetchall():
                    nid = f"{ntype}:{row[0]}"
                    if nid in node_set:
                        continue
                    label = row[0]
                    title = f"{ntype.capitalize()}: {row[0]}"
                    url = cls._node_url(request, ntype, row)
                    node_set[nid] = {
                        "id": nid, "label": label, "type": ntype,
                        "title": title, "url": url,
                    }

            # If too many nodes, trim evenly across types
            if len(node_set) > max_nodes:
                truncated = True
                kept = {}
                ratio = max_nodes / len(node_set)
                from collections import Counter

                type_counts = Counter()
                for nid, nd in node_set.items():
                    type_counts[nd["type"]] += 1

                type_limits = {t: max(5, int(c * ratio)) for t, c in type_counts.items()}
                type_seen = Counter()
                for nid, nd in node_set.items():
                    t = nd["type"]
                    if type_seen[t] < type_limits[t]:
                        kept[nid] = nd
                        type_seen[t] += 1
                node_set = kept

            # --- Edges ---
            node_ids = set(node_set.keys())
            edge_id_counter = [0]

            def add_edge(frm, to, label="", arrows="to", dashes=None, color=None, width=1, title=""):
                if frm in node_ids and to in node_ids:
                    eid = edge_id_counter[0]
                    edge_id_counter[0] += 1
                    e = {"id": eid, "from": frm, "to": to, "label": label,
                         "arrows": arrows, "width": width, "title": title}
                    if dashes:
                        e["dashes"] = dashes
                    if color:
                        e["color"] = color
                    edges.append(e)

            # Buffer: Item -> Location
            cursor.execute("SELECT item_id, location_id FROM buffer LIMIT 600")
            for row in cursor.fetchall():
                add_edge(f"item:{row[0]}", f"location:{row[1]}", "stocked at", arrows="to", color={"color": "#888888"})

            # OperationMaterial: BOM inputs (quantity<0 => Item->Operation), outputs (quantity>0 => Operation->Item)
            cursor.execute(
                "SELECT operation_id, item_id, quantity FROM operationmaterial LIMIT 1200"
            )
            for row in cursor.fetchall():
                if row[2] is not None and float(row[2]) < 0:
                    add_edge(f"item:{row[1]}", f"operation:{row[0]}", "consumed by", arrows="to",
                             color={"color": "#E74C3C"}, dashes=[5, 5])
                else:
                    add_edge(f"operation:{row[0]}", f"item:{row[1]}", "produces", arrows="to",
                             color={"color": "#27AE60"})

            # OperationResource: Operation -> Resource
            cursor.execute("SELECT operation_id, resource_id FROM operationresource LIMIT 600")
            for row in cursor.fetchall():
                add_edge(f"operation:{row[0]}", f"resource:{row[1]}", "uses", arrows="to", color={"color": "#8E44AD"})

            # ItemSupplier: Item -> Supplier
            cursor.execute("SELECT item_id, supplier_id FROM itemsupplier LIMIT 600")
            for row in cursor.fetchall():
                add_edge(f"item:{row[0]}", f"supplier:{row[1]}", "sourced from", arrows="to", color={"color": "#2980B9"})

            # ItemDistribution: Location(origin) -> Location(destination)
            cursor.execute(
                "SELECT origin_id, location_id, item_id FROM itemdistribution LIMIT 600"
            )
            for row in cursor.fetchall():
                add_edge(f"location:{row[0]}", f"location:{row[1]}", row[2] or "item", arrows="to",
                         color={"color": "#17A589"}, dashes=[8, 4])

            # Demand -> Item, Customer, Location
            cursor.execute(
                "SELECT name, item_id, customer_id, location_id FROM demand LIMIT 600"
            )
            for row in cursor.fetchall():
                dem_nid = f"demand:{row[0]}"
                for prefix, col_idx in [("item", 1), ("customer", 2), ("location", 3)]:
                    if row[col_idx]:
                        add_edge(dem_nid, f"{prefix}:{row[col_idx]}", "demands" if prefix == "item" else f"for {prefix}",
                                 arrows="to", color={"color": "#F39C12"})

        # Build nodes list
        for nd in node_set.values():
            nodes.append({
                "id": nd["id"], "label": nd["label"], "type": nd["type"],
                "title": nd["title"], "url": nd.get("url"),
            })

        data = {"nodes": nodes, "edges": edges, "truncated": truncated}
        html = [
            '<div id="supplychaingraph-toolbar" style="margin-bottom:6px;display:flex;flex-wrap:wrap;gap:4px;align-items:center"></div>',
            '<div id="supplychaingraph" style="width:100%;height:550px;border:1px solid #dee2e6;border-radius:4px"></div>',
            '<script type="application/json" id="supplychaingraph-json">',
            json.dumps(data, ensure_ascii=False),
            '</script>',
        ]
        return HttpResponse("\n".join(html))

    @staticmethod
    def _node_url(request, ntype, row):
        """Build a detail URL for a graph node."""
        name = row[0]
        prefix = request.prefix
        supplypath_types = {"item", "buffer", "resource", "demand", "operation"}
        if ntype in supplypath_types:
            return f"{prefix}/supplypath/{ntype}/{name}/"
        elif ntype in ("location", "supplier", "customer"):
            return f"{prefix}/data/input/{ntype}/"
        return None


Dashboard.register(SupplyChainGraphWidget)
