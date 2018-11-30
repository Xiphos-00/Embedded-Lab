#include "gpiolib_addr.h"
#include "gpiolib_reg.h"
#include "gpiolib_addr.h"
#include "gpiolib_pinmd.h"
#include "gpiolib_pinfuncs.h"

#include <stdlib.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>		//for the printf() function
#include <fcntl.h>
#include <linux/watchdog.h> 	//needed for the watchdog specific constants
#include <unistd.h> 		//needed for sleep
#include <sys/ioctl.h> 		//needed for the ioctl function
#include <stdlib.h> 		//for atoi
#include <time.h> 		//for time_t and the time() function
#include <sys/time.h>           //for gettimeofday()
#include <stdbool.h>

//Macros
#define LASER1 4
#define LASER2 6
#define LED 17
#define DEFAULTTIME 30
#define SLEEPLENGTH 10000
#define WATCHDOGTIMEOUT_MAX 15
#define WATCHDOGTIMEOUT_MIN 1
#define WATCHDOGTIMEOUT_DEFAULT 10
#define PRINT_MSG(file, cTime, programName, pid, str) \
    do{ \
        fprintf(file, "==%i== | %s |: %s (%s)\n", pid, programName, str, cTime);\
        fflush(file);\
    }while(0)
#define LOGFILE_DEFAULT "/home/pi/LaserLab.log"
#define CONFIGFILE "/home/pi/LaserLab.cfg"
#define STATS_THRESHOLD 5
#define STATUSUPDATE 15

//Function declarations
int main(const int argc, const char* argv[]);
int laserDiodeStatus(GPIO_Handle gpio, int diodeNumber);
GPIO_Handle initializeGPIO();
void readConfig(FILE* configFile, int* timeout, char* logFileName, char* StatsFile);
void getTime(char* buffer);
bool isAlphaNumeric(const char c);
bool isWhiteSpace(const char c);
bool isNumber(const char c);
bool isValidValueCharacter(const char c);
bool tokenIsNumber(const char* c, int length);
char* constructToken(const char* buffer, int start, int length);
void copyParam(char* param, char* to, int paramlength);
bool stringEqual(const char* a, const char* parameter, const int paramLength);
bool outputStats(const char* statsFile, const int l1Broken, const int l2Broken, const int numIn, const int numOut, const char* cTime);

//Enums
enum LaserState {BROKEN, UNBROKEN};
typedef enum State {WAITING, START, L1BROKEN, L2BROKEN, PARTIALIN, PARTIALOUT, GOINGIN, GOINGOUT} State;
typedef enum CfgFileState {CFGSTART, WHITESPACE, GOT_ALHPANUMERIC,GOT_EQUALSIGN, GOT_PARAMETER_NAME, PARAMETER_DONE, GOT_VALUE, CFGDONE, CFGEXIT} CfgFileState;

