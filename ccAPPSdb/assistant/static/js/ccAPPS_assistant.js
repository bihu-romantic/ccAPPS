/**
 * ccAPPS AI 助手 - VSCode 风格分栏布局
 */
var assistantHistory = [];
var assistantLoading = false;
var assistantPanelWidth = 420;
var assistantStorageKey = 'ccAPPS_assistant_state';

(function() {

// 高风险操作关键词 - 需要用户确认
var highRiskPatterns = [
  /sudo\b/i,
  /rm\s+-rf/i,
  /DROP\s+(TABLE|DATABASE)/i,
  /DELETE\s+FROM/i,
  /TRUNCATE/i,
  /ALTER\s+(TABLE|SYSTEM)/i,
  /shutdown/i,
  /reboot/i,
  /kill\s+-9/i,
  /chmod\s+777/i,
  /chown\s+-R/i,
  /FORMAT\s+(C:|disk)/i,
  /dd\s+if=/i,
  /mkfs/i,
  /\bgit\s+push\s+--force\b/i,
  /\bgit\s+reset\s+--hard\b/i,
];

function isHighRisk(message) {
  for (var i = 0; i < highRiskPatterns.length; i++) {
    if (highRiskPatterns[i].test(message)) {
      return true;
    }
  }
  return false;
}

// ---- 跨页面状态持久化 ----

function saveState() {
  try {
    var state = {
      history: assistantHistory,
      panelWidth: assistantPanelWidth,
      panelHidden: document.getElementById('assistant-panel') ?
        document.getElementById('assistant-panel').classList.contains('assistant-hidden') : false,
      confirmed: localStorage.getItem('assistant_confirmed') === '1'
    };
    sessionStorage.setItem(assistantStorageKey, JSON.stringify(state));
  } catch(e) {}
}

function loadState() {
  try {
    var raw = sessionStorage.getItem(assistantStorageKey);
    if (!raw) return null;
    return JSON.parse(raw);
  } catch(e) {
    return null;
  }
}

function restoreMessages(container) {
  for (var i = 0; i < assistantHistory.length; i++) {
    var msg = assistantHistory[i];
    assistantAddMessageToContainer(container, msg.role, msg.content);
  }
  container.scrollTop = container.scrollHeight;
}

// ---- 布局初始化 ----

function initAssistantLayout() {
  var panel = document.getElementById('assistant-panel');
  if (!panel) return;

  // Restore saved state
  var saved = loadState();
  if (saved) {
    assistantHistory = saved.history || [];
    assistantPanelWidth = saved.panelWidth || 420;
    if (saved.panelWidth) {
      panel.style.width = saved.panelWidth + 'px';
    }
  }

  // Create main content wrapper
  var wrapper = document.createElement('div');
  wrapper.id = 'assistant-main-wrapper';

  // Move all body children into the wrapper EXCEPT assistant elements
  var body = document.body;
  var children = Array.from(body.children);
  for (var i = 0; i < children.length; i++) {
    var el = children[i];
    if (el.id === 'assistant-panel' || el.id === 'assistant-splitter'
        || el.id === 'assistant-reopen' || el.id === 'assistant-resize-overlay') {
      continue;
    }
    wrapper.appendChild(el);
  }

  // Create splitter
  var splitter = document.createElement('div');
  splitter.id = 'assistant-splitter';

  // Create resize overlay (catches mouse events during drag)
  var overlay = document.createElement('div');
  overlay.id = 'assistant-resize-overlay';

  // Create reopen button (hidden by default)
  var reopen = document.createElement('div');
  reopen.id = 'assistant-reopen';
  reopen.textContent = 'AI助手';
  reopen.title = '打开AI助手';
  reopen.addEventListener('click', function() {
    panel.classList.remove('assistant-hidden');
    saveState();
    // Scroll to bottom when re-opening
    var container = document.getElementById('assistant-messages');
    if (container) container.scrollTop = container.scrollHeight;
  });

  // Rebuild body: wrapper, splitter, panel, reopen, overlay
  body.appendChild(wrapper);
  body.appendChild(splitter);
  body.appendChild(panel);
  body.appendChild(reopen);
  body.appendChild(overlay);
  body.classList.add('assistant-layout');

  // Restore panel visibility - if previously hidden, keep it hidden
  if (saved && saved.panelHidden) {
    panel.classList.add('assistant-hidden');
  }

  // Restore chat messages
  if (saved && saved.history && saved.history.length > 0) {
    var container = document.getElementById('assistant-messages');
    if (container) {
      // Clear default greeting message
      container.innerHTML = '';
      restoreMessages(container);
    }
  }

  // Splitter drag behavior
  var startX, startWidth;
  splitter.addEventListener('mousedown', function(e) {
    e.preventDefault();
    startX = e.clientX;
    startWidth = panel.offsetWidth;
    splitter.classList.add('dragging');
    overlay.style.display = 'block';
    document.body.style.userSelect = 'none';
  });

  document.addEventListener('mousemove', function(e) {
    if (!splitter.classList.contains('dragging')) return;
    var dx = startX - e.clientX;
    var newWidth = startWidth + dx;
    var maxWidth = window.innerWidth - 400;
    newWidth = Math.max(280, Math.min(newWidth, maxWidth));
    panel.style.width = newWidth + 'px';
    assistantPanelWidth = newWidth;
  });

  document.addEventListener('mouseup', function() {
    if (splitter.classList.contains('dragging')) {
      splitter.classList.remove('dragging');
      overlay.style.display = 'none';
      document.body.style.userSelect = '';
      saveState();
    }
  });

  // Close button
  var closeBtn = document.getElementById('assistant-close');
  if (closeBtn) {
    closeBtn.addEventListener('click', function() {
      panel.classList.add('assistant-hidden');
      saveState();
    });
  }

  // Confirmation overlay
  var confirmOverlay = document.getElementById('assistant-confirm');
  if (confirmOverlay && localStorage.getItem('assistant_confirmed') === '1') {
    confirmOverlay.classList.add('assistant-confirmed');
  }
}

// ---- 聊天功能 ----

function assistantSend() {
  if (assistantLoading) return;
  var input = document.getElementById('assistant-input');
  if (!input) return;
  var msg = input.value.trim();
  if (!msg) return;

  // 高风险操作确认
  if (isHighRisk(msg)) {
    var confirmed = confirm(
      '⚠️ 高风险操作警告\n\n' +
      '你的请求包含高风险操作（如 sudo、删除数据库、强制推送等）。\n\n' +
      '请确认你了解此操作的后果。AI助手不对系统级操作负责。\n\n' +
      '点击"确定"继续发送，点击"取消"放弃。'
    );
    if (!confirmed) return;
  }

  assistantAddMessage('user', msg);
  input.value = '';
  input.style.height = 'auto';

  // Collect page context
  var pageContext = {};
  try {
    var tables = document.querySelectorAll('#assistant-main-wrapper table');
    if (tables.length > 0) {
      var tableData = [];
      for (var i = 0; i < Math.min(tables.length, 3); i++) {
        var rows = tables[i].querySelectorAll('tr');
        var tdata = [];
        for (var j = 0; j < Math.min(rows.length, 20); j++) {
          var cells = rows[j].querySelectorAll('td, th');
          var row = [];
          for (var k = 0; k < cells.length; k++) {
            row.push(cells[k].textContent.trim());
          }
          if (row.length > 0) tdata.push(row);
        }
        if (tdata.length > 0) tableData.push(tdata);
      }
      if (tableData.length > 0) pageContext.tables = tableData;
    }

    var forms = document.querySelectorAll('#assistant-main-wrapper form');
    if (forms.length > 0) {
      var formInfo = [];
      for (var i = 0; i < Math.min(forms.length, 2); i++) {
        var inputs = forms[i].querySelectorAll('input:not([type="hidden"]), select, textarea');
        var finfo = {};
        for (var j = 0; j < inputs.length; j++) {
          var inp = inputs[j];
          if (inp.name && inp.value) {
            finfo[inp.name] = inp.value;
          }
        }
        if (Object.keys(finfo).length > 0) formInfo.push(finfo);
      }
      if (formInfo.length > 0) pageContext.forms = formInfo;
    }
  } catch (e) {}

  try {
    var crumbs = document.querySelectorAll('#assistant-main-wrapper #breadcrumbs .breadcrumb-item');
    var crumbTexts = [];
    for (var i = 0; i < crumbs.length; i++) {
      crumbTexts.push(crumbs[i].textContent.trim());
    }
    if (crumbTexts.length > 0) pageContext.breadcrumbs = crumbTexts;
  } catch (e) {}

  assistantHistory.push({role: 'user', content: msg});
  saveState();
  assistantLoading = true;
  assistantShowLoading(true);

  var payload = {
    message: msg,
    page_url: window.location.href,
    page_title: document.title,
    context_data: JSON.stringify(pageContext),
    history: assistantHistory.slice(-20)
  };

  fetch(url_prefix + '/assistant/chat/', {
    method: 'POST',
    headers: {
      'Content-Type': 'application/json',
      'X-CSRFToken': assistantGetCookie('csrftoken'),
      'X-Requested-With': 'XMLHttpRequest'
    },
    body: JSON.stringify(payload)
  })
  .then(function(response) {
    return response.json();
  })
  .then(function(data) {
    assistantLoading = false;
    assistantShowLoading(false);
    if (data.error) {
      assistantAddMessage('system', '错误：' + data.error);
    } else if (data.response) {
      assistantHistory.push({role: 'assistant', content: data.response});
      saveState();
      assistantAddMessage('assistant', data.response);
    }
  })
  .catch(function(err) {
    assistantLoading = false;
    assistantShowLoading(false);
    assistantAddMessage('system', '连接错误：' + err.message);
  });
}

function assistantAddMessage(role, content) {
  var container = document.getElementById('assistant-messages');
  if (!container) return;
  assistantAddMessageToContainer(container, role, content);
  container.scrollTop = container.scrollHeight;
}

function assistantAddMessageToContainer(container, role, content) {
  var div = document.createElement('div');
  div.className = 'assistant-message assistant-message-' + role;

  var contentDiv = document.createElement('div');
  contentDiv.className = 'assistant-message-content';
  contentDiv.innerHTML = assistantFormatMessage(content);

  div.appendChild(contentDiv);
  container.appendChild(div);
}

function assistantFormatMessage(text) {
  var escaped = text
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;');
  escaped = escaped.replace(/```(\w*)\n([\s\S]*?)```/g, '<pre><code>$2</code></pre>');
  escaped = escaped.replace(/`([^`]+)`/g, '<code>$1</code>');
  escaped = escaped.replace(/\*\*([^*]+)\*\*/g, '<strong>$1</strong>');
  escaped = escaped.replace(/\n/g, '<br>');
  return escaped;
}

function assistantShowLoading(show) {
  var container = document.getElementById('assistant-messages');
  var existing = document.getElementById('assistant-loading');
  if (show) {
    if (!existing) {
      var div = document.createElement('div');
      div.id = 'assistant-loading';
      div.className = 'assistant-message assistant-message-system';
      div.innerHTML = '<div class="assistant-message-content"><span class="assistant-dot"></span><span class="assistant-dot"></span><span class="assistant-dot"></span></div>';
      container.appendChild(div);
      container.scrollTop = container.scrollHeight;
    }
  } else {
    if (existing) existing.remove();
  }
}

function assistantKeyDown(event) {
  if (event.key === 'Enter' && !event.shiftKey) {
    event.preventDefault();
    assistantSend();
  }
}

function assistantGetCookie(name) {
  var value = '; ' + document.cookie;
  var parts = value.split('; ' + name + '=');
  if (parts.length === 2) return parts.pop().split(';').shift();
  return '';
}

function assistantConfirm() {
  var overlay = document.getElementById('assistant-confirm');
  if (overlay) {
    overlay.classList.add('assistant-confirmed');
    localStorage.setItem('assistant_confirmed', '1');
  }
  saveState();
  var input = document.getElementById('assistant-input');
  if (input) input.focus();
}

// Expose to global scope for onclick handlers
window.assistantSend = assistantSend;
window.assistantKeyDown = assistantKeyDown;
window.assistantConfirm = assistantConfirm;

// Initialize when DOM is ready
function bindInputEvents() {
  var input = document.getElementById('assistant-input');
  if (input) {
    input.addEventListener('input', function() {
      this.style.height = 'auto';
      this.style.height = Math.min(this.scrollHeight, 120) + 'px';
    });
  }
}

if (document.readyState === 'loading') {
  document.addEventListener('DOMContentLoaded', function() {
    initAssistantLayout();
    bindInputEvents();
  });
} else {
  initAssistantLayout();
  bindInputEvents();
}

})();
