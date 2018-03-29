#include <iostream>
#include <iomanip>
#include <unistd.h>
#include <string>
#include <stdlib.h>
#include <pthread.h>
#include <getopt.h>

#include "Options.h"
#include "Manager.h"
#include "Driver.h"
#include "Node.h"
#include "Group.h"
#include "Notification.h"
#include "value_classes/ValueStore.h"
#include "value_classes/Value.h"
#include "value_classes/ValueBool.h"
#include "platform/Log.h"
#include "Defs.h"

#define     SWITCH_BINARY_ID    15
#define     LOG_DIR             "logs"

using namespace OpenZWave;

using std::cout;
using std::endl;
using std::string;

bool temp = false;

static int verbose_flag;
static uint32 g_homeId = 0;
static bool   g_initFailed = false;

static struct option  long_options[] =
{
    {"verbose"  , no_argument       , &verbose_flag , true  },
    {"port"     , required_argument , 0             , 'p'   },
    {"config"   , required_argument , 0             , 'c'   },
    {"help"     , no_argument       , 0             , 'h'   },
    {0          , 0                 , 0             , 0     },
};

typedef struct
{
    uint32			m_homeId;
    uint8			m_nodeId;
    bool			m_polled;
    list<ValueID>	m_values;
} NodeInfo;

static list<NodeInfo*> g_nodes;
static pthread_mutex_t g_criticalSection;
static pthread_cond_t  initCond  = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t initMutex = PTHREAD_MUTEX_INITIALIZER;

bool ToggleSwitchBinary(const int node_Id, bool status)
{
    cout << "Setting new status: " << status << " to node ID " << node_Id << endl;
    bool result {true};
    pthread_mutex_lock (&g_criticalSection);
    for (list<NodeInfo *>::iterator it = g_nodes.begin(); it != g_nodes.end(); ++it)
    {
        NodeInfo * nd = *it;
        cout << left << setw(20) << "Current NodeID:" << static_cast<int>(nd->m_nodeId) << endl;
        if (nd->m_nodeId == node_Id)
        {
            cout << "The list of values of the current NodeInfo(nodeId=" << static_cast<int>(nd->m_nodeId) << ") "
                << (nd->m_values.empty() ? "is empty" : "contains something!") << endl;
            for (list<ValueID>::iterator it2 = nd->m_values.begin(); it2 != nd->m_values.end(); ++it2)
            {
                ValueID v = *it2;
                cout << "v.GetCommandClassId() = 0x" << hex << static_cast<int>(v.GetCommandClassId()) << dec << endl;
                if (v.GetCommandClassId() == 0x25)
                {
                    cout << "Setting value ..." << endl;
                    Manager::Get()->SetValue(v, status);
                }
            }
        }
    }
    pthread_mutex_unlock (&g_criticalSection);
    return result;
}

void help(string program)
{
    int width { 30 };
    int internal_width { 18 };

    cout << "USAGE: " << program << " [args] status" << endl;
    cout << "[args]:" << endl;
    cout << left << setw(width - internal_width) << "--config" << setw(internal_width) << "<dir>"
        << "Choose where the config files are stored." << endl;
    cout << left << setw(width) << "--help" << "Show this help and exit." << right << endl;
    cout << left << setw(width - internal_width) << "--port" << setw(internal_width) << "<tty>"
        << "Choose USB serial port." << endl;
    cout << left << setw(width) << "--verbose" << "Output some verbose." << endl;
}

//-----------------------------------------------------------------------------
// <GetNodeInfo>
// Return the NodeInfo object associated with this notification
//-----------------------------------------------------------------------------
NodeInfo* GetNodeInfo ( Notification const* _notification )
{
    uint32 const homeId = _notification->GetHomeId();
    uint8 const nodeId = _notification->GetNodeId();
    for( list<NodeInfo*>::iterator it = g_nodes.begin(); it != g_nodes.end(); ++it )
    {
        NodeInfo* nodeInfo = *it;
        if( ( nodeInfo->m_homeId == homeId ) && ( nodeInfo->m_nodeId == nodeId ) )
        {
            return nodeInfo;
        }
    }

    return NULL;
}

