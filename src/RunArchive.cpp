#include "RunArchive.h"

#include <chrono>
#include <fstream>
#include <mutex>

namespace
{
  std::mutex g_mutex;
  std::string g_path;
  std::vector<std::string> g_records;
  unsigned long long g_lastId{ 0U };
}

void RunArchive::Load(const std::string& path)
{
  std::lock_guard<std::mutex> lock(g_mutex);

  g_path = path;
  g_records.clear();

  std::ifstream file(path);
  std::string line;
  while (std::getline(file, line))
  {
    // Only lines that look like the records written by Add(): a damaged line is skipped, not fatal.
    if (line.size() > 2U && line.front() == '{' && line.back() == '}')
      g_records.push_back(line);
  }
}

void RunArchive::Add(const std::string& record)
{
  std::lock_guard<std::mutex> lock(g_mutex);

  g_records.push_back(record);

  if (g_path.empty())
    return;

  std::ofstream file(g_path, std::ios::out | std::ios::app);
  if (file.is_open())
    file << record << '\n';
}

std::vector<std::string> RunArchive::All()
{
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_records;
}

void RunArchive::Clear()
{
  std::lock_guard<std::mutex> lock(g_mutex);

  g_records.clear();

  if (!g_path.empty())
    std::ofstream(g_path, std::ios::out | std::ios::trunc);
}

std::string RunArchive::NewId(const std::string& prefix)
{
  std::lock_guard<std::mutex> lock(g_mutex);

  // Milliseconds of the wall clock, kept increasing: unique also across sessions sharing the same file.
  const auto now = static_cast<unsigned long long>(std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::system_clock::now().time_since_epoch()).count());
  g_lastId = now > g_lastId ? now : g_lastId + 1U;
  return prefix + std::to_string(g_lastId);
}
