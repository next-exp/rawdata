#include <mysql.h>
#include <stdio.h>
#include <string>

#include "database/sensors.h"
#include "database/huffman.h"

#ifndef _READCONFIG
#include "config/ReadConfig.h"
#endif

#ifndef SPDLOG_VERSION
#include "spdlog/spdlog.h"
#endif

enum class HuffmannSensor { sipm, pmt };

void finish_with_error(MYSQL *con, std::shared_ptr<spdlog::logger> log);
void getSensorsFromDB(ReadConfig * config, next::Sensors &sensors, int run_number, bool masked);
void getHuffmanFromDB(ReadConfig * config, Huffman * huffman, int run_number, HuffmannSensor sensor);