//-----------------------------------------------------------------------------
// <OnNotification>
// Callback that is triggered when a value, group or node changes
//-----------------------------------------------------------------------------
void OnNotification ( Notification const* _notification, void* _context )
{
    // Must do this inside a critical section to avoid conflicts with the main thread
    pthread_mutex_lock( &g_criticalSection );

    switch( _notification->GetType() )
    {
        case Notification::Type_ValueAdded:
            {
                if( NodeInfo* nodeInfo = GetNodeInfo( _notification ) )
                {
                    // Add the new value to our list
                    nodeInfo->m_values.push_back( _notification->GetValueID() );
                }
                break;
            }

        case Notification::Type_ValueRemoved:
            {
                if( NodeInfo* nodeInfo = GetNodeInfo( _notification ) )
                {
                    // Remove the value from out list
                    for( list<ValueID>::iterator it = nodeInfo->m_values.begin(); it != nodeInfo->m_values.end(); ++it )
                    {
                        if( (*it) == _notification->GetValueID() )
                        {
                            nodeInfo->m_values.erase( it );
                            break;
                        }
                    }
                }
                break;
            }

        case Notification::Type_ValueChanged:
            {
                // One of the node values has changed
                if( NodeInfo* nodeInfo = GetNodeInfo( _notification ) )
                {
                    nodeInfo = nodeInfo;		// placeholder for real action
                }
                break;
            }

        case Notification::Type_Group:
            {
                // One of the node's association groups has changed
                if( NodeInfo* nodeInfo = GetNodeInfo( _notification ) )
                {
                    nodeInfo = nodeInfo;		// placeholder for real action
                }
                break;
            }

        case Notification::Type_NodeAdded:
            {
                // Add the new node to our list
                NodeInfo* nodeInfo = new NodeInfo();
                nodeInfo->m_homeId = _notification->GetHomeId();
                nodeInfo->m_nodeId = _notification->GetNodeId();
                nodeInfo->m_polled = false;		
                g_nodes.push_back( nodeInfo );
                if (temp == true) {
                    Manager::Get()->CancelControllerCommand( _notification->GetHomeId() );
                }
                break;
            }

        case Notification::Type_NodeRemoved:
            {
                // Remove the node from our list
                uint32 const homeId = _notification->GetHomeId();
                uint8 const nodeId = _notification->GetNodeId();
                for( list<NodeInfo*>::iterator it = g_nodes.begin(); it != g_nodes.end(); ++it )
                {
                    NodeInfo* nodeInfo = *it;
                    if( ( nodeInfo->m_homeId == homeId ) && ( nodeInfo->m_nodeId == nodeId ) )
                    {
                        g_nodes.erase( it );
                        delete nodeInfo;
                        break;
                    }
                }
                break;
            }

        case Notification::Type_NodeEvent:
            {
                // We have received an event from the node, caused by a
                // basic_set or hail message.
                if( NodeInfo* nodeInfo = GetNodeInfo( _notification ) )
                {
                    nodeInfo = nodeInfo;		// placeholder for real action
                }
                break;
            }

        case Notification::Type_PollingDisabled:
            {
                if( NodeInfo* nodeInfo = GetNodeInfo( _notification ) )
                {
                    nodeInfo->m_polled = false;
                }
                break;
            }

        case Notification::Type_PollingEnabled:
            {
                if( NodeInfo* nodeInfo = GetNodeInfo( _notification ) )
                {
                    nodeInfo->m_polled = true;
                }
                break;
            }

        case Notification::Type_DriverReady:
            {
                g_homeId = _notification->GetHomeId();
                break;
            }

        case Notification::Type_DriverFailed:
            {
                g_initFailed = true;
                pthread_cond_broadcast(&initCond);
                break;
            }

        case Notification::Type_AwakeNodesQueried:
        case Notification::Type_AllNodesQueried:
        case Notification::Type_AllNodesQueriedSomeDead:
            {
                pthread_cond_broadcast(&initCond);
                break;
            }

        case Notification::Type_DriverReset:
        case Notification::Type_Notification:
        case Notification::Type_NodeNaming:
        case Notification::Type_NodeProtocolInfo:
        case Notification::Type_NodeQueriesComplete:
        default:
            {
            }
    }

    pthread_mutex_unlock( &g_criticalSection );
}