int main(const int argc, const char* argv[]){
    GPIO_Handle gpio = initializeGPIO();

    //Get the program
    int PID = getpid();
    char* programName = "Laser Lab";
    char cTime[30];

    ////////////////////////////////////////////////////
    //////           Read Config File             //////
    
    FILE* configFile;
    configFile = fopen(CONFIGFILE, "r");    

    if(!configFile){
        fprintf(stderr, "ERROR: The config file could not be opened\n");
        return -1;
    }

    //Variables that will be set by config file
    int timeout;
    char logFileName[50];
    char statsFileName[50];
    int funcTime;

    fprintf(stderr, "Going to read config\n");
    readConfig(configFile, &timeout, logFileName, statsFileName);
    
    fclose(configFile);
    fprintf(stderr, "Ended config\n");

    //////          Config File Done              //////
    ////////////////////////////////////////////////////

    ////////////////////////////////////////////////////
    //////            Open Log File              ///////

    fprintf(stderr, "Logfile was set to %s\n", logFileName);
    FILE* logFile = fopen(logFileName, "a");
    if(!logFile){
        fprintf(stderr, "The log file could not be opened!\n");
        return -1;
    }

    //////           Log File Done              ///////
    ///////////////////////////////////////////////////

    ////////////////////////////////////////////////////
    //////            Set Up Watchdog            ///////

    int watchdog;
    getTime(cTime);
    if((watchdog = open("/dev/watchdog", O_RDWR | O_NOCTTY)) < 0){
        PRINT_MSG(logFile, cTime, programName, PID, "ERROR: Could not open Watchdog device!");
        fclose(logFile);
        return -1;
    }
    PRINT_MSG(logFile, cTime, programName, PID, "Debug: The Watchdog file was successfully opened");

    ioctl(watchdog, WDIOC_SETTIMEOUT, &timeout);

    fprintf(logFile, "==%i== | %s | Debug: | Watchdog timer set: %i \n", PID, programName, timeout, cTime);
    fflush(logFile);

    ioctl(watchdog, WDIOC_GETTIMEOUT, &timeout);

    fprintf(stderr, "The watchdog timeout is %d seconds.\n\n", timeout);

    //////            Watchdog Done              ///////
    ////////////////////////////////////////////////////

    ///////////////////////////////////////////////////
    ///////////        Function Set-Up        /////////
    int numIn=0, numOut=0;
    State funcState = START;

    int l1State = 0,l2State = 0;
    int l1Broken = 0, l2Broken = 0;

    /////////////////////////////////////////////////////
    ///////////     Status Update Vars       ////////////
    int statsTime = time(0);
    int watchdogPingTime = time(0);
    int statusUpdateTime = time(0);
    int statisticsUpdates = 0;
    int watchdogPings = 0;


    while(1){
        //Get the states at the beginning of each loop
        l1State = laserDiodeStatus(gpio, 1); 
        //fprintf(stderr, "Diode Status: %i %i\n", l1State,l2State);
        l2State = laserDiodeStatus(gpio, 2);

        usleep(SLEEPLENGTH);

        //Update the stats occasionally
        if(difftime(time(0), statsTime) > STATS_THRESHOLD){
            statsTime = time(0);
            bool statsOutput = outputStats(statsFileName ,l1Broken, l2Broken, numIn, numOut, cTime);
        
            getTime(cTime);
            if(statsOutput){
                //Output what we did to log
                statisticsUpdates++;
                //PRINT_MSG(logFile, cTime, programName, PID, "Debug: Statistics file was updated");
            }
            else{
                //Something went wrong with the stats file
                PRINT_MSG(logFile, cTime, programName, PID, "ERROR: Tried to write to statistics file, but failed.");
            }
        }

        //Ping the watchdog twice as often as the watchdog would kill us
        if(difftime(time(0), watchdogPingTime) > timeout/2){        
            ioctl(watchdog, WDIOC_KEEPALIVE, 0);
            watchdogPingTime = time(0);
            watchdogPings++;

            //Output what we did to log
            getTime(cTime);
            //PRINT_MSG(logFile, cTime, programName, PID, "Debug: Watchdog was kicked.");
        }

        //Give a status update to log once in a while
        if(difftime(time(0), statusUpdateTime) > STATUSUPDATE){
            //Reset time
            statusUpdateTime = time(0);
            
            //Write to logfile
            getTime(cTime);
            fprintf(logFile, "==%i== | %s |: Debug: | Watchdog pings: %i | Stats updates: %i | since last status update (%s)\n", PID, programName,watchdogPings, statisticsUpdates, cTime);\
            fflush(logFile);

            //Reset pings
            statisticsUpdates = 0;
            watchdogPings = 0;
        }

        //State machine
        switch(funcState){
            case START:
                //No lasers are broken, what happens next?
                usleep(SLEEPLENGTH);

                //Laser 1 breaks
                if(l1State == BROKEN && l2State == UNBROKEN){   
                    l1Broken++;
                    funcState = L1BROKEN;
                    PRINT_MSG(logFile, cTime, programName, PID, "Debug: First laser was broken; entered L1BROKEN state");
                    break;
                }

                //Laser 2 breaks
                if(l1State == UNBROKEN && l2State == BROKEN){
                    l2Broken++;
                    funcState = L2BROKEN;
                    PRINT_MSG(logFile, cTime, programName, PID, "Debug: Second laser was broken; entered L2BROKEN state");
                    break;
                }

                break;
            case L1BROKEN:
                //Laser 1 is broken, what happens next?
                usleep(SLEEPLENGTH);

                //Laser 1 unbreaks
                if(l1State == UNBROKEN && l2State == UNBROKEN){
                    funcState = START;
                    PRINT_MSG(logFile, cTime, programName, PID, "Debug: First laser was unbroken; went back to START");
                    break;
                }

                //Laser 2 breaks
                else if(l1State == BROKEN && l2State == BROKEN){
                    l2Broken++;
                    funcState = PARTIALIN;
                    PRINT_MSG(logFile, cTime, programName, PID, "Debug: Second laser broke; went to PARTIALIN");
                    break;
                }
                break;
            case L2BROKEN:
                //Laser 2 is broken, what happens next?
                usleep(SLEEPLENGTH);

                //Laser 2 unbreaks
                if(l2State == UNBROKEN && l1State == UNBROKEN){
                    funcState = START;
                    PRINT_MSG(logFile, cTime, programName, PID, "Debug: Laser 2 unbroken; went back to START");
                    break;
                }

                //Laser 1 breaks
                else if(l2State == BROKEN && l1State == BROKEN){
                    l1Broken++;
                    funcState = PARTIALOUT;
                    PRINT_MSG(logFile, cTime, programName, PID, "Debug: Laser 1 broke; went to PARTIALOUT");
                    break;
                }
                break;
            case PARTIALIN:
                //Both are broken, we are possibly on the way in. What happens next?
                usleep(SLEEPLENGTH);

                //Laser 1 is unbroken, we are now further in
                if(l1State== UNBROKEN && l2State == BROKEN){
                    funcState = GOINGIN;
                    PRINT_MSG(logFile, cTime, programName, PID, "Debug: Laser 1 unbroke; went to GOINGIN");
                    break;
                }

                //Laser 2 is unbroken, we are going back out
                else if(l1State == BROKEN && l2State == UNBROKEN){
                    funcState = L1BROKEN;
                    PRINT_MSG(logFile, cTime, programName, PID, "Debug: Laser 2 unbroke; went to L1BROKEN");
                    break;
                }
                break;
            case PARTIALOUT:
                //Both are broken, we are possibly on the way out. What happens next?
                usleep(SLEEPLENGTH);

                //Laser 2 is unbroken, we are now further out
                if(l2State== UNBROKEN && l1State == BROKEN){
                    funcState = GOINGOUT;
                    PRINT_MSG(logFile, cTime, programName, PID, "Debug: Laser 2 unbroke; went to GOINGOUT");
                    break;
                }

                //Laser 1 is unbroken, we are going back in
                else if(l2State == BROKEN && l1State == UNBROKEN){
                    funcState = L2BROKEN;
                    PRINT_MSG(logFile, cTime, programName, PID, "Debug: Laser 1 unbroken; went back to L2BROKEN");
                    break;
                }
                break;
            case GOINGIN:
                //We are one laser break away from going fully in. What happens next?
                usleep(SLEEPLENGTH);

                //Laser 2 also unbreaks, we have successfully gone in! go back to start state
                if(l1State == UNBROKEN && l2State == UNBROKEN){
                    funcState = START;
                    numIn++;
                    PRINT_MSG(logFile, cTime, programName, PID, "Debug: Succesfully entered; went back to START");
                    break;
                }

                //Laser 2 is still broken and laser 1 breaks, we are going back out
                if(l1State == BROKEN && l2State == BROKEN){
                    l1Broken++;
                    funcState = PARTIALIN;
                    PRINT_MSG(logFile, cTime, programName, PID, "Debug: Laser 1 broke; went back to PARTIALIN");
                    break;
                }
                break;
            case GOINGOUT:
                //We are one laser break away from fully going out. What happens next?
                usleep(SLEEPLENGTH);

                //Laser 1 also unbreaks, we have successfuly left! go back to start state
                if(l1State == UNBROKEN && l2State == UNBROKEN){
                    funcState = START;
                    numOut++;
                    PRINT_MSG(logFile, cTime, programName, PID, "Debug: Successfully exited; went back to START");
                    break;
                }

                //Laser 1 is still broken and laser 2 breaks, we are going back in
                if(l1State == BROKEN && l2State == BROKEN){
                    l2Broken++;
                    funcState = PARTIALOUT;
                    PRINT_MSG(logFile, cTime, programName, PID, "Debug: Laser 2 broke; went back to PARTIALOUT");
                    break;
                }
                break;
            default:
                PRINT_MSG(logFile, cTime, programName, PID, "ERROR: Invalid state in main loop");
                break;
        }
    }

    //Close the watchdog
    write(watchdog, "V", 1);
    getTime(cTime);
    PRINT_MSG(logFile, cTime, programName, PID, "Debug: The Watchdog was closed");

    //Close the log file
    PRINT_MSG(logFile, cTime, programName, PID, "Debug: Program terminating.");
    fclose(logFile);
    return 0;
}

