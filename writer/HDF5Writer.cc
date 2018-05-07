#include "writer/HDF5Writer.h"
#include <sstream>
#include <cstring>
#include <stdlib.h>

#include<stdint.h>

namespace spd = spdlog;

next::HDF5Writer::HDF5Writer(ReadConfig * config) :
    _file(0), _pmtrd(0), _ievt(0), _config(config)
{

	_log = spd::stdout_color_mt("writer");
	if(config->verbosity() > 0){
		_log->set_level(spd::level::debug);
	}
	_nodb = config->no_db();
}

next::HDF5Writer::~HDF5Writer(){
}

void next::HDF5Writer::Open(std::string fileName){
	_log->debug("Opening output file {}", fileName);
	_firstEvent= true;

	_file =  H5Fcreate( fileName.c_str(), H5F_ACC_TRUNC,
			H5P_DEFAULT, H5P_DEFAULT );

	//Group for runinfo
	std::string run_group_name = std::string("/Run");
	_rinfoG = createGroup(_file, run_group_name);

	std::string events_table_name = std::string("events");
	_memtypeEvt = createEventType();
	_eventsTable = createTable(_rinfoG, events_table_name, _memtypeEvt);

	_isOpen=true;
}

void next::HDF5Writer::Close(){
  _isOpen=false;

  _log->debug("Closing output file");
  H5Fclose(_file);
}