int main( int argc, char* argv[] )
{
    int c, optindex;
    bool create_success {false}, new_status;
    pthread_mutexattr_t mutexattr;

    /* Default port */
    string port{ "/dev/ttyACM0" };
    string config{ "./config/" };

    /* Parse command line options */
    while ( (c = getopt_long(argc, argv, "c:hp:", long_options, &optindex)) != -1)
    {
        switch (c)
        {
            case 'h':
                help( string{argv[0]} );
                return 0;
            case 'p':
                port = string{optarg};
                break;
            case 'c':
                config = string{optarg};
                break;
            case '?':
                help( string{argv[0]} );
                return 1;
            default:
                abort ();
        }
    }

    /* Init logging */
    Log::Create( "", false, true, LogLevel_Debug, LogLevel_Debug, LogLevel_None);

    if (optind == argc)
    {
        Log::Write(LogLevel_Error,
                "Forgot to set the new status?");
        help ( string{argv[0]} );
        return 1;
    }
    else
    {
        new_status = (bool) atoi(argv[optind]);
    }

    pthread_mutexattr_init ( &mutexattr );
    pthread_mutexattr_settype( &mutexattr, PTHREAD_MUTEX_RECURSIVE );
    pthread_mutex_init( &g_criticalSection, &mutexattr );
    pthread_mutexattr_destroy( &mutexattr );

    pthread_mutex_lock( &initMutex );

    // Create the OpenZWave Manager.
    // The first argument is the path to the config files (where the manufacturer_specific.xml file is located
    // The second argument is the path for saved Z-Wave network state and the log file.  If you leave it NULL 
    // the log file will appear in the program's working directory.
    Options::Create(config, LOG_DIR, "", create_success);
    if (!create_success)
    {
        Log::Write(LogLevel_Error,
                "Unable to find the configuration files neither at '%s', '%s' or at config/, exiting...",
                config.c_str(),
                SYSCONFDIR);
        pthread_mutex_destroy( &g_criticalSection );
        return 1;
    }

    Options::Get()->AddOptionInt( "SaveLogLevel", LogLevel_Detail );
    Options::Get()->AddOptionInt( "QueueLogLevel", LogLevel_Debug );
    Options::Get()->AddOptionInt( "DumpTrigger", LogLevel_Error );
    Options::Get()->AddOptionInt( "PollInterval", 500 );
    Options::Get()->AddOptionBool( "IntervalBetweenPolls", true );
    Options::Get()->AddOptionBool("ValidateValueChanges", true);
    Options::Get()->Lock();

    Manager::Create();

    // Add a callback handler to the manager.  The second argument is a context that
    // is passed to the OnNotification method.  If the OnNotification is a method of
    // a class, the context would usually be a pointer to that class object, to
    // avoid the need for the notification handler to be a static.
    Manager::Get()->AddWatcher( OnNotification, NULL );

    // Add a Z-Wave Driver
    Manager::Get()->AddDriver( port );
    cout << "AddDriver() [done]" << endl;

    // Now we just wait for either the AwakeNodesQueried or AllNodesQueried notification,
    // then write out the config file.
    // In a normal app, we would be handling notifications and building a UI for the user.
    pthread_cond_wait( &initCond, &initMutex );

    // Since the configuration file contains command class information that is only 
    // known after the nodes on the network are queried, wait until all of the nodes 
    // on the network have been queried (at least the "listening" ones) before
    // writing the configuration file.  (Maybe write again after sleeping nodes have
    // been queried as well.)
    /*
    if( !g_initFailed )
    {
        cout << endl << endl << endl << "Here" << endl << endl << endl;

        // The section below demonstrates setting up polling for a variable.  In this simple
        // example, it has been hardwired to poll COMMAND_CLASS_BASIC on the each node that 
        // supports this setting.
        pthread_mutex_lock( &g_criticalSection );
        for( list<NodeInfo*>::iterator it = g_nodes.begin(); it != g_nodes.end(); ++it )
        {
            NodeInfo* nodeInfo = *it;

            // skip the controller (most likely node 1)
            if( nodeInfo->m_nodeId == 1) continue;

            cout << "NodeID: " << nodeInfo->m_nodeId << endl;
            cout << "\t NodeName: " << Manager::Get()->GetNodeName(nodeInfo->m_homeId,nodeInfo->m_nodeId) << endl;
            cout << "\t ManufacturerName: " << 
                Manager::Get()->GetNodeManufacturerName(nodeInfo->m_homeId,nodeInfo->m_nodeId) << endl;
            cout << "\t NodeProductName: " <<
                Manager::Get()->GetNodeProductName(nodeInfo->m_homeId,nodeInfo->m_nodeId) << endl;
            cout << "Values announced by the nodes without polling:" << endl;

            for( list<ValueID>::iterator it2 = nodeInfo->m_values.begin(); it2 != nodeInfo->m_values.end(); ++it2 )
            {
                ValueID v = *it2;
                cout << "\t ValueLabel: " << Manager::Get()->GetValueLabel(v) << endl;
                cout << "\t\t ValueType: " << v.GetType() << endl;
                cout << "\t\t ValueHelp: " << Manager::Get()->GetValueHelp(v) << endl;
                cout << "\t\t ValueUnits: " << Manager::Get()->GetValueUnits(v) << endl;
                cout << "\t\t ValueMin: " << Manager::Get()->GetValueMin(v) << endl;
                cout << "\t\t ValueMax: " << Manager::Get()->GetValueMax(v) << endl;

                if( v.GetCommandClassId() == COMMAND_CLASS_BASIC )
                {
                    //Manager::Get()->EnablePoll( v, 2 );		// enables polling with "intensity" of 2, though this is irrelevant with only one value polled
                    break;
                }
            }
        }
        pthread_mutex_unlock( &g_criticalSection );
    }
    */

    ToggleSwitchBinary(SWITCH_BINARY_ID, new_status);

    Manager::Get()->RemoveDriver( port );

    Manager::Get()->RemoveWatcher( OnNotification, NULL );
    Manager::Destroy();
    Options::Destroy();
    pthread_mutex_destroy( &g_criticalSection );

    return 0;
}
