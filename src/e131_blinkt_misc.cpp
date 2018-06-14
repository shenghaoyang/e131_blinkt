/**
 * \file e131_blinkt_misc.cpp
 *
 * \copyright Shenghao Yang, 2018
 *
 * See LICENSE for details
 */
#include <e131_blinkt.hpp>

namespace e131_blinkt
{

config_settings::config_settings(const libconfig::Config& conf,
                                 const std::string& path)
    : blinkt{path}, e131{conf.lookup("e131_blinkt.e131.universe"),
                         conf.lookup("e131_blinkt.e131.max_sources"),
                         conf.lookup("e131_blinkt.e131.offset"),
                         conf.lookup("e131_blinkt.e131.ignore_preview_flag")}
{
}

std::ostream&
operator<<(std::ostream& ost, const config_settings& settings)
{
  ost << "Configuration settings:" << std::endl;
  ost << "Blinkt settings:" << std::endl;
  ost << "\tSPI device: " << settings.blinkt.path << std::endl;

  ost << "E1.31 settings:" << std::endl;
  ost << "\tUniverse: " << settings.e131.universe << std::endl;
  ost << "\tMax sources: " << settings.e131.max_sources << std::endl;
  ost << "\tDMX channel offset: " << settings.e131.offset << std::endl;
  ost << "\tPreview flag ignored: " << settings.e131.ignore_preview_flag
      << std::endl;
  return ost;
}

std::ostream&
operator<<(std::ostream& ost, std::map<std::string, docopt::value> m)
{
  for (const auto& i : m) { ost << i.first << ": " << i.second << std::endl; }
  return ost;
}
} // namespace e131_blinkt