GPIO_Handle initializeGPIO(){
    GPIO_Handle gpio;
    gpio = gpiolib_init_gpio();
    if(gpio == NULL){
        perror("Failed initialization");
    }
    return gpio;
}

bool outputStats(const char* statsFile, const int l1Broken, const int l2Broken, const int numIn, const int numOut, const char* cTime){
    //////////////////////////////////////////////
    ///////        Output Stats         //////////
    FILE* stats = fopen(statsFile, "w");

    if(!stats){
        return false;
    }

    fprintf(stats, "=== Statistics for Laser Lab || Last Updated: %s ===\n", cTime);
    fprintf(stats, "Laser 1 was broken %i times\n", l1Broken);
    fprintf(stats, "Laser 2 was broken %i times\n", l2Broken);
    fprintf(stats, "%i objects entered the room\n", numIn);
    fprintf(stats, "%i objects exited the room\n\n", numOut);
    fclose(stats);

    return true;
}

int laserDiodeStatus(GPIO_Handle gpio, int diodeNumber){
    if(diodeNumber == 1){
        return readPin(gpio, LASER1);
    }
    if(diodeNumber == 2){
        return readPin(gpio, LASER2);
    }
    return -1;
}

void readConfig(FILE* configFile, int* timeout, char* logFileName, char* StatsFile){
    //Get the program
    int PID = getpid();
    char* programName = "Laser Lab";
    char cTime[30];

    //Parameters we are allowing the user to set
    //WATCHDOG_TIMEOUT
    //LOGFILE
    //STATSFILE
    //numOfParameters: 3

    //Index, buffer, and line counter
    int count = 0;
    char buffer[255];
    int lineNum = 0;

    //Timeout and enum
    *timeout = 0;
    
    
    //Set up the temporary log file. If something goes wrong
    //And we don't have access to the logfile, output to default
    FILE* logFile = fopen(LOGFILE_DEFAULT, "rw");

    if(logFile == NULL){
        fprintf(stderr, "ERROR: Could not open default log file! Continuing config read...\n");
    }
    else{
        PRINT_MSG(logFile, cTime, programName, PID, "Debug: Successfully opened default log file.");
    }
    //fprintf(stderr, "Beginning loop\n");
    while(fgets(buffer,255,configFile) != NULL){
        count = 0;
        //fprintf(stderr, "Next Line\n");
        if(buffer[count] != '#' && !isWhiteSpace(buffer[0])){
            //Some variables for each line
            int parameterStart = 0;
            int parameterLength = 0;
            bool gotParameter = false;

            int valueStart = 0;
            int valueLength = 0;
            bool gotValue = false;

            //We are now reading a non-comment line
            CfgFileState cState = CFGSTART;
            //fprintf(stderr, "Beginning line loop\n");
            //fprintf(stderr, "%s", buffer);
            while(cState != CFGEXIT){
                getTime(cTime);
                //fprintf(stderr, "%i\n", count);
                switch(cState){
                    case CFGSTART:

                        if(isAlphaNumeric(buffer[count])){
                            //Store the beginning index of the parameter name
                            //And go to the parameter name state
                            //fprintf(stderr, "Found letter, transitioning to GOT_PARAMETER_NAME\n");
                            parameterStart = count;
                            parameterLength++;
                            cState = GOT_PARAMETER_NAME;
                        }
                        else if(!isWhiteSpace(buffer[count])){
                            //The first item in the line is not a alphanumeric
                            //If it whitespace, we still might have a parameter name
                            //Otherwise we're done.
                            cState = CFGDONE;

                            PRINT_MSG(logFile, cTime, programName, PID, "Warning: Got invalid character at beginning of line in config file.");
                        }
                        else{
                            PRINT_MSG(logFile, cTime, programName, PID, "ERROR: Fatal error in config file reading. This should not be seen.");
                            cState = CFGEXIT;
                        }
                    case GOT_PARAMETER_NAME:
                        if(isAlphaNumeric(buffer[count])){
                            parameterLength++;
                            //fprintf(stderr, "%c incremented the length\n", buffer[count]);
                            break;
                        }
                        if(isWhiteSpace(buffer[count])){
                            //fprintf(stderr, "Got to end of token 1\n");
                            cState = PARAMETER_DONE;
                            break;
                        }
                        if(buffer[count] == '='){
                            gotParameter = true;
                            cState = GOT_EQUALSIGN;
                            break;
                        }
                        else{
                            PRINT_MSG(logFile, cTime, programName, PID, "Warning: Got invalid character while reading parameter name in config file.");
                            break;
                        }
                    case PARAMETER_DONE:
                        //We are now awaiting an equal sign
                        if(buffer[count] == '='){
                            //fprintf(stderr, "Got equal sign, awaiting value\n");
                            gotParameter = true;
                            cState = GOT_EQUALSIGN;
                            break;
                        }
                        if(!isWhiteSpace(buffer[count])){
                            PRINT_MSG(logFile, cTime, programName, PID, "Warning: Got invalid character before equal sign in config file.");
                            cState = CFGDONE;
                            break;
                        }
                        break;
                    case GOT_EQUALSIGN:
                        //Await a valid value character
                        if(isValidValueCharacter(buffer[count])){
                            //fprintf(stderr, "Got start of value: %c\n", buffer[count]);

                            valueStart = count;
                            valueLength++;
                            cState = GOT_VALUE;
                            break;
                        }
                        if(!isWhiteSpace(buffer[count])){
                            PRINT_MSG(logFile, cTime, programName, PID, "Warning: Got invalid character after equal sign in config file.");
                            //fprintf(stderr, "Found invalid character after whitespace\n");
                            break;
                        }
                        break;
                    case GOT_VALUE:
                        if(isValidValueCharacter(buffer[count])){
                            //fprintf(stderr, "%c incremented value length\n", buffer[count]);
                            valueLength++;
                            break;
                        }
                        if(isWhiteSpace(buffer[count]) || buffer[count] == '\0'){
                            //fprintf(stderr, "Finished Value\n");
                            gotValue = true;
                            cState = CFGDONE;
                            break;
                        }
                        else{
                            PRINT_MSG(logFile, cTime, programName, PID, "Warning: Got invalid character while reading value in config file.");
                            break;
                        }
                    case CFGDONE:
                        if(gotValue && gotParameter){
                            //We have a value and a parameter, now we must find out which one
                            //fprintf(stderr, "Parameter Started at: %i \n with length: %i \n\n", parameterStart, parameterLength);

                            //Construct the tokens
                            char* parameterName = constructToken(buffer, parameterStart, parameterLength);
                            char* parameterValue = constructToken(buffer, valueStart, valueLength);

                            //fprintf(stderr, "Parameter Name: %s with length %i\n", parameterName, parameterLength);
                            //fprintf(stderr, "Parameter Value: %s with length %i\n\n", parameterValue, valueLength);

                            if(stringEqual("WATCHDOG_TIMEOUT", parameterName, parameterLength)){
                                
                                //Number
                                if(tokenIsNumber(parameterValue, valueLength)){
                                    int cPower = 0;
                                    int cTimeout = 0;

                                    //Calculate the value based on the digits
                                    for(int i = (valueLength - 1); i >= 0; i--){
                                        cTimeout += ((parameterValue[i] - '0') * pow(10,cPower));
                                        cPower++;
                                    }
                                    
                                    //Time out of bounds
                                    if(cTimeout > WATCHDOGTIMEOUT_MAX){
                                        PRINT_MSG(logFile, cTime, programName, PID, "Warning: Invalid value for WATCHDOG_TIMEOUT (>Max). Continuing with default.");
                                        *timeout = WATCHDOGTIMEOUT_DEFAULT;
                                    }
                                    if(cTimeout < WATCHDOGTIMEOUT_MIN){
                                        PRINT_MSG(logFile, cTime, programName, PID, "Warning: Invalid value for WATCHDOG_TIMOUT (<Min). Continuing with default.");
                                        *timeout = WATCHDOGTIMEOUT_DEFAULT;
                                    }

                                    //Valid value
                                    else{
                                        //Everything is okay, set the timeout value
                                        fprintf(stderr, "Setting the timeout to %i\n", cTimeout);
                                        *timeout = cTimeout;
                                    }
                                }

                                //Non-number
                                else{
                                    PRINT_MSG(logFile, cTime, programName, PID, "Warning: Invalid value for WATCHDOG_TIMEOUT (not a number). Continuing with default.");
                                    *timeout = WATCHDOGTIMEOUT_DEFAULT;                                    
                                }

                            }
                            else if(stringEqual("LOGFILE", parameterName, parameterLength)){
                                //The only check we put on filenames is that they cannot be numbers
                                if(!tokenIsNumber(parameterValue, valueLength)){
                                    //fprintf(stderr, "Setting the logfilename\n");
                                    copyParam(parameterValue, logFileName, valueLength-1);
                                }
                                else{
                                    PRINT_MSG(logFile, cTime, programName, PID, "Warning: Invalid value for LOGFILE");                                    
                                }
                            }
                            
                            //DEPRECATED
                            /*else if(stringEqual("DEFAULT_TIME", parameterName, parameterLength)){
                                if(tokenIsNumber(parameterValue, valueLength)){
                                    int cPower = 0;
                                    int cTimeDefault = 0;
                                    for(int i = (valueLength - 1); i >= 0; i--){
                                        cTimeDefault += ((parameterValue[i] - '0') * pow(10,cPower));
                                        cPower++;
                                    }

                                    fprintf(stderr, "Setting the functime to %i\n", cTimeDefault);
                                    *funcTime = cTimeDefault;
                                }
                                else{
                                    PRINT_MSG(logFile, cTime, programName, PID, "Warning: Invalid value for DEFAULT_TIME");
                                }
                            }*/

                            else if(stringEqual("STATSFILE", parameterName, parameterLength)){
                                //The only check we put on filenames is that they cannot be numbers
                                if(!tokenIsNumber(parameterValue, valueLength)){
                                    //fprintf(stderr, "Setting statsfile\n");
                                    copyParam(parameterValue, StatsFile, valueLength-1);
                                }
                                else{
                                    PRINT_MSG(logFile, cTime, programName, PID, "Warning: Invalid value for STATSFILE");                                    
                                }
                            }
                            else{
                                PRINT_MSG(logFile, cTime, programName, PID, "Warning: Got a parameter name which matched no known parameter");
                            }

                            //We are done with the line
                            //fprintf(stderr, "Done reading cfg line, exiting loop\n");
                            cState = CFGEXIT;
                            break;
                        }
                        else{
                            PRINT_MSG(logFile, cTime, programName, PID, "Warning: Failure in reading config file line.");
                            break;
                        }
                    default:
                        break;          
                }

                count++;    
            }
            lineNum++;
        }
    }
}

