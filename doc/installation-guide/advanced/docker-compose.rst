==============================
Deployment with docker compose
==============================

Here is a sample docker-compose file that defines 2 containers: 1) a postgres container
to run the database and 2) a ccAPPS web application server.

You access the application with your browser on the URL http://localhost:9000/

The ccAPPS log and configuration files are put in volumes (which allows to reuse
them between different releases of the ccAPPS image).

Note that the postgres database container comes with default settings. For production
use you should update the configuration with the pgtune recommendations from
https://pgtune.leopard.in.ua/ (use "data warehouse" as application type and also assure
the max_connections setting is moved from the default 100 to eg 400).

.. code-block:: none

  services:

    ccAPPS:
      image: "ghcr.io/ccAPPS/ccAPPS-community:latest"
      container_name: ccAPPS-community-webserver
      ports:
        - 9000:80
      depends_on:
        - ccAPPS-community-postgres
      networks:
        - backend
      volumes:
        - log-apache-community:/var/log/apache2
        - log-ccAPPS-community:/var/log/ccAPPS
        - config-ccAPPS-community:/etc/ccAPPS
        - config-apache-community:/etc/apache2
      environment:
        POSTGRES_HOST: "ccAPPS-community-postgres"
        POSTGRES_PORT: 5432
        POSTGRES_USER: "ccAPPS"
        POSTGRES_PASSWORD: "ccAPPS"
        ccAPPS_DATE_STYLE: "year-month-day"
        ccAPPS_DATE_STYLE_WITH_HOURS: "false"
        ccAPPS_TIME_ZONE: "UTC"
        ccAPPS_THEMES: "earth grass lemon odoo openbravo orange snow strawberry water"
        ccAPPS_DEFAULT_THEME: "earth"
        ccAPPS_EMAIL_USE_TLS: "true"
        ccAPPS_DEFAULT_FROM_EMAIL: "your_email@domain.com"
        ccAPPS_SERVER_EMAIL: "your_email@domain.com"
        ccAPPS_EMAIL_HOST_USER: "your_email@domain.com"
        ccAPPS_EMAIL_HOST_PASSWORD: "ccAPPSIsTheBest"
        ccAPPS_EMAIL_HOST: ""
        ccAPPS_EMAIL_PORT: 25
        ccAPPS_CONTENT_SECURITY_POLICY: "frame-ancestors 'self'"
        ccAPPS_X_FRAME_OPTIONS: "SAMEORIGIN"
        ccAPPS_CSRF_TRUSTED_ORIGINS: ""
        ccAPPS_SECURE_PROXY_SSL_HEADER: ""
        ccAPPS_SESSION_COOKIE_SECURE: "false"
        ccAPPS_CSRF_COOKIE_SAMESITE: "lax"
        ccAPPS_FTP_PROTOCOL: "SFTP"
        ccAPPS_FTP_HOST: ""
        ccAPPS_FTP_PORT: 22
        ccAPPS_FTP_USER: ""
        ccAPPS_FTP_PASSWORD: ""

    ccAPPS-community-postgres:
      image: "postgres:16"
      container_name: ccAPPS-community-postgres
      networks:
        - backend
      environment:
        POSTGRES_PASSWORD: ccAPPS
        POSTGRES_DB: ccAPPS
        POSTGRES_USER: ccAPPS
        POSTGRES_DBNAME: ccAPPS

  volumes:
    log-apache-community:
    log-ccAPPS-community:
    config-ccAPPS-community:
    config-apache-community:

  networks:
    backend:
