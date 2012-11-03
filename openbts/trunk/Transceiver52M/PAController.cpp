/* 
 * Written by Kurtis Heimerl <kheimerl@cs.berkeley.edu>
 *
 * Copyright (c) 2011 Kurtis Heimerl
 *
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 
 * Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 
 * Neither the name of the project's author nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <stdio.h>
#include <time.h>
#include <Logger.h>
#include <fcntl.h>
#include <string.h>
#include <Configuration.h>
#include "PAController.h"
#include "config.h"

#if defined USE_UHD || defined USE_USRP1
#define DONT_USE_SERIAL 1
#endif

extern ConfigurationTable gConfig;

using namespace std;

//I hate C++ -kurtis

//should make these configurable later
#define ON_CMD "O0=1\r"
#define OFF_CMD "O0=0\r"

static bool pa_on = false;
static Mutex pa_lock;
static time_t last_update = NULL;
//hack for now, as I want one source file for both RAD1 and UHD/USRP1

/* assumes you hold the lock */
static void actual_pa_off(){
  LOG(ALERT) << "PA Off";
  pa_on = false;
#ifndef DONT_USE_SERIAL
  fcntl(fd1,F_SETFL,0);
  write(fd1,OFF_CMD, strlen(OFF_CMD));
#endif
}

static void turn_pa_on(bool resetTime){
  ScopedLock lock (pa_lock);
  //don't think I need to garbage collect, it's just an int
  if (!pa_on || resetTime){
    LOG(ALERT) << "PA On";
    last_update = time(NULL);
    pa_on = true;
#ifndef DONT_USE_SERIAL
    fcntl(fd1,F_SETFL,0);
    write(fd1,ON_CMD, strlen(ON_CMD));
#endif
  }
}

static void turn_pa_off(){
  ScopedLock lock (pa_lock);
  actual_pa_off();
}

/* key point: this is being called all the time
   by the transceiver, allowing it to be updated
   almost immediately after time stamp ends */
bool update_pa(){
  int pa_timeout = gConfig.getNum("VBTS.PA.Timeout", 5 * 60);
  ScopedLock lock (pa_lock);
  if (pa_on && last_update && 
      time(NULL) > pa_timeout + last_update){
    actual_pa_off();
  }
  return pa_on;
}

//the "turn the PA on method"
class on_method : public xmlrpc_c::method {
public:
  on_method() {
    this->_signature = "n:";
    this->_help = "This method turns the PA on";
  }
  void
  execute(xmlrpc_c::paramList const& paramList,
	  xmlrpc_c::value *   const  retvalP) {
    turn_pa_on(true);
    *retvalP = xmlrpc_c::value_nil();
  }
};

//the "turn the PA on method"
class off_method : public xmlrpc_c::method {
public:
  off_method() {
    this->_signature = "n:"; 
    this->_help = "This method turns the PA off";
  }
  void
  execute(xmlrpc_c::paramList const& paramList,
	  xmlrpc_c::value *   const  retvalP) {
    turn_pa_off();
    *retvalP = xmlrpc_c::value_nil();
  }
};

//the "turn the PA on method"
class status_method : public xmlrpc_c::method {
public:
  status_method() {
    this->_signature = "b:";
    this->_help = "This method returns the PA status";
  }
  void
  execute(xmlrpc_c::paramList const& paramList,
	  xmlrpc_c::value *   const  retvalP) {
    *retvalP = xmlrpc_c::value_boolean(update_pa());
  }
};

PAController::PAController()
{

#ifndef DONT_USE_SERIAL
  string serial_loc = gConfig.getStr("VBTS.PA.SerialLoc", "/dev/ttyACM0");
  fd1 = open (SERIAL_LOC, O_RDWR | O_NOCTTY | O_NDELAY);
#endif

  registry = new xmlrpc_c::registry();

  xmlrpc_c::methodPtr const onMethod(new on_method);
  xmlrpc_c::methodPtr const offMethod(new off_method);
  xmlrpc_c::methodPtr const statusMethod(new status_method);

  registry->addMethod("on", onMethod);
  registry->addMethod("off", offMethod);
  registry->addMethod("status", statusMethod);

  long rpc_port = gConfig.getNum("VBTS.PA.RPCPort", 8080);
  string rpc_log = gConfig.getStr("VBTS.PA.RPCLogLoc", "/tmp/xmlrpc.log");

  RPCServer = new xmlrpc_c::serverAbyss(*registry,
					rpc_port,
					rpc_log
					);
}

PAController::~PAController()
{
  //should call the deconstructor and close cleanly... right?
  delete RPCServer;
  delete registry;
}

void PAController::run()
{
  RPCServer->run();
}

void PAController::on()
{
  turn_pa_on(false);
}

void PAController::off()
{
  turn_pa_off();
}

bool PAController::state()
{
  return update_pa();
}

/* non-member functions */
void runController(PAController* cont)
{
  Thread RPCThread;
  RPCThread.start((void*(*)(void*)) &PAController::run, cont);
  cont->on();
}
