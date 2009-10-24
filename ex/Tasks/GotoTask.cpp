/* Generated by Together */

#include "GotoTask.h"
#include "Navigation/Waypoint.hpp"
#include "BaseTask/TaskPoint.hpp"
#include <stdlib.h>
#include <fstream>

GotoTask::GotoTask(const TaskEvents &te, 
                   TaskAdvance &ta,
                   GlidePolar &gp): 
  AbstractTask(te,ta,gp),
  tp(NULL) 
{
}

GotoTask::~GotoTask() {
  if (tp) {
    delete tp;
  }
}

TaskPoint* 
GotoTask::getActiveTaskPoint() { 
  return tp;
}

void 
GotoTask::setActiveTaskPoint(unsigned index)
{
  // nothing to do
}


void 
GotoTask::report(const AIRCRAFT_STATE &state)
{
  if (tp) {
    std::ofstream f1("res-goto.txt");
    tp->print(f1,state);
  }
}

bool 
GotoTask::update_sample(const AIRCRAFT_STATE &state,
                        const bool full_update)
{
  return false; // nothing to do
}


bool 
GotoTask::check_transitions(const AIRCRAFT_STATE &, const AIRCRAFT_STATE&)
{
  return false; // nothing to do
}

void 
GotoTask::do_goto(const WAYPOINT & wp)
{
  if (tp) {
    delete tp;
  }
  tp = new TaskPoint(wp);
}