void next::HDF5Writer::Write(DigitCollection& pmts, DigitCollection& blrs,
		DigitCollection& extPmt, DigitCollection& sipms,
		std::vector<std::pair<std::string, int> > triggerInfo,
		std::vector<int> triggerChans,
		std::uint64_t timestamp, unsigned int evt_number, size_t run_number){
	_log->debug("Writing event to HDF5 file");

	//Write event number & timestamp
	evt_t evtData;
	evtData.evt_number = evt_number;
	evtData.timestamp = timestamp;
	writeEvent(&evtData, _eventsTable, _memtypeEvt, _ievt);

//	for(int i=0; i<triggerChans.size(); i++){
//		std::cout << "trigger channel: " << triggerChans[i] << std::endl;
//	}

	//Get number of sensors
	int total_pmts  = _sensors.getNumberOfPmts();
	int total_blrs  = _sensors.getNumberOfPmts();
	int total_sipms = _sensors.getNumberOfSipms();
	hsize_t npmt   = pmts.size();
	hsize_t nblr   = blrs.size();
	hsize_t nsipm = sipms.size();

	std::vector<next::Digit*> active_sipms;
	if(_nodb){
		total_pmts = npmt;
		total_blrs = nblr;

		select_active_sensors(&active_sipms, sipms);
		total_sipms = active_sipms.size();
	}

	_hasPmts  = npmt > 0;
	_hasBlrs  = nblr > 0;
	_hasSipms = nsipm > 0;

	//Waveform size
	hsize_t pmtDatasize  = 0;
	hsize_t sipmDatasize = 0;
	hsize_t extPmtDatasize = 0;

	if (_hasPmts){
		pmtDatasize    = pmts[0].nSamples();
	}
	if (_hasSipms){
		sipmDatasize   = sipms[0].nSamples();
	}
	if(extPmt.size() > 0){
		extPmtDatasize = extPmt[0].nSamples();
	}

	if (_firstEvent){
		//Load sensors data from DB
		getSensorsFromDB(_config, _sensors, run_number, true);

		//Run info (to be moved away)
		hsize_t memtype_run = createRunType();
		std::string run_name = std::string("runInfo");
		hid_t runinfo_table = createTable(_rinfoG, run_name, memtype_run);
		runinfo_t runinfo;
		runinfo.run_number = (int) run_number;
		writeRun(&runinfo, runinfo_table, memtype_run, _ievt);

		// Trigger info
		if(triggerInfo.size() > 0){
			saveTriggerInfo(triggerInfo);
		}

		//Create group
		std::string groupName = std::string("/RD");
		hid_t group = createGroup(_file, groupName);

		//Create pmt array
		if (_hasPmts){
			std::string pmt_name = std::string("pmtrwf");
			_pmtrd = createWaveforms(group, pmt_name, total_pmts, pmtDatasize);

			//Create trigger array
			std::string trigger_name = std::string("events");
			_triggerd = createWaveform(_triggerG, trigger_name, total_pmts);
		}

		//Create blr array
		if (_hasBlrs){
			std::string blr_name = std::string("pmtblr");
			_pmtblr = createWaveforms(group, blr_name, total_pmts, pmtDatasize);
		}

		//Create sipms array
		if (_hasSipms){
			std::string sipm_name = std::string("sipmrwf");
			_sipmrd = createWaveforms(group, sipm_name, total_sipms, sipmDatasize);
		}

		if(extPmt.size() > 0){
			std::string extPmt_name = std::string("extpmt");
			_extpmtrd = createWaveform(group, extPmt_name, extPmtDatasize);
		}

		_firstEvent = false;

		if(_nodb){
			// Hack to be compatible with IC (always expects pmtrwf,
			// pmtblr, sipmrwf...)
			if(!_hasPmts){
				std::string pmt_name = std::string("pmtrwf");
				_pmtrd = createWaveforms(group, pmt_name, 1, 1);
			}
			if(!_hasBlrs){
				std::string blr_name = std::string("pmtblr");
				_pmtblr = createWaveforms(group, blr_name, 1, 1);
			}
			if (!_hasSipms){
				std::string sipm_name = std::string("sipmrwf");
				_sipmrd = createWaveforms(group, sipm_name, 1, 1);
			}

			//Group for runinfo
			std::string zs_group_name = std::string("/ZS");
			hsize_t zsinfoG = createGroup(_file, zs_group_name);

			std::string pmtzs_name = std::string("pmts");
			hsize_t pmt_zs = createWaveform(zsinfoG, pmtzs_name, MAX_PMTs);

			std::string blrzs_name = std::string("blrs");
			hsize_t blr_zs = createWaveform(zsinfoG, blrzs_name, MAX_PMTs);

			std::string sipmzs_name = std::string("sipms");
			hsize_t sipm_zs = createWaveform(zsinfoG, sipmzs_name, MAX_SIPMs);
		}
	}

	//Need to sort all digits
	std::vector<next::Digit*> sorted_pmts(total_pmts);
	std::vector<next::Digit*> sorted_blrs(total_blrs);
	std::vector<next::Digit*> sorted_sipms(total_sipms);
	if(_nodb){
		sortPmtsNoDB(sorted_pmts, pmts);
		sortPmtsNoDB(sorted_blrs, blrs);
		sortSipmsNoDB(sorted_sipms, active_sipms);

		save_elecids(&_pmt_elecids, sorted_pmts);
		save_elecids(&_blr_elecids, sorted_blrs);
		save_elecids(&_sipm_elecids, sorted_sipms);
	}else{
		sortPmts(sorted_pmts, pmts);
		sortPmts(sorted_blrs, blrs);
		sortSipms(sorted_sipms, sipms);
	}

	//Trigger channels elecID
	std::vector<int> triggers(48, 0);
	for(int i=0; i<triggerChans.size(); i++){
		int ch = triggerChans[i];
		//printf("channel: %d\n", ch);
		triggers[ch] = 1;
	}

//	for(int i=0; i<triggers.size(); i++){
//		printf("ch[%d]: %d\n", i, triggers[i]);
//	}

	//Write waveforms
	//TODO ZS
	if (_hasPmts){
		StorePmtWaveforms(sorted_pmts, total_pmts, pmtDatasize, _pmtrd);
		StoreTriggerChannels(sorted_pmts, triggers, total_pmts, pmtDatasize, _triggerd);
	}
	if (_hasBlrs){
		StorePmtWaveforms(sorted_blrs, total_pmts, pmtDatasize, _pmtblr);
	}
	if (_hasSipms){
		StoreSipmWaveforms(sorted_sipms, total_sipms, sipmDatasize, _sipmrd);
	}

	if(extPmt.size() > 0){
		short int *extPmtdata = new short int[extPmtDatasize];
		int index = 0;
		for(unsigned int sid=0; sid < extPmt.size(); sid++){
			auto wf = extPmt[sid].waveform();
			for(unsigned int samp=0; samp<pmtDatasize; samp++) {
				extPmtdata[index] = (short int) wf[samp];
				index++;
			}
		}
		WriteWaveform(extPmtdata, _extpmtrd, extPmtDatasize, _ievt);
		delete[] extPmtdata;
	}

	_ievt++;
}

void next::HDF5Writer::select_active_sensors(std::vector<next::Digit*> * active_sensors,
		DigitCollection& sensors){
	active_sensors->reserve(sensors.size());
	for(int i=0; i<sensors.size(); i++){
		if (sensors[i].active()){
			active_sensors->push_back(&(sensors[i]));
		}
	}
}

