// **********************************************************************
//
// Copyright (c) 2002
// MutableRealms, Inc.
// Huntsville, AL, USA
//
// All Rights Reserved
//
// **********************************************************************

#include <Ice/Ice.h>
#include <Ice/DynamicLibrary.h>
#include <IceBox/ServiceManagerI.h>

using namespace Ice;
using namespace IceInternal;
using namespace std;

typedef IceBox::Service* (*SERVICE_FACTORY)(CommunicatorPtr);

IceBox::ServiceManagerI::ServiceManagerI(CommunicatorPtr communicator, int& argc, char* argv[])
    : _communicator(communicator)
{
    _logger = _communicator->getLogger();

    if(argc > 0)
    {
        _progName = argv[0];
    }

    for(int i = 1; i < argc; i++)
    {
        _argv.push_back(argv[i]);
    }

    PropertiesPtr properties = _communicator->getProperties();
    _options = properties->getCommandLineOptions();
}

IceBox::ServiceManagerI::~ServiceManagerI()
{
}

void
IceBox::ServiceManagerI::shutdown(const Current& current)
{
    _communicator->shutdown();
}

int
IceBox::ServiceManagerI::run()
{
    try
    {
        ServiceManagerPtr obj = this;

        //
        // Create an object adapter. Services probably should NOT share
        // this object adapter, as the endpoint(s) for this object adapter
        // will most likely need to be firewalled for security reasons.
        //
        ObjectAdapterPtr adapter = _communicator->createObjectAdapterFromProperty("ServiceManagerAdapter",
                                                                                  "IceBox.ServiceManager.Endpoints");
        adapter->add(obj, stringToIdentity("ServiceManager"));

        //
        // Load and initialize the services defined in the property set
        // with the prefix "IceBox.Service.". These properties should
        // have the following format:
        //
        // IceBox.Service.Foo=entry_point [args]
        //
        const string prefix = "IceBox.Service.";
        PropertiesPtr properties = _communicator->getProperties();
        PropertyDict services = properties->getPropertiesForPrefix(prefix);
	PropertyDict::const_iterator p;
	for(p = services.begin(); p != services.end(); ++p)
	{
	    string name = p->first.substr(prefix.size());
	    const string& value = p->second;

            //
            // Separate the entry point from the arguments.
            //
            string entryPoint;
            StringSeq args;
            string::size_type pos = value.find_first_of(" \t\n");
            if(pos == string::npos)
            {
                entryPoint = value;
            }
            else
            {
                entryPoint = value.substr(0, pos);
                string::size_type beg = value.find_first_not_of(" \t\n", pos);
                while(beg != string::npos)
                {
                    string::size_type end = value.find_first_of(" \t\n", beg);
                    if(end == string::npos)
                    {
                        args.push_back(value.substr(beg));
                        beg = end;
                    }
                    else
                    {
                        args.push_back(value.substr(beg, end - beg));
                        beg = value.find_first_not_of(" \t\n", end);
                    }
                }
            }

            init(name, entryPoint, args);
        }

        //
        // Invoke start() on the services.
        //
        map<string,ServiceInfo>::const_iterator r;
        for(r = _services.begin(); r != _services.end(); ++r)
        {
            try
            {
                r->second.service->start();
            }
            catch(const FailureException&)
            {
                throw;
            }
            catch(const Exception& ex)
            {
                FailureException e;
                e.reason = "ServiceManager: exception in start for service " + r->first + ": " + ex.ice_name();
                throw e;
            }
        }

        //
        // We may want to notify external scripts that the services
        // have started. This is done by defining the property:
        //
        // IceBox.PrintServicesReady=bundleName
        //
        // Where bundleName is whatever you choose to call this set of
        // services. It will be echoed back as "bundleName ready".
        //
        // This must be done after start() has been invoked on the
        // services.
        //
        string bundleName = properties->getProperty("IceBox.PrintServicesReady");
        if(!bundleName.empty())
        {
            cout << bundleName << " ready" << endl;
        }

        //
        // Start request dispatching after we've started the services.
        //
        adapter->activate();

        _communicator->waitForShutdown();

        //
        // Invoke stop() on the services.
        //
        stopAll();
    }
    catch(const FailureException& ex)
    {
        Error out(_logger);
        out << ex.reason;
        stopAll();
        return EXIT_FAILURE;
    }
    catch(const Exception& ex)
    {
        Error out(_logger);
        out << "ServiceManager: " << ex;
        stopAll();
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

IceBox::ServicePtr
IceBox::ServiceManagerI::init(const string& service, const string& entryPoint, const StringSeq& args)
{
    //
    // We need to create a property set to pass to init().
    // The property set is populated from a number of sources.
    // The precedence order (from lowest to highest) is:
    //
    // 1. Properties defined in the server property set (e.g.,
    //    that were defined in the server's configuration file)
    // 2. Service arguments
    // 3. Server arguments
    //
    // We'll compose an array of arguments in the above order.
    //
    StringSeq serviceArgs;
    StringSeq::size_type j;
    for(j = 0; j < _options.size(); j++)
    {
        if(_options[j].find("--" + service + ".") == 0)
        {
            serviceArgs.push_back(_options[j]);
        }
    }
    for(j = 0; j < args.size(); j++)
    {
        serviceArgs.push_back(args[j]);
    }
    for(j = 0; j < _argv.size(); j++)
    {
        if(_argv[j].find("--" + service + ".") == 0)
        {
            serviceArgs.push_back(_argv[j]);
        }
    }

    //
    // Create the service property set.
    //
    PropertiesPtr serviceProperties = createProperties(serviceArgs);
    serviceArgs = serviceProperties->parseCommandLineOptions("Ice", serviceArgs);
    serviceArgs = serviceProperties->parseCommandLineOptions(service, serviceArgs);

    //
    // Load the entry point.
    //
    DynamicLibraryPtr library = new DynamicLibrary();
    DynamicLibrary::symbol_type sym = library->loadEntryPoint(entryPoint);
    if(sym == 0)
    {
        string msg = library->getErrorMessage();
        FailureException ex;
        ex.reason = "ServiceManager: unable to load entry point `" + entryPoint + "'";
        if(!msg.empty())
        {
            ex.reason += ": " + msg;
        }
        throw ex;
    }

    //
    // Invoke the factory function.
    //
    SERVICE_FACTORY factory = (SERVICE_FACTORY)sym;
    ServiceInfo info;
    try
    {
        info.service = factory(_communicator);
    }
    catch(const Exception& ex)
    {
        FailureException e;
        e.reason = "ServiceManager: exception in entry point `" + entryPoint + "': " + ex.ice_name();
        throw e;
    }
    catch (...)
    {
        FailureException e;
        e.reason = "ServiceManager: unknown exception in entry point `" + entryPoint + "'";
        throw e;
    }

    //
    // Invoke Service::init().
    //
    try
    {
        info.service->init(service, _communicator, serviceProperties, serviceArgs);
        info.library = library;
        _services[service] = info;
    }
    catch(const FailureException&)
    {
        throw;
    }
    catch(const Exception& ex)
    {
        FailureException e;
        e.reason = "ServiceManager: exception while initializing service " + service + ": " + ex.ice_name();
        throw e;
    }

    return info.service;
}

void
IceBox::ServiceManagerI::stop(const string& service)
{
    map<string,ServiceInfo>::iterator r = _services.find(service);
    assert(r != _services.end());
    ServiceInfo info = r->second;
    _services.erase(r);

    try
    {
        info.service->stop();
    }
    catch(const Exception& ex)
    {
        //
        // Release the service before the library
        //
        info.service = 0;
        info.library = 0;

        FailureException e;
        e.reason = "ServiceManager: exception in stop for service " + service + ": " + ex.ice_name();
        throw e;
    }

    //
    // Release the service before the library
    //
    info.service = 0;
    info.library = 0;
}

void
IceBox::ServiceManagerI::stopAll()
{
    map<string,ServiceInfo>::const_iterator r = _services.begin();
    while(r != _services.end())
    {
        try
        {
            stop((*r++).first);
        }
        catch(const FailureException& ex)
        {
            Error out(_logger);
            out << ex.reason;
        }
    }
    assert(_services.empty());
}
