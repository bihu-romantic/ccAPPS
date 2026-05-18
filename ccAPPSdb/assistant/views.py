import json
import logging
import os
import subprocess

from django.conf import settings
from django.contrib.auth.decorators import login_required
from django.http import JsonResponse, StreamingHttpResponse
from django.views.decorators.csrf import csrf_exempt
from django.views.decorators.http import require_POST

logger = logging.getLogger(__name__)


def _get_claude_path():
    """Find the claude CLI binary."""
    for path in [
        "/usr/bin/claude",
        "/usr/local/bin/claude",
        os.path.expanduser("~/.local/bin/claude"),
        os.path.expanduser("~/.nvm/versions/node/v24.14.1/bin/claude"),
    ]:
        if os.path.isfile(path):
            return path
    # Also check PATH
    for d in os.environ.get("PATH", "").split(os.pathsep):
        p = os.path.join(d, "claude")
        if os.path.isfile(p):
            return p
    return "claude"


@csrf_exempt
@require_POST
@login_required
def chat(request):
    try:
        data = json.loads(request.body.decode("utf-8"))
        user_message = data.get("message", "").strip()
        page_url = data.get("page_url", "")
        page_title = data.get("page_title", "")
        context_data = data.get("context_data", "")
        history = data.get("history", [])

        if not user_message:
            return JsonResponse({"error": "Empty message"}, status=400)

        # Build the prompt in Chinese
        prompt_parts = [
            "你是集成在 ccAPPS 供应链规划平台中的AI助手。",
            "你帮助用户导航应用程序、理解数据和回答问题。",
            "请使用中文回复用户。",
            "",
            "重要安全规则：",
            "- 涉及 sudo、删除数据库/表、强制推送代码、修改系统配置等高风险操作时，必须先向用户说明风险并获得明确确认。",
            "- 不要绕过安全检查、不要禁用验证钩子、不要建议破坏性操作。",
            "- 如果用户请求危险操作，先解释更安全的替代方案。",
            "",
        ]

        if page_url or page_title or context_data:
            prompt_parts.append("=== 当前页面上下文 ===")
            if page_title:
                prompt_parts.append("页面标题: %s" % page_title)
            if page_url:
                prompt_parts.append("页面URL: %s" % page_url)
            if context_data:
                prompt_parts.append("页面数据: %s" % context_data)
            prompt_parts.append("")

        if history:
            prompt_parts.append("=== 对话历史 ===")
            for msg in history[-10:]:
                prompt_parts.append("%s: %s" % (msg["role"].capitalize(), msg["content"]))
            prompt_parts.append("")

        prompt_parts.append("=== 用户问题 ===")
        prompt_parts.append(user_message)

        prompt = "\n".join(prompt_parts)

        # Call claude CLI
        claude_path = _get_claude_path()
        env = os.environ.copy()
        env.setdefault("HOME", os.path.expanduser("~"))
        env.setdefault("PATH", os.environ.get("PATH", "/usr/local/bin:/usr/bin:/bin"))

        proc = subprocess.run(
            [claude_path, "-p", prompt],
            capture_output=True,
            text=True,
            timeout=getattr(settings, "CCAPPS_ASSISTANT_TIMEOUT", 120),
            env=env,
            cwd=os.path.expanduser("~"),
        )

        if proc.returncode == 0:
            return JsonResponse({"response": proc.stdout.strip()})
        else:
            logger.error("claude CLI error (code %s): %s", proc.returncode, proc.stderr)
            return JsonResponse(
                {"error": "CLI error: %s" % proc.stderr[-500:]}, status=500
            )

    except subprocess.TimeoutExpired:
        return JsonResponse({"error": "AI响应超时"}, status=504)
    except FileNotFoundError:
        return JsonResponse(
            {"error": "找不到Claude CLI，请确认claude已安装。"},
            status=503,
        )
    except json.JSONDecodeError:
        return JsonResponse({"error": "请求无效"}, status=400)
    except Exception as e:
        logger.exception("Assistant chat error")
        return JsonResponse({"error": str(e)}, status=500)


@csrf_exempt
def page_context(request):
    """Return current page context for debugging."""
    return JsonResponse(
        {
            "url": request.META.get("HTTP_REFERER", ""),
            "database": getattr(request, "database", None),
            "user": str(request.user),
        }
    )
