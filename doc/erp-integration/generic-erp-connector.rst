=====================
Generic ERP connector
=====================

This represents a skeleton addon app for a two-way integration to an ERP system.

The application is structured to keep the code structure clean and simple, making it
an ideal starting point for integration ccAPPS in your IT landscape.

The technical implementation uses an SQL connection the ERP database
to read and write the relevant information. The ERP data are extracted as a
set of CSV files that can be loaded in ccAPPS. Variations can easily be
created to use different technologies.


How to use the connector?
-------------------------

* The connector is run from the execution screen.

* After running the connector you'll see a series of flat files
  in the import folder. When using the Cloud Edition you can upload
  files you have extracted locally from your ERP.

* Now you can import the data in ccAPPS. During the data loading
  all data will be validated and any data errors will be reported.

* Now you can generate your plan.


* After reviewing the plan you can export the planning results.
  You can do this incrementally for individual transactions, or
  in bulk mode for all proposed transactions.

* After reviewing the plan you can incrementally approve purchase orders,
  distribution orders and manufacturing orders, and export .

  .. image:: _images/odoo-approve-export.png
   :alt: Exporting individual transactions


How to configure and customize it?
----------------------------------

We highly recommend to start by reading our blog post on ERP integrations. The document
gives a number of very useful best practices and practical hints for the development of
this type of integrations.

The connector is designed to be easily customizable to. There a few places to fill in with your
own code:

* A first step is to **activate the connector in the djangosettings.py configuration file**.
  Edit the file to include the app "ccAPPSdb.erpconnection" in the setting INSTALLED_APPS.

  ::

    # Installed applications.
    INSTALLED_APPS = (
      'django.contrib.auth',
      'django.contrib.contenttypes',
      'django.contrib.messages',
      'django.contrib.staticfiles',
      'ccAPPSdb.boot',
      # Add any project specific apps here
      #'ccAPPSdb.odoo',
      'ccAPPSdb.erpconnection', # <<<< New app <<<<
      'ccAPPSdb.input',
      'ccAPPSdb.output',
      'ccAPPSdb.execute',
      'ccAPPSdb.common',
      'rest_framework',
      'django.contrib.admin',
	  )

* Next, you'll need to **customize the SQL statements extracting the data from your ERP**
  source into a series of CSV-formatted flat files. The code is in the file
  ccAPPSdb/erpconnection/management/commands/erp2ccAPPS.py

  For each data object, you'll find a method following the pattern from this example.
  You'll need to customize the included SQL's to match your data source.

  ::

     def extractItem(self):
       # Print progress
       outfilename = os.path.join(self.destination, 'item.%s' % self.ext)
       print("Start extracting items to %s" % outfilename)
       # Retrieve the data from the ERP with an SQL query.
       # The cursor is open on a connection to your ERP database. All common
       # databases have adapters for Python.
	   self.cursor.execute('''
	     Your extraction SQL query goes here.
	     ''')
	   # Write the result to a CSV file
	   with open(outfilename, 'w', newline='') as outfile:
	     outcsv = csv.writer(outfile, quoting=csv.QUOTE_MINIMAL)
	     outcsv.writerow(['name', 'subcategory', 'description', 'category', 'lastmodified'])
	     outcsv.writerows(self.cursor.fetchall())

  Depending on the modelled ccAPPS functionalities additional fields may be
  required. The skeleton is based on a minimal set of ccAPPS fields required
  to build a basic model.

* A next step is to **customize the SQL statements to feed the planning results**
  **from ccAPPS back into your ERP system**. This code is in the file
  ccAPPSdb/erpconnection/management/commands/ccAPPS2erp.py

  For each data export, you'll find a method following the pattern from this example.
  You'll need to customize the included SQL's to match your data source.

  ::

	 def extractPurchaseOrders(self):
	   print("Start exporting purchase orders")
	   self.cursor_ccAPPS.execute('''
	     select
	       item_id, location_id, supplier_id, quantity, startdate, enddate
	     from operationplan
	     inner join item on item_id = item.name
	     where type = 'PO' and status = 'approved'
         order by supplier_id
	      ''')
	    output = [ i for i in self.cursor_ccAPPS.fetchall()]
	    self.cursor_erp.executemany('''
	      Your insert&update SQL query goes here.
	      ''', output)


* A final step is to **customize are the SQL statements to incrementally**
  **export individual approved transactions from ccAPPS to the ERP system**.
  This code is in the file ccAPPSdb/erpconnection/views.py

  ::

      @login_required
      @csrf_protect
      def Upload(request):
      try:
        data = json.loads(request.body.decode('utf-8'))
        # Your logic goes here to send the information to the ERP
        return HttpResponse("OK")
      except Exception as e:
        logger.error("Can't connect to the ERP: %s" % e)
        return HttpResponseServerError("Can't connect to the ERP")
