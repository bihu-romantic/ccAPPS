from django.urls import re_path
from ccAPPSdb.assistant import views

autodiscover = True

urlpatterns = [
    re_path(r"^assistant/chat/$", views.chat, name="assistant_chat"),
    re_path(r"^assistant/context/$", views.page_context, name="assistant_context"),
]
