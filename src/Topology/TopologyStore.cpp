/*
Copyright_License {

  XCSoar Glide Computer - http://www.xcsoar.org/
  Copyright (C) 2000, 2001, 2002, 2003, 2004, 2005, 2006, 2007, 2008, 2009

	M Roberts (original release)
	Robin Birch <robinb@ruffnready.co.uk>
	Samuel Gisiger <samuel.gisiger@triadis.ch>
	Jeff Goodenough <jeff@enborne.f2s.com>
	Alastair Harrison <aharrison@magic.force9.co.uk>
	Scott Penrose <scottp@dd.com.au>
	John Wharington <jwharington@gmail.com>
	Lars H <lars_hn@hotmail.com>
	Rob Dunning <rob@raspberryridgesheepfarm.com>
	Russell King <rmk@arm.linux.org.uk>
	Paolo Ventafridda <coolwind@email.it>
	Tobias Lohner <tobias@lohner-net.de>
	Mirek Jezek <mjezek@ipplc.cz>
	Max Kellermann <max@duempel.org>
	Tobias Bieniek <tobias.bieniek@gmx.de>

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
}
*/

#include "Topology/TopologyStore.hpp"
#include "Protection.hpp"
#include "Topology/Topology.hpp"
#include "Dialogs.h"
#include "Language.hpp"
#include "Compatibility/string.h"
#include "Profile.hpp"
#include "LocalPath.hpp"
#include "UtilsText.hpp"
#include "StringUtil.hpp"
#include "LogFile.hpp"
#include "SettingsUser.hpp" // for EnableTopology
#include "Interface.hpp"
#include "IO/ZipLineReader.hpp"

#include <assert.h>

void
TopologyStore::TriggerUpdateCaches(Projection &m_projection)
{
  Poco::ScopedRWLock protect(lock, true);

  for (int z = 0; z < MAXTOPOLOGY; z++) {
    if (topology_store[z])
      topology_store[z]->triggerUpdateCache = true;
  }

  // check if things have come into or out of scale limit
  for (int z = 0; z < MAXTOPOLOGY; z++) {
    if (topology_store[z])
      topology_store[z]->TriggerIfScaleNowVisible(m_projection);
  }
}

bool
TopologyStore::ScanVisibility(Projection &m_projection,
    rectObj &_bounds_active, const bool force)
{
  Poco::ScopedRWLock protect(lock, true);

  // check if any needs to have cache updates because wasnt
  // visible previously when bounds moved
  bool first = true;
  bool remaining = false;

  // we will make sure we update at least one cache per call
  // to make sure eventually everything gets refreshed
  for (int z = 0; z < MAXTOPOLOGY; z++) {
    if (topology_store[z]) {
      bool update = force || first;
      bool purge_only = !update;

      if (topology_store[z]->triggerUpdateCache)
        first = false;

      topology_store[z]->updateCache(m_projection, _bounds_active, purge_only);
      remaining |= (topology_store[z]->triggerUpdateCache);
    }
  }

  return remaining;
}

TopologyStore::~TopologyStore()
{
  Close();
}

void
TopologyStore::Close()
{
  LogStartUp(TEXT("CloseTopology"));

  Poco::ScopedRWLock protect(lock, true);

  for (int z = 0; z < MAXTOPOLOGY; z++) {
    if (topology_store[z])
      delete topology_store[z];
  }
}

/**
 * Draws the topology to the given canvas
 * @param canvas The drawing canvas
 * @param rc The area to draw in
 */
void
TopologyStore::Draw(Canvas &canvas, BitmapCanvas &bitmap_canvas,
                    const Projection &projection, LabelBlock &label_block,
                    const SETTINGS_MAP &settings_map)
{
  Poco::ScopedRWLock protect(lock, false);

  for (int z = 0; z < MAXTOPOLOGY; z++) {
    if (topology_store[z])
      topology_store[z]->Paint(canvas, bitmap_canvas, projection,
                               label_block, settings_map);
  }
}

