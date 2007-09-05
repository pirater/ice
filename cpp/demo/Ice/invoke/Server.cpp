// **********************************************************************
//
// Copyright (c) 2003-2007 ZeroC, Inc. All rights reserved.
//
// This copy of Ice is licensed to you under the terms described in the
// ICE_LICENSE file included in this distribution.
//
// **********************************************************************

#include <Ice/Ice.h>
#include <PrinterI.h>

using namespace std;

class InvokeServer : public Ice::Application
{
public:

    virtual int run(int, char*[]);
};

int
main(int argc, char* argv[])
{
    InvokeServer app;
    return app.main(argc, argv, "config.server");
}

int
InvokeServer::run(int argc, char* argv[])
{
    if(argc > 1)
    {
        cerr << appName() << ": too many arguments" << endl;
        return EXIT_FAILURE;
    }

    Ice::ObjectAdapterPtr adapter = communicator()->createObjectAdapter("Printer");
    adapter->add(new PrinterI, communicator()->stringToIdentity("printer"));
    adapter->activate();
    communicator()->waitForShutdown();
    return EXIT_SUCCESS;
}
