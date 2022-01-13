.. _lwm2m_msc:

Message sequence charts
#######################

The following message sequence chart shows the initialization of the LwM2M carrier library:


.. msc::
    hscale = "1.1";
    "LTE link controller", Application, "LwM2M carrier Library";
    Application=>"LwM2M carrier Library"             [label="lwm2m_carrier_init()"];
    Application<<="LwM2M carrier Library"            [label="LWM2M_CARRIER_EVENT_CARRIER_INIT"];
    Application rbox Application                     [label="Application can now attach to network"];
    "LTE link controller"<<= "Application"           [label="lte_lc_init_and_connect()"];
    Application=>"LwM2M carrier Library"             [label="lwm2m_carrier_run()"];
    |||;
    "LwM2M carrier Library" :> "LwM2M carrier Library" [label="(no return)"];
    ...;



The following message sequence chart shows the ``lwm2m_carrier_thread_run`` thread up until we register with the device management server:

.. msc::
    hscale = "1.1";
    "LTE link controller", Application, "LwM2M carrier Library";
    Application=>"LwM2M carrier Library"                 [label="lwm2m_carrier_run()"];
    ...;
    |||;
    ---                                                  [label="Device is not bootstrapped yet"];
    |||;
    ...;
    Application<<="LwM2M carrier Library"                [label="LWM2M_CARRIER_EVENT_LTE_LINK_DOWN"];
    "LTE link controller"<<= "Application"               [label="lte_lc_offline()"];
    "LwM2M carrier Library" rbox "LwM2M carrier Library" [label="Write Bootstrap-Server keys to modem"];
    |||;
    Application<<="LwM2M carrier Library"                [label="LWM2M_CARRIER_EVENT_LTE_LINK_UP"];
    "LTE link controller"<<= "Application"               [label="lte_lc_connect()"];
    ...;
    Application<<="LwM2M carrier Library"                [label="LWM2M_CARRIER_EVENT_LTE_LINK_DOWN"];
    "LTE link controller"<<= "Application"               [label="lte_lc_offline()"];
    "LwM2M carrier Library" rbox "LwM2M carrier Library" [label="Write management server keys to modem"];
    Application<<="LwM2M carrier Library"                [label="LWM2M_CARRIER_EVENT_BOOTSTRAPPED"];
    Application<<="LwM2M carrier Library"                [label="LWM2M_CARRIER_EVENT_LTE_LINK_UP"];
    "LTE link controller"<<= "Application"               [label="lte_lc_connect()"];
    ---                                                  [label="Device is bootstrapped"];
    Application<<="LwM2M carrier Library"                [label="LWM2M_CARRIER_EVENT_LTE_READY"];
    Application rbox Application                         [label="Application can now use the LTE link without further interruptions from the LwM2M carrier library"];
    "LwM2M carrier Library" rbox "LwM2M carrier Library" [label="Connecting to the management server(s)"];
    ...;
    Application<<="LwM2M carrier Library"                [label="LWM2M_CARRIER_EVENT_REGISTERED"];
    ...;


The following message sequence chart shows FOTA updates:

.. msc::
    hscale = "1.1";
    "LTE link controller", Application, "LwM2M carrier Library";
    |||;
    ---                                        [label="Carrier initiates modem update"];
    |||;
    Application<<="LwM2M carrier Library"      [label="LWM2M_CARRIER_EVENT_FOTA_START"];
    ...;
    Application<<="LwM2M carrier Library"      [label="LWM2M_CARRIER_EVENT_LTE_LINK_DOWN"];
    "LTE link controller"<<= "Application"     [label="lte_lc_offline()"];
    Application<<="LwM2M carrier Library"      [label="LWM2M_CARRIER_EVENT_LTE_LINK_UP"];
    "LTE link controller"<<= "Application"     [label="lte_lc_connect()"];
    Application<<="LwM2M carrier Library"      [label="LWM2M_CARRIER_EVENT_LTE_READY"];
    Application<<="LwM2M carrier Library"      [label="LWM2M_CARRIER_EVENT_REGISTERED"];
    ...;


The following message sequence chart shows a successful CA certificate initialization:

.. msc::
    hscale = "1.1";
    Modem,Application,"LwM2M carrier Library";
    |||;
    ---                                    [label="LwM2M carrier library is initializing"];
    |||;
    Application<<="LwM2M carrier Library"  [label="LWM2M_CARRIER_EVENT_CERTS_INIT"];
    ...;
    Application rbox Application           [label="Write CA certificates to modem security tags"];
    Modem<<=Application                    [label="modem_key_mgmt_write(...)"];
    ...;
    Modem->Application                     [label="Success"];
    Application rbox Application           [label="Provide LwM2M carrier library the security tags for CA certificates"];
    Application rbox Application           [label="LwM2M carrier event data set to ca_cert_tags_t"];
    Application->"LwM2M carrier Library"   [label="Success"];
    ...;

The following message sequence chart shows that the CA certificate initialization fails if the application fails to provision the keys to the modem:

.. msc::
    hscale = "1.1";
    Modem,Application,"LwM2M carrier Library";
    |||;
    ---                                         [label="LwM2M carrier library is initializing"];
    |||;
    Application<<="LwM2M carrier Library"       [label="LWM2M_CARRIER_EVENT_CERTS_INIT"];
    ...;
    Application rbox Application                [label="Write CA certificates to modem security tags"];
    Modem<<=Application                         [label="modem_key_mgmt_write(...)"];
    ...;
    Modem->Application                          [label="Failure"];
    Application->"LwM2M carrier Library"        [label="Failure"];
    "LwM2M carrier Library" rbox "LwM2M carrier Library" [label="LwM2M carrier library fails to initialize"];
    Application<-"LwM2M carrier Library"        [label="Failure"];
    ...;

The following message sequence chart shows that FOTA fails at run time if an invalid CA certificate is provided during the initialization:

.. msc::
    hscale = "1.1";
    Application,"LwM2M carrier Library";
    |||;
    ---                                        [label="Carrier initiates modem update"];
    |||;
    Application<<="LwM2M carrier Library"      [label="LWM2M_CARRIER_EVENT_FOTA_START"];
    ...;
    "LwM2M carrier Library" rbox "LwM2M carrier Library" [label="Apply security tag that contains invalid certificate"];
    |||;
    Application<<="LwM2M carrier Library"      [label="LWM2M_CARRIER_ERROR_FOTA_CONN (NRF_ECONNREFUSED)"];
    ...;
