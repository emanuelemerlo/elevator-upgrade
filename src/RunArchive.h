/**********************************************************************************
*        File: RunArchive.h
* Description: Results of the completed runs, to compare strategies and configurations.
*              Every record is a flat JSON object (settings, scenario, results) kept in memory and
*              appended as one line to a file (JSON Lines), read again at the next start.
*      Author: Emanuele Merlo (emanuele.merlo@gmail.com)
**********************************************************************************/

#pragma once

#include <string>
#include <vector>

namespace RunArchive
{
  /** \brief Reads the records of the previous sessions. A missing file is an empty archive. */
  void Load(const std::string& path);

  /** \brief Adds a record (a JSON object on one line) and appends it to the file. */
  void Add(const std::string& record);

  std::vector<std::string> All();

  /** \brief Empties the archive and the file. */
  void Clear();

  /** \brief A new identifier for a record or a batch (increasing, unique within the archive). */
  std::string NewId(const std::string& prefix);
}
