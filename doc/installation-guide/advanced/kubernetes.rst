==========================
Deployment with Kubernetes
==========================

A set of Kubernetes configuration files is available on
https://github.com/ccAPPS/ccAPPS/tree/master/contrib/kubernetes

Create a copy of these files on your machine. Then run the following commands
to deploy ccAPPS.

.. code-block:: bash

   kubectl apply -f ccAPPS-deployment.yaml,ccAPPS-postgres-deployment.yaml,ccAPPS-networkpolicy.yaml

The following resources are then defined in your cluster:

- A ccAPPS service that runs the ccAPPS planning engine and an Apache web server.
  It exposes port 80 for HTTP access to the application.

- A postgresql service to store the ccAPPS data.

- Persistent volumes to store the web server logs (50MB), the application logs (100MB)
  and the postgresql data (1GB).

- A network policy to keep the connection between ccAPPS and its postgres database private.
