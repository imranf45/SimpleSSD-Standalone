/**
 * Copyright (C) 2017 CAMELab
 *
 * This file is part of SimpleSSD.
 *
 * SimpleSSD is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * SimpleSSD is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with SimpleSSD.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors: Jie Zhang <jie@camelab.org>
 *          Donghyun Gouk <kukdh1@camelab.org>
 */

#include "ssd_sim.h"

RequestGenerator::RequestGenerator(uint32_t _MAX_REQ, uint64_t _start_PPN,
                                   uint32_t _REQ_SIZE, uint32_t _read_fraction,
                                   uint32_t _read_denominator, uint32_t _random_fraction,
                                   uint32_t _random_denominator, uint32_t _randSeed,
                                   int _IOGEN, string _fn) {
  cur_page = _MAX_REQ;
  MAX_REQ = _MAX_REQ;
  cur_REQ = 0;

  start_PPN = _start_PPN;
  REQ_SIZE = _REQ_SIZE;
  curRequest.PPN = 0;
  curRequest.REQ_SIZE = 0;
  curRequest.Oper = OPER_READ;
  curRequest.IOGEN = 0;
  readratio.fraction = _read_fraction;
  readratio.denominator = _read_denominator;
  randomratio.fraction = _random_fraction;
  randomratio.denominator = _random_denominator;
  srand(_randSeed);
  IOGEN = _IOGEN;
  fn = _fn;
  if (fn != "none") tr = new TraceReader(fn);
}

RequestGenerator::~RequestGenerator() {
  // DO NOTHING
}

bool RequestGenerator::generate_request(){
  if (fn == "none"){
    if (cur_REQ >= MAX_REQ) {
      return false;
    }

    if (cur_REQ % randomratio.denominator < randomratio.fraction) { //random access
      int cmd = rand() % 100; //for testing
      curRequest.PPN = (uint64_t)((uint64_t)(rand()% (MAX_REQ * REQ_SIZE / 2))*(uint64_t)8192);
    }
    else {
      curRequest.PPN = start_PPN; //8192 is default page size
      start_PPN += 4096*REQ_SIZE;
    }

    if (cur_REQ % readratio.denominator < readratio.fraction) {
      curRequest.Oper = OPER_READ;
    }
    else {
      curRequest.Oper = OPER_WRITE;
    }

    curRequest.REQ_SIZE = REQ_SIZE;
    curRequest.IOGEN = IOGEN;
    cur_REQ++;
  }
  else{
    return tr->ReadIO(curRequest.PPN, curRequest.Oper, curRequest.REQ_SIZE);
  }
  return true;
}

/*==============================
Main
==============================*/
int main(int argc, char* argv[])
{
  //Initialization configuration
  unsigned SSD_enable = 1;
  string SSD_config;
  if (argc > 1){
    SSD_config = argv[1];
    cout << "Config File:" << SSD_config << "\n";
    cout << flush;

    // Create config
    Config cfg(SSD_config);

    // Create HIL model
    HIL *m_hil = new HIL(0, 1, cfg);
    m_hil->setSSD(1);

    //*********initialize trace generator*****************
    currentTick = 0;
    std::list<Tick> request_queue;
    RequestGenerator* RG = new RequestGenerator(
      cfg.MaxRequest, cfg.StartPPN, cfg.RequestSize, cfg.ReadFraction, cfg.ReadDenominator,
      cfg.RandomFraction, cfg.RandomDenominator, cfg.RandomSeed, cfg.IOGEN, cfg.TraceFile
    );
    cout << "Trace file is" << cfg.TraceFile << "\n";
    sampled_period = EPOCH_INTERVAL;
    //*****************************************************
    for (unsigned i = 0; i < cfg.QueueDepth; i++){
      if (!RG->generate_request()) {
        break;
      }

      m_hil->SSDoperation(RG->curRequest.PPN, RG->curRequest.REQ_SIZE, currentTick, RG->curRequest.Oper);
      m_hil->updateFinishTick(RG->curRequest.PPN);
      m_hil->print_sample(sampled_period);
      request_queue.push_front(finishTick);
    }
    request_queue.sort();

    printf("From the head to the end:\t");
    for (std::list<Tick>::iterator e = request_queue.begin(); e != request_queue.end(); e++){
      printf("%" PRIu64 "\t", *e);
    }
    printf("\n");
    while(1){
      if (!RG->generate_request()) {
        break;
      }

      //sync operation
      currentTick = request_queue.front();
      m_hil->SSDoperation(RG->curRequest.PPN, RG->curRequest.REQ_SIZE, currentTick, RG->curRequest.Oper);
      Tick scheduledTick = m_hil->updateDelay(RG->curRequest.PPN) + currentTick;
      m_hil->updateFinishTick(RG->curRequest.PPN);
      m_hil->print_sample(sampled_period);
      request_queue.pop_front();
      request_queue.push_front(scheduledTick);
      request_queue.sort();

    }
    //*****************************************************


    //print final results:
    struct output_result output;
    printf("<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<\n");
    m_hil->get_parameter(PAL_LAYER, output);
    printf(">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>\n");
    printf("PAL: read count = %lf\n",output.statistics[RD][CAPACITY][COUNT]);
    printf("PAL: write count = %lf\n",output.statistics[WR][CAPACITY][COUNT]);
    printf("PAL: device idle time = %lf\n",output.statistics[TOT][DEVICE_IDLE][TOT]);
    printf("PAL: device busy time = %lf\n",output.statistics[TOT][DEVICE_BUSY][TOT]);
    printf("PAL: average read bandwidth = %lf\n", output.statistics[RD][BANDWIDTH][AVG]);
    printf("PAL: average write bandwidth = %lf\n", output.statistics[WR][BANDWIDTH][AVG]);
    printf("PAL: average read IOPS = %lf\n", output.statistics[RD][IOPS][AVG] );
    printf("PAL: average write IOPS = %lf\n", output.statistics[WR][IOPS][AVG] );
    printf("PAL: average read latency (ms) = %lf\n", output.statistics[RD][LATENCY][AVG] / 1000 / 1000 / 1000);
    printf("PAL: average write latency (ms) = %lf\n", output.statistics[WR][LATENCY][AVG] / 1000 / 1000 / 1000);
    printf("<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<\n");
    m_hil->get_parameter(FTL_HOST_LAYER, output);
    printf(">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>\n");
    printf("FTL_HOST: read count = %lf\n",output.statistics[RD][CAPACITY][COUNT]);
    printf("FTL_HOST: write count = %lf\n",output.statistics[WR][CAPACITY][COUNT]);
    printf("FTL_HOST: average read bandwidth = %lf\n", output.statistics[RD][BANDWIDTH][AVG]);
    printf("FTL_HOST: average write bandwidth = %lf\n", output.statistics[WR][BANDWIDTH][AVG]);
    printf("FTL_HOST: average read IOPS = %lf\n", output.statistics[RD][IOPS][AVG]);
    printf("FTL_HOST: average write IOPS = %lf\n", output.statistics[WR][IOPS][AVG]);
    printf("FTL_HOST: average read latency = %lf\n", output.statistics[RD][LATENCY][AVG]);
    printf("FTL_HOST: average write LATENCY = %lf\n", output.statistics[WR][LATENCY][AVG]);

    // delete m_hil;
    delete RG;

    return 0;
  }
  else {
    printf("Usage:\n\t%s [SSD-ConfigFile]\n", argv[0]);
    return 0;
  }

}