void next::HDF5Writer::saveTriggerInfo(std::vector<std::pair<std::string, int> > triggerInfo){
	//Create group
	std::string triggerGroup = std::string("/Trigger");
	_triggerG = createGroup(_file, triggerGroup);

	hsize_t memtype_trigger = createTriggerType();
	std::string trigger_name = std::string("configuration");
	hid_t trigger_table = createTable(_triggerG, trigger_name, memtype_trigger);
	for(int i=0; i<triggerInfo.size(); i++){
		trigger_t triggerData;
		memset(triggerData.param, 0, STRLEN);
		memcpy(triggerData.param, triggerInfo[i].first.c_str(), triggerInfo[i].first.length());
		triggerData.value = triggerInfo[i].second;
		writeTrigger(&triggerData, trigger_table, memtype_trigger, i);
	}
}

void next::HDF5Writer::save_elecids(std::vector<int> * elecids,
		std::vector<next::Digit*> &sorted_sensors){
	if(!elecids->size()){
		elecids->reserve(sorted_sensors.size());
		for(unsigned int i=0; i<sorted_sensors.size(); i++){
			//(*elecids)[i] = sorted_sensors[i]->chID();
			elecids->push_back(sorted_sensors[i]->chID());
		}
	}
}

void next::HDF5Writer::sortPmts(std::vector<next::Digit*> &sorted_sensors,
		DigitCollection &sensors){
	std::fill(sorted_sensors.begin(), sorted_sensors.end(), (next::Digit*) 0);
	int sensorid;
	for(unsigned int i=0; i<sensors.size(); i++){
		sensorid = _sensors.elecToSensor(sensors[i].chID());
		if(sensorid >= 0){
			sorted_sensors[sensorid] = &(sensors[i]);
		}
	}
}

bool compareDigitsID (next::Digit * d1, next::Digit * d2){
	return d1->chID() < d2->chID();
}

void next::HDF5Writer::sortPmtsNoDB(std::vector<next::Digit*> &sorted_sensors,
		DigitCollection &sensors){
	for(unsigned int i=0; i<sensors.size(); i++){
		sorted_sensors[i] = &(sensors[i]);
	}
	// Sort them according to ElecID
	std::sort(sorted_sensors.begin(), sorted_sensors.end(), compareDigitsID);
}

void next::HDF5Writer::sortSipmsNoDB(std::vector<next::Digit*> &sorted_sensors,
		std::vector<next::Digit*> &sensors){
	for(unsigned int i=0; i<sensors.size(); i++){
		sorted_sensors[i] = sensors[i];
	}
	// Sort them according to ElecID
	std::sort(sorted_sensors.begin(), sorted_sensors.end(), compareDigitsID);
}

void next::HDF5Writer::sortSipms(std::vector<next::Digit*> &sorted_sensors,
		DigitCollection &sensors){
	std::fill(sorted_sensors.begin(), sorted_sensors.end(), (next::Digit*) 0);
	int sensorid, position;
	for(unsigned int i=0; i<sensors.size(); i++){
		sensorid = _sensors.elecToSensor(sensors[i].chID());
		position = SipmIDtoPosition(sensorid);
		if(sensorid >= 0){
			sorted_sensors[position] = &(sensors[i]);
		}
	}
}

void next::HDF5Writer::StoreTriggerChannels(std::vector<next::Digit*> sensors,
		std::vector<int> triggers, hsize_t nsensors, hsize_t datasize, hsize_t dataset){
	short int *data = new short int[nsensors];
	for(int i=0; i<sensors.size(); i++){
		if(sensors[i]){
			int ch = sensors[i]->chID();
			data[i] = triggers[ch];
		}else{
			data[i] = 0;
		}
	}
	WriteWaveform(data, _triggerd, nsensors, _ievt);
	delete[] data;
}

