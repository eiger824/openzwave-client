#include <iostream>
#include <iomanip>
#include <unistd.h>
#include <string>
#include <stdlib.h>
#include <thread>
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
#include "command_classes/SwitchBinary.h"
#include "Defs.h"

#define     SWITCH_BINARY_ID    21 
#define     LOG_DIR             "logs"

using namespace OpenZWave;

using std::cout;
using std::endl;
using std::string;

bool temp = false;

static uint32 g_homeId = 0;
static bool   g_initFailed = false;

static struct option  long_options[] =
{
    {"verbose"      , no_argument       , 0             , 'v'   },
    {"interactive"  , no_argument       , 0             , 'i'   },
    {"port"         , required_argument , 0             , 'p'   },
    {"config"       , required_argument , 0             , 'c'   },
    {"help"         , no_argument       , 0             , 'h'   },
    {0              , 0                 , 0             , 0     },
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
    bool result {true};
    pthread_mutex_lock (&g_criticalSection);
    for (list<NodeInfo *>::iterator it = g_nodes.begin(); it != g_nodes.end(); ++it)
    {
        NodeInfo * nd = *it;
        if (nd->m_nodeId == node_Id)
        {
            for (list<ValueID>::iterator it2 = nd->m_values.begin(); it2 != nd->m_values.end(); ++it2)
            {
                ValueID v = *it2;
                if (v.GetCommandClassId() == SwitchBinary::StaticGetCommandClassId())
                {
                    result = Manager::Get()->SetValue(v, status);
                    string val{};
                    Manager::Get()->GetValueAsString(v, &val);
                    cout << "New status is:\t" << val << endl;
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
    cout << left << setw(width) << "--interactive" << "Run toggling interactively." << endl;
    cout << left << setw(width - internal_width) << "--port" << setw(internal_width) << "<tty>"
        << "Choose USB serial port." << endl;
    cout << left << setw(width) << "--silent" << "Don't output anything to console." << endl;
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
    bool verbose = false, interactive = false;

    /* Default port */
    string port{ "/dev/ttyACM0" };
    string config{ "./config/" };

    /* Parse command line options */
    while ( (c = getopt_long(argc, argv, "c:hip:v", long_options, &optindex)) != -1)
    {
        switch (c)
        {
            case 'c':
                config = string{optarg};
                break;
            case 'h':
                help( string{argv[0]} );
                return 0;
            case 'i':
                interactive = true;
                break;
            case 'p':
                port = string{optarg};
                break;
            case 'v':
                verbose = true;
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
    Options::Get()->AddOptionBool("ConsoleOutput", verbose );
    Options::Get()->Lock();

    Manager::Create();

    // Add a callback handler to the manager.  The second argument is a context that
    // is passed to the OnNotification method.  If the OnNotification is a method of
    // a class, the context would usually be a pointer to that class object, to
    // avoid the need for the notification handler to be a static.
    Manager::Get()->AddWatcher( OnNotification, NULL );

    // Add a Z-Wave Driver
    Manager::Get()->AddDriver( port );

    // Now we just wait for either the AwakeNodesQueried or AllNodesQueried notification,
    // then write out the config file.
    // In a normal app, we would be handling notifications and building a UI for the user.
    pthread_cond_wait( &initCond, &initMutex );


    string opt{};
    for (;;)
    {
        if (interactive)
        {
            cout << "Switch on/off?: ";
            cin >> opt;
            if (opt != "on" && opt != "off")
            {
                cerr << "on / off valid" << endl;
                break;
            }
            ToggleSwitchBinary(SWITCH_BINARY_ID, (opt == "on" ? true : false));
        }
        else
        {
            ToggleSwitchBinary(SWITCH_BINARY_ID, new_status);
            new_status = !new_status;
            sleep(3);
        }

    }

    Manager::Get()->RemoveDriver( port );
    Manager::Get()->RemoveWatcher( OnNotification, NULL );
    Manager::Destroy();

    Options::Destroy();
    pthread_mutex_destroy( &g_criticalSection );

    return 0;
}