void
TopologyStore::Open()
{
  LogStartUp(TEXT("OpenTopology"));

  XCSoarInterface::CreateProgressDialog(gettext(TEXT("Loading Topology File...")));

  Poco::ScopedRWLock protect(lock, true);

  // Start off by getting the names and paths
  TCHAR szFile[MAX_PATH];
  TCHAR Directory[MAX_PATH];

  for (int z = 0; z < MAXTOPOLOGY; z++) {
    topology_store[z] = 0;
  }

  Profile::Get(szProfileTopologyFile, szFile, MAX_PATH);
  ExpandLocalPath(szFile);

  if (string_is_empty(szFile)) {
    // file is blank, so look for it in a map file
    Profile::Get(szProfileMapFile, szFile, MAX_PATH);
    if (string_is_empty(szFile))
      return;

    ExpandLocalPath(szFile);

    // Look for the file within the map zip file...
    _tcscpy(Directory, szFile);
    _tcscat(Directory, TEXT("/"));
    _tcscat(szFile, TEXT("/"));
    _tcscat(szFile, TEXT("topology.tpl"));
  } else {
    ExtractDirectory(Directory, szFile);
  }

  // Ready to open the file now..
  ZipLineReader reader(szFile);
  if (reader.error()) {
    LogStartUp(TEXT("No topology file: %s"), szFile);
    return;
  }

  TCHAR ctemp[80];
  TCHAR *TempString;
  TCHAR ShapeName[50];
  double ShapeRange;
  long ShapeIcon;
  long ShapeField;
  TCHAR wShapeFilename[MAX_PATH];
  TCHAR *Stop;
  int numtopo = 0;
  char ShapeFilename[MAX_PATH];

  while ((TempString = reader.read()) != NULL) {
    // Look For Comment
    if (string_is_empty(TempString)
        || (_tcsstr(TempString, TEXT("*")) == TempString))
      continue;

    BYTE red, green, blue;
    // filename,range,icon,field

    // File name
    PExtractParameter(TempString, ctemp, 0);
    _tcscpy(ShapeName, ctemp);

    _tcscpy(wShapeFilename, Directory);
    _tcscat(wShapeFilename, ShapeName);
    _tcscat(wShapeFilename, TEXT(".shp"));

#ifdef _UNICODE
    WideCharToMultiByte(CP_ACP, 0, wShapeFilename,
        _tcslen(wShapeFilename) + 1, ShapeFilename, 200, NULL, NULL);
#else
    strcpy(ShapeFilename, wShapeFilename);
#endif

    // Shape range
    PExtractParameter(TempString, ctemp, 1);
    ShapeRange = _tcstod(ctemp, NULL);

    // Shape icon
    PExtractParameter(TempString, ctemp, 2);
    ShapeIcon = _tcstol(ctemp, &Stop, 10);

    // Shape field for text display
    // sjt 02NOV05 - field parameter enabled
    PExtractParameter(TempString, ctemp, 3);
    if (_istalnum(ctemp[0])) {
      ShapeField = _tcstol(ctemp, &Stop, 10);
      ShapeField--;
    } else {
      ShapeField = -1;
    }

    // Red component of line / shading colour
    PExtractParameter(TempString, ctemp, 4);
    red = (BYTE)_tcstol(ctemp, &Stop, 10);

    // Green component of line / shading colour
    PExtractParameter(TempString, ctemp, 5);
    green = (BYTE)_tcstol(ctemp, &Stop, 10);

    // Blue component of line / shading colour
    PExtractParameter(TempString, ctemp, 6);
    blue = (BYTE)_tcstol(ctemp, &Stop, 10);

    if ((red == 64) && (green == 96) && (blue == 240)) {
      // JMW update colours to ICAO standard
      red = 85; // water colours
      green = 160;
      blue = 255;
    }

    if (ShapeField < 0) {
      Topology* newtopo;
      newtopo = new Topology(ShapeFilename, Color(red, green, blue));
      topology_store[numtopo] = newtopo;
    } else {
      TopologyLabel *newtopol;
      newtopol = new TopologyLabel(ShapeFilename, Color(red, green, blue),
          ShapeField);
      topology_store[numtopo] = newtopol;
    }

    if (ShapeIcon != 0)
      topology_store[numtopo]->loadIcon(ShapeIcon);

    topology_store[numtopo]->scaleThreshold = ShapeRange;

    numtopo++;
  }
}