//For PMTs missing sensors are filled at the end
void next::HDF5Writer::StorePmtWaveforms(std::vector<next::Digit*> sensors,
	   	hsize_t nsensors, hsize_t datasize, hsize_t dataset){
	short int *data = new short int[nsensors * datasize];
	int index = 0;
	int activeSensors = 0;

	for(unsigned int sid=0; sid < sensors.size(); sid++){
		if(sensors[sid]){
			activeSensors++;
			auto wf = sensors[sid]->waveform();
			for(unsigned int samp=0; samp<sensors[sid]->nSamples(); samp++) {
				data[index] = (short int) wf[samp];
				index++;
			}
		}
	}

	for(unsigned int sid=0; sid < (nsensors-activeSensors) * datasize; sid++){
			data[index] = (short int) 0;
			index++;
	}

	WriteWaveforms(data, dataset, nsensors, datasize, _ievt);
	delete[] data;
}

//For SIPMs missing sensors are filled in place
void next::HDF5Writer::StoreSipmWaveforms(std::vector<next::Digit*> sensors,
		hsize_t nsensors, hsize_t datasize, hsize_t dataset){
	short int *data = new short int[nsensors * datasize];
	int index = 0;
	int activeSensors = 0;

	for(unsigned int sid=0; sid < sensors.size(); sid++){
		if(sensors[sid]){
			activeSensors++;
			auto wf = sensors[sid]->waveform();
			for(unsigned int samp=0; samp<sensors[sid]->nSamples(); samp++) {
				data[index] = (short int) wf[samp];
				index++;
			}
		}else{
			for(unsigned int i=0; i<datasize; i++){
				data[index] = (short int) 0;
				index++;
			}
		}
	}

	WriteWaveforms(data, dataset, nsensors, datasize, _ievt);
	delete[] data;
}

void next::HDF5Writer::WriteRunInfo(){
	//Group for sensors
	std::string sensors_group_name = std::string("/Sensors");
	hsize_t sensorsG = createGroup(_file, sensors_group_name);
	hid_t memtype = createSensorType();

	std::string pmt_table_name  = std::string("DataPMT");
	std::string blr_table_name  = std::string("DataBLR");
	std::string sipm_table_name = std::string("DataSiPM");
	hsize_t pmtsTable;
	hsize_t blrsTable;
	hsize_t sipmsTable;

	if (_hasPmts){
		pmtsTable = createTable(sensorsG, pmt_table_name,  memtype);
	}
	if (_hasBlrs){
		blrsTable  = createTable(sensorsG, blr_table_name,  memtype);
	}
	if (_hasSipms){
		sipmsTable = createTable(sensorsG, sipm_table_name, memtype);
	}

	int npmts  = _sensors.getNumberOfPmts();
	int nsipms = _sensors.getNumberOfSipms();
	sensor_t sensor;

	//Write PMTs
	if(!_nodb){
		int sensor_count = 0;
		if (_hasPmts){
			for(int i=0; i<npmts; i++){
				sensor.channel  = _sensors.sensorToElec(i);
				sensor.sensorID = i;
				if (sensor.channel >= 0){
					writeSensor(&sensor, pmtsTable, memtype, sensor_count);
					sensor_count++;
				}
			}
		}

		//Write BLRs
		sensor_count = 0;
		if (_hasBlrs){
			for(int i=0; i<npmts; i++){
				sensor.channel  = _sensors.sensorToElec(i);
				sensor.sensorID = i;
				if (sensor.channel >= 0){
					writeSensor(&sensor, blrsTable, memtype, sensor_count);
					sensor_count++;
				}
			}
		}
		//Write SIPMs
		int sensorid;
		sensor_count = 0;
		if (_hasSipms){
			for(int i=0; i<nsipms; i++){
				sensorid = PositiontoSipmID(i);
				sensor.channel  = _sensors.sensorToElec(sensorid);
				sensor.sensorID = sensorid;
				if (sensor.channel >= 0){
					writeSensor(&sensor, sipmsTable, memtype, sensor_count);
					sensor_count++;
				}
			}
		}
	}else{
		//Write PMTs
		for(int i=0; i<_pmt_elecids.size(); i++){
			sensor.channel  = _pmt_elecids[i];
			sensor.sensorID = -1;
			writeSensor(&sensor, pmtsTable, memtype, i);
		}

		//Write BLRs
		for(int i=0; i<_blr_elecids.size(); i++){
			sensor.channel  = _blr_elecids[i];
			sensor.sensorID = -1;
			writeSensor(&sensor, blrsTable, memtype, i);
		}

		//Write Sipms
		for(int i=0; i<_sipm_elecids.size(); i++){
			sensor.channel  = _sipm_elecids[i];
			sensor.sensorID = -1;
			writeSensor(&sensor, sipmsTable, memtype, i);
		}
	}
}
