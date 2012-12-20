#include <stdio.h>
#include <stdlib.h>
#include <wiringPi.h>
#include <wiringSerial.h>
#include <string>
#include <map>
#include <fstream>
#include <iostream>

static char hexRfid[8];
static unsigned long decimalRfid = 0;
static int lastRfidRead = 0;
static int flowCount0 = 0;
static int flowLastTick0 = 0;

#define RFID_KEY 0
#define FLOW_COUNT0_KEY 1
#define FLOW_COUNT1_KEY 2

#define FLOW_SENSOR0_GPIO_4 4
#define FLOW_SENSOR1_GPIO_17 17
#define PIEZO_GPIO_11 11

#define MIN_TICKS_FOR_POUR 20

#define URL "172.16.1.125:80"
#define SENSOR0_NAME "kegboard.flow0"
#define API_KEY "100000093b2b6a727dc6972fbf26f2448dc60c0c"

using namespace std;

static string getUserName( unsigned long rfid );

PI_THREAD( rfid )
{
    (void)piHiPri( 90 );
    int handle = serialOpen( "/dev/ttyAMA0", 9600 );
    
    while( 1 )
    {
        if( serialDataAvail( handle ) > 0 )
        {
            if( serialGetchar( handle ) == 2 )
            {
                serialGetchar( handle );
                serialGetchar( handle );
                piLock( RFID_KEY );
                    for( int i = 0; i < 8; i++ )
                    {
                        hexRfid[i] = serialGetchar( handle );
                    }
                piUnlock( RFID_KEY );
                serialFlush( handle );
		
		if( decimalRfid != strtoul( hexRfid, 0, 16 ) || lastRfidRead < ( millis() - 20000 ) )
		{
                    digitalWrite(11,1);
                    delay(500);
                    digitalWrite (11,0);

                    decimalRfid = strtoul( hexRfid, 0, 16 );
                    lastRfidRead = millis();
                    printf("RFID #: %d Username: %s\n", decimalRfid, getUserName( decimalRfid ).c_str() );
                }   
            }
        }
        delay( 1 );
    }
}

PI_THREAD( flowSensor0 )
{
    (void)piHiPri( 91 );

    while( 1 )
    {
        if( waitForInterrupt( FLOW_SENSOR0_GPIO_4, -1 ) > 0 )
        {
            piLock( FLOW_COUNT0_KEY );
                flowCount0 += 1;
                flowLastTick0 = millis();
            piUnlock( FLOW_COUNT0_KEY );
        }            
    }
}

string getUserName( unsigned long rfid )
{
    map<int,string> rfidList;
    string line;
    ifstream myfile ("rfid.txt");
    if (myfile.is_open())
    {
        while ( myfile.good() )
        {
            getline (myfile,line);
            size_t index = line.find_first_of( "," );
            string userName = line.substr( 0, index );
            string rfid = line.substr( index+1, line.length()-index-1 );
	    //printf( "rfid.txt userName=%s rfid=%s\n", userName.c_str(), rfid.c_str() );
            rfidList[atoi(rfid.c_str())] = userName;
        }
        myfile.close();
    }
    printf( "USER=%s ID=%d\n", rfidList[rfid].c_str(), rfid );
    return rfidList[rfid];
}

void setup( )
{
    system ("gpio edge 4 falling") ;
    system ("gpio edge 17 falling") ;
    system ("gpio export 11 out") ;

    wiringPiSetupSys () ;

    piThreadCreate(rfid);
    piThreadCreate(flowSensor0);
}

int main ( void )
{
    setup();
    int lastTickReported = 0;
    while( 1 )
    {
        //printf("Running");
        if( flowCount0 != 0 )
        {
            if( lastTickReported != flowCount0 )
	    {
                printf("FlowCount0 = %d\n", flowCount0);
                lastTickReported = flowCount0;
            }
            if( flowLastTick0 < ( millis() - 1000 ) )
            {
		if( MIN_TICKS_FOR_POUR < flowCount0 )
                {
                    int flowToReport = flowCount0;
                    piLock( FLOW_COUNT0_KEY );
                        flowCount0 = 0;
                    piUnlock( FLOW_COUNT0_KEY );

                    printf("FlowCount0 Final = %d\n", flowToReport);
                    char output[200];
                    char userName[50] = "";
                    if( lastRfidRead != 0 &&  ( lastRfidRead > ( millis() - 20000 ) ) )
                    {
                        if( !getUserName( decimalRfid ).empty() )
                        {
                            sprintf( userName, " -F username=%s", getUserName( decimalRfid ).c_str() );
                        }
                    }
                    sprintf(output, "curl http://%s/api/taps/%s/?api_key=%s -F ticks=%d%s", URL, SENSOR0_NAME, API_KEY, flowToReport, userName );
                    printf( "%s", output );
		    system( output );
                    piLock(RFID_KEY);
                        decimalRfid = 0;
                    piUnlock(RFID_KEY);                
                }
		else
		{
		    piLock( FLOW_COUNT0_KEY );
			flowCount0 = 0;
		    piUnlock( FLOW_COUNT0_KEY );
                }
            }
        }
        delay( 1 );
    }
    return 0;
}