void getTime(char* buffer){
    struct timeval tv;

    time_t curTime;

    gettimeofday(&tv, NULL);

    curTime = tv.tv_sec;

    strftime(buffer, 30, "%Y-%m-%d  %T", localtime(&curTime));
}

bool isAlphaNumeric(const char c){
    if(c >= 'a' && c <= 'z'){
        return true;
    }
    if(c >= 'A' && c <= 'Z'){
        return true;
    }
    if(c >= '0' && c <= '9'){
        return true;
    }
    if(c == '_'){
        return true;
    }
    return false;
}
bool isNumber(const char c){
    if(c >= '0' && c <= '9'){
        return true;
    }
    return false;
}
bool isValidValueCharacter(const char c){
    if(isAlphaNumeric(c) || c == '/' || c == '.'){
        return true;
    }
    return false;
}
bool isWhiteSpace(const char c){
    if(c == ' ' || c == '\t'){
        return true;
    }
    return false;
}
bool tokenIsNumber(const char* c, int length){
    for(int i = 0; i < length; i++){
        //fprintf(stderr, "%c was checked (%i)\n", c[i], i);
        if(!isNumber(c[i])){
            return false;
        }
    }
    return true;
}
char* constructToken(const char* buffer, int start, int length){
    char* returnValue = (char*) malloc(length * sizeof(char));
    //Copy in the values
    int retCounter = 0;
    for(int i = start; i < start + length; i++){
        returnValue[retCounter] = buffer[i];

        retCounter++;
    }

    //Return the token
    return returnValue;
}

void copyParam(char* param, char* to, int paramlength){

    for(int i = 0; i <= paramlength; i++){
        to[i] = param[i];
    }
    to[paramlength+1] = '\0';
    //fprintf(stderr, "set value to: %s\n", to);
}
bool stringEqual(const char* a, const char* parameter, const int parameterLength){
    int ai = 0;
    //fprintf(stderr, "%s | %s: ", a, parameter);
    while(a[ai] != '\0'){
        //fprintf(stderr, "%s", a[ai]);
        ai++;
    }

    if(ai != parameterLength-1 ){
        //fprintf(stderr, " Returned False (incorrect length): %i %i.\n", ai, parameterLength-1);
        return false;
    }

    for(int i = 0; i <= ai; i++){
        if(a[i] != parameter[i] && !isWhiteSpace(a[i]) && !isWhiteSpace(parameter[i])){
            //fprintf(stderr, "%s | %s", a[i], b[i]);
            //fprintf(stderr, " Returned False (different chars: %c %c).\n", a[i], parameter[i]);
            return false;
        }
    }
    //fprintf(stderr, " Returned True.\n");
    return true;
}